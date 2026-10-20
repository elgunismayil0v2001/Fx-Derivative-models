#include "fxval/calibration/heston_calibration.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "fxval/calibration/heston_smile.hpp"
#include "fxval/mc/normal_stream.hpp"
#include "fxval/numerics/normal.hpp"
#include "fxval/smile/smile_slice.hpp"

namespace fxval::calibration {
namespace {

using Params = std::array<double, 5>;
using Vector = std::vector<double>;

struct Target {
  models::HestonVanilla vanilla;
  Pivot pivot;
  double strike;
  double market_vol;
};

[[nodiscard]] Result<models::HestonParams> to_heston(const Params& p) {
  return models::HestonParams::create(p[0], p[1], p[2], p[3], p[4]);
}

// Residual vector of the natural parameters; also fills `model_vols` when asked.
[[nodiscard]] Result<Vector> residuals_at(const std::vector<Target>& targets, const Params& p) {
  const Result<models::HestonParams> params = to_heston(p);
  if (!params) {
    return params.error();
  }
  Vector r(targets.size());
  for (std::size_t i = 0; i < targets.size(); ++i) {
    const Result<Volatility> vol = heston_implied_volatility(targets[i].vanilla, params.value());
    if (!vol) {
      return vol.error();
    }
    r[i] = vol.value().value() - targets[i].market_vol;
  }
  return r;
}

// Logistic map between the unbounded optimiser variable u and a parameter in (lo, hi).
[[nodiscard]] double to_parameter(double u, double lo, double hi) {
  return lo + (hi - lo) / (1.0 + std::exp(-u));
}

[[nodiscard]] double to_unbounded(double p, double lo, double hi) {
  const double f = std::clamp((p - lo) / (hi - lo), 1e-9, 1.0 - 1e-9);
  return std::log(f / (1.0 - f));
}

[[nodiscard]] Params map_all(const Vector& u, const ParameterBounds& b) {
  Params p{};
  for (std::size_t j = 0; j < 5; ++j) {
    p[j] = to_parameter(u[j], b.lower[j], b.upper[j]);
  }
  return p;
}

[[nodiscard]] Result<std::vector<Target>> build_targets(const CalibrationMarket& market,
                                                        const std::vector<MarketSlice>& slices) {
  if (slices.empty()) {
    return Error{ErrorCode::invalid_argument, "calibration needs at least one expiry"};
  }
  std::vector<Target> targets;
  targets.reserve(3 * slices.size());
  for (const MarketSlice& s : slices) {
    if (!(s.expiry.value() > 0.0)) {
      return Error{ErrorCode::invalid_argument, "calibration expiries must be positive"};
    }
    const smile::SmileMarket smile_market{market.spot, market.domestic, market.foreign, s.expiry};
    const Result<smile::SmileSlice> slice =
        smile::SmileSlice::from_quotes(smile_market, s.quotes, market.convention);
    if (!slice) {
      return slice.error();
    }
    const smile::PivotStrikes& strikes = slice.value().pivot_strikes();
    const smile::PivotVolatilities& vols = slice.value().pivot_volatilities();
    const struct {
      Pivot pivot;
      double strike;
      double vol;
    } pivots[] = {{Pivot::put, strikes.put, vols.put.value()},
                  {Pivot::atm, strikes.atm, vols.atm.value()},
                  {Pivot::call, strikes.call, vols.call.value()}};
    for (const auto& pv : pivots) {
      const Result<Strike> strike = Strike::from_value(pv.strike);
      if (!strike) {
        return strike.error();
      }
      targets.push_back(
          Target{models::HestonVanilla{
                     market.spot, strike.value(), market.domestic, market.foreign, s.expiry},
                 pv.pivot,
                 pv.strike,
                 pv.vol});
    }
  }
  return targets;
}

// Data-driven start: variance level from the shortest and longest ATM, skew sign from the RR.
[[nodiscard]] Params data_driven_start(const std::vector<MarketSlice>& slices,
                                       const ParameterBounds& b) {
  const auto by_expiry = [](const MarketSlice& x, const MarketSlice& y) {
    return x.expiry.value() < y.expiry.value();
  };
  const auto shortest = std::min_element(slices.begin(), slices.end(), by_expiry);
  const auto longest = std::max_element(slices.begin(), slices.end(), by_expiry);
  double rr_over_atm = 0.0;
  for (const MarketSlice& s : slices) {
    rr_over_atm += s.quotes.risk_reversal.value() / s.quotes.atm.value();
  }
  rr_over_atm /= static_cast<double>(slices.size());
  const double v0 = shortest->quotes.atm.value() * shortest->quotes.atm.value();
  const double theta = longest->quotes.atm.value() * longest->quotes.atm.value();
  Params p{v0, 1.5, theta, 0.5, std::clamp(4.0 * rr_over_atm, -0.8, 0.8)};
  for (std::size_t j = 0; j < 5; ++j) {
    p[j] = std::clamp(p[j], b.lower[j] * 1.01 + 1e-12, b.upper[j] * 0.99 - 1e-12);
  }
  return p;
}

// Random start k: counter-based, so the same (seed, k) always gives the same start.
[[nodiscard]] Params random_start(const std::vector<MarketSlice>& slices,
                                  const ParameterBounds& b,
                                  std::uint64_t seed,
                                  std::uint64_t k) {
  mc::NormalStream stream(seed, k);
  double z[6];
  stream.next_pair(z[0], z[1]);
  stream.next_pair(z[2], z[3]);
  stream.next_pair(z[4], z[5]);
  double u[5];
  for (std::size_t j = 0; j < 5; ++j) {
    u[j] = numerics::normal_cdf(z[j]);  // uniform in (0, 1)
  }
  double atm_min = std::numeric_limits<double>::infinity();
  double atm_max = 0.0;
  for (const MarketSlice& s : slices) {
    atm_min = std::min(atm_min, s.quotes.atm.value());
    atm_max = std::max(atm_max, s.quotes.atm.value());
  }
  const double var_lo = 0.5 * atm_min * atm_min;
  const double var_hi = 1.5 * atm_max * atm_max;
  Params p{var_lo + (var_hi - var_lo) * u[0],
           0.3 * std::exp(std::log(6.0 / 0.3) * u[1]),
           var_lo + (var_hi - var_lo) * u[2],
           0.1 + 1.1 * u[3],
           -0.9 + 1.8 * u[4]};
  for (std::size_t j = 0; j < 5; ++j) {
    p[j] = std::clamp(p[j], b.lower[j] * 1.01 + 1e-12, b.upper[j] * 0.99 - 1e-12);
  }
  return p;
}

}  // namespace

Result<CalibrationResult> calibrate_heston(const CalibrationMarket& market,
                                           const std::vector<MarketSlice>& slices,
                                           const CalibrationSettings& settings) {
  const ParameterBounds& bounds = settings.bounds;
  for (std::size_t j = 0; j < 5; ++j) {
    if (!(bounds.lower[j] < bounds.upper[j]) || !std::isfinite(bounds.lower[j]) ||
        !std::isfinite(bounds.upper[j])) {
      return Error{ErrorCode::invalid_argument, "parameter bounds must satisfy lower < upper"};
    }
  }
  if (settings.random_starts < 0) {
    return Error{ErrorCode::invalid_argument, "random_starts must not be negative"};
  }
  const Result<std::vector<Target>> built = build_targets(market, slices);
  if (!built) {
    return built.error();
  }
  const std::vector<Target>& targets = built.value();

  std::vector<Params> starts;
  if (settings.initial_guess) {
    const models::HestonParams& g = *settings.initial_guess;
    Params p{g.v0(), g.kappa(), g.theta(), g.xi(), g.rho()};
    for (std::size_t j = 0; j < 5; ++j) {
      p[j] = std::clamp(p[j], bounds.lower[j] * 1.01 + 1e-12, bounds.upper[j] * 0.99 - 1e-12);
    }
    starts.push_back(p);
  } else {
    starts.push_back(data_driven_start(slices, bounds));
  }
  for (int k = 0; k < settings.random_starts; ++k) {
    starts.push_back(random_start(slices, bounds, settings.seed, static_cast<std::uint64_t>(k)));
  }

  const numerics::ResidualFunction unbounded = [&](const Vector& u) {
    return residuals_at(targets, map_all(u, bounds));
  };

  std::vector<StartOutcome> outcomes;
  std::optional<Params> best_params;
  double best_cost = std::numeric_limits<double>::infinity();
  bool best_converged = false;
  std::size_t best_index = 0;
  int evaluations = 0;
  Error last_error{ErrorCode::not_converged, "no start produced a usable fit"};

  for (std::size_t s = 0; s < starts.size(); ++s) {
    Vector u0(5);
    for (std::size_t j = 0; j < 5; ++j) {
      u0[j] = to_unbounded(starts[s][j], bounds.lower[j], bounds.upper[j]);
    }
    const Result<numerics::LeastSquaresResult> run =
        numerics::levenberg_marquardt(unbounded, u0, settings.optimiser);
    StartOutcome outcome{starts[s],
                         starts[s],
                         std::numeric_limits<double>::infinity(),
                         0,
                         numerics::LeastSquaresTermination::stalled,
                         true};
    if (run) {
      const numerics::LeastSquaresResult& r = run.value();
      evaluations += r.evaluations;
      outcome.end = map_all(r.x, bounds);
      outcome.cost = r.cost;
      outcome.iterations = r.iterations;
      outcome.termination = r.termination;
      outcome.failed = false;
      // Prefer a converged run; among equals the lower cost.
      const bool better = (r.converged() && !best_converged) ||
                          (r.converged() == best_converged && r.cost < best_cost);
      if (better) {
        best_cost = r.cost;
        best_converged = r.converged();
        best_params = outcome.end;
        best_index = s;
      }
    } else {
      last_error = run.error();
    }
    outcomes.push_back(outcome);
  }
  if (!best_params) {
    return last_error;
  }

  const Result<models::HestonParams> fitted = to_heston(*best_params);
  if (!fitted) {
    return fitted.error();
  }
  const Result<Vector> final_r = residuals_at(targets, *best_params);
  if (!final_r) {
    return final_r.error();
  }

  CalibrationResult result{fitted.value(),
                           0.0,
                           0.0,
                           {},
                           std::move(outcomes),
                           best_index,
                           evaluations,
                           best_converged,
                           {},
                           {}};
  double sum_sq = 0.0;
  for (std::size_t i = 0; i < targets.size(); ++i) {
    const double error = final_r.value()[i];
    sum_sq += error * error;
    result.max_abs_error = std::max(result.max_abs_error, std::abs(error));
    result.fits.push_back(QuoteFit{targets[i].vanilla.expiry,
                                   targets[i].pivot,
                                   targets[i].strike,
                                   targets[i].market_vol,
                                   targets[i].market_vol + error,
                                   error});
  }
  result.rms_error = std::sqrt(sum_sq / static_cast<double>(targets.size()));
  for (std::size_t j = 0; j < 5; ++j) {
    const double fraction =
        ((*best_params)[j] - bounds.lower[j]) / (bounds.upper[j] - bounds.lower[j]);
    result.near_bound[j] = fraction < 1e-3 || fraction > 1.0 - 1e-3;
  }

  result.standard_errors.fill(std::numeric_limits<double>::infinity());
  if (settings.compute_standard_errors) {
    const numerics::ResidualFunction natural = [&](const Vector& p) {
      return residuals_at(targets, Params{p[0], p[1], p[2], p[3], p[4]});
    };
    const Vector p_vec(best_params->begin(), best_params->end());
    const Result<Vector> jacobian = numerics::finite_difference_jacobian(natural, p_vec, 1e-6);
    const std::size_t m = targets.size();
    if (jacobian && m > 5) {
      const double sigma2 = sum_sq / static_cast<double>(m - 5);
      const Vector se = numerics::parameter_standard_errors(jacobian.value(), m, 5, sigma2);
      std::copy(se.begin(), se.end(), result.standard_errors.begin());
    }
  }
  return result;
}

}  // namespace fxval::calibration
