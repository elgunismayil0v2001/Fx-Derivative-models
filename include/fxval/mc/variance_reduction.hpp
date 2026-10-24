#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "fxval/core/error.hpp"
#include "fxval/mc/engine.hpp"
#include "fxval/mc/normal_stream.hpp"

// Variance reduction for the Monte Carlo engine.
//
// Both techniques are built on run_samples(), so they inherit the reproducibility of the engine:
// the result does not depend on the number of threads (test V-07 style checks are repeated for
// them in tests/unit/test_variance_reduction.cpp).
//
//  * ANTITHETIC VARIATES. Each sample is the average of two paths driven by the same random
//    numbers with opposite signs, (z, -z). Both are valid draws, so the average is unbiased; if the
//    payoff is monotone in the driving noise the two are negatively correlated and the average has
//    smaller variance. A "path" in McConfig.paths then means one PAIR, so one sample costs two
//    path simulations. Fair comparison with plain Monte Carlo is therefore at EQUAL COST:
//    plain with 2n paths against antithetic with n pairs (test V-08 does this).
//
//  * CONTROL VARIATE. Given a function g of the terminal spot with exactly known expectation
//    E[g] = mu, estimate E[f] by the mean of  f(S) - c (g(S) - mu). Any fixed c is unbiased,
//    because the subtracted term has mean zero. The variance is smallest for c* = Cov(f, g) /
//    Var(g), and drops by the factor 1 - corr(f, g)^2. If c is estimated from the same paths it
//    becomes random and correlated with the estimate (a small bias of order 1/n), so c is estimated
//    in a PILOT run on different random streams and then held fixed (estimate_control_coefficient).
//    The known mean must be exact for the model that is simulated: a wrong mu is not detected by
//    the standard error and biases the price (tests/validation/test_v08_* checks this on purpose).
//
// The controls for Heston: the terminal spot (E[S_T] is the forward, a martingale property of the
// model) and, for tests of the machinery under GBM, any Garman-Kohlhagen price (known exactly).

namespace fxval::mc {

// First random stream number used by pilot runs. Path streams are 0 .. paths-1 (paths <= 2e9),
// so pilot streams never coincide with the streams of the main run.
inline constexpr std::uint64_t pilot_stream_offset = std::uint64_t{1} << 62U;

// Control function that is identically zero: antithetic variates without a control variate.
struct NoControl {
  [[nodiscard]] double operator()(double /*terminal_spot*/) const noexcept { return 0.0; }
};

namespace detail {

// One (possibly antithetic) sample of the payoff and of the control, before any adjustment.
struct PayoffAndControl {
  double payoff;
  double control;
};

template <PathModel Model, class Payoff, class Control>
[[nodiscard]] PayoffAndControl evaluate_sample(const Model& model,
                                               const Payoff& payoff,
                                               const Control& control,
                                               std::uint64_t seed,
                                               std::uint64_t stream,
                                               std::size_t steps,
                                               bool antithetic) noexcept {
  NormalStream plus(seed, stream);
  const double s_plus = model.simulate_terminal_spot(plus, steps);
  if (!antithetic) {
    return {payoff(s_plus), control(s_plus)};
  }
  NormalStream minus(seed, stream, true);
  const double s_minus = model.simulate_terminal_spot(minus, steps);
  return {0.5 * (payoff(s_plus) + payoff(s_minus)), 0.5 * (control(s_plus) + control(s_minus))};
}

}  // namespace detail

// Estimate E[ payoff(S_T) ] (undiscounted) with a control variate g and/or antithetic pairs.
//   control_mean  exact E[g(S_T)] under the simulated model
//   coefficient   the fixed multiplier c (use estimate_control_coefficient, or 0 for none)
//   antithetic    average each path with its mirror image
// With control = NoControl{}, coefficient = 0 this is plain antithetic Monte Carlo.
template <PathModel Model, class Payoff, class Control>
  requires(std::is_nothrow_invocable_r_v<double, const Payoff&, double> &&
           std::is_nothrow_invocable_r_v<double, const Control&, double>)
[[nodiscard]] Result<McEstimate> run_variance_reduced(const Model& model,
                                                      const Payoff& payoff,
                                                      const Control& control,
                                                      double control_mean,
                                                      double coefficient,
                                                      bool antithetic,
                                                      const McConfig& config) {
  if (!std::isfinite(control_mean) || !std::isfinite(coefficient)) {
    return Error{ErrorCode::invalid_argument, "control mean and coefficient must be finite"};
  }
  const std::uint64_t seed = config.seed;
  const std::size_t steps = config.steps;
  return run_samples(
      [&](std::uint64_t path) noexcept {
        const auto s =
            detail::evaluate_sample(model, payoff, control, seed, path, steps, antithetic);
        return s.payoff - coefficient * (s.control - control_mean);
      },
      config);
}

// Antithetic variates only.
template <PathModel Model, class Payoff>
  requires std::is_nothrow_invocable_r_v<double, const Payoff&, double>
[[nodiscard]] Result<McEstimate> run_antithetic(const Model& model,
                                                const Payoff& payoff,
                                                const McConfig& config) {
  return run_variance_reduced(model, payoff, NoControl{}, 0.0, 0.0, true, config);
}

// Optimal control coefficient c* = Cov(f, g) / Var(g) from a pilot run of `pilot_paths` samples
// on streams pilot_stream_offset + 0 .. pilot_paths - 1 (independent of the main run), single
// threaded and therefore deterministic. With antithetic = true the covariance is of the pair
// averages, which is what the main run will use.
// Errors: invalid_argument (fewer than 100 pilot paths, bad steps), numerical_failure (the control
// has zero or non-finite variance, or the covariance is not finite).
template <PathModel Model, class Payoff, class Control>
  requires(std::is_nothrow_invocable_r_v<double, const Payoff&, double> &&
           std::is_nothrow_invocable_r_v<double, const Control&, double>)
[[nodiscard]] Result<double> estimate_control_coefficient(const Model& model,
                                                          const Payoff& payoff,
                                                          const Control& control,
                                                          bool antithetic,
                                                          std::uint64_t seed,
                                                          std::size_t steps,
                                                          std::size_t pilot_paths) {
  if (pilot_paths < 100 || pilot_paths > max_paths) {
    return Error{ErrorCode::invalid_argument, "pilot_paths must be in [100, 2e9]"};
  }
  if (steps < 1 || steps > max_steps) {
    return Error{ErrorCode::invalid_argument, "steps must be in [1, 1e6]"};
  }
  // Welford updates of the means and of the (co)variance sums.
  double n = 0.0;
  double mean_f = 0.0;
  double mean_g = 0.0;
  double m2_g = 0.0;
  double c_fg = 0.0;
  for (std::size_t i = 0; i < pilot_paths; ++i) {
    const auto s = detail::evaluate_sample(
        model, payoff, control, seed, pilot_stream_offset + i, steps, antithetic);
    n += 1.0;
    const double df = s.payoff - mean_f;
    const double dg = s.control - mean_g;
    mean_f += df / n;
    mean_g += dg / n;
    m2_g += dg * (s.control - mean_g);
    c_fg += df * (s.control - mean_g);
  }
  if (!(m2_g > 0.0) || !std::isfinite(m2_g) || !std::isfinite(c_fg)) {
    return Error{ErrorCode::numerical_failure,
                 "control variate has zero or non-finite variance in the pilot run"};
  }
  return c_fg / m2_g;
}

}  // namespace fxval::mc
