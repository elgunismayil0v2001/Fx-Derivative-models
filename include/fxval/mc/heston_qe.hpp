#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

#include "fxval/core/error.hpp"
#include "fxval/core/units.hpp"
#include "fxval/mc/heston_euler.hpp"  // HestonState
#include "fxval/mc/normal_stream.hpp"
#include "fxval/models/heston.hpp"

namespace fxval::mc {

// Heston paths by the Quadratic-Exponential (QE) scheme of Andersen (2008, "Efficient simulation
// of the Heston stochastic volatility model", Journal of Computational Finance 11(3)), with the
// central discretisation of ln S and the martingale correction.
//
// WHY: the variance is a square-root (CIR) process. Euler steps can make it negative and need a
// truncation rule, which is the main source of Euler's bias when the Feller condition fails.
// QE instead samples the next variance from a distribution that matches the exact conditional
// mean and variance of v_{n+1} given v_n. Those two moments are known in closed form
// (the exact law is a scaled non-central chi-square):
//     m  = theta + (v - theta) e^{-kappa dt}
//     s2 = v xi^2 e^{-kappa dt} (1 - e^{-kappa dt}) / kappa
//          + theta xi^2 (1 - e^{-kappa dt})^2 / (2 kappa)
//     psi = s2 / m^2
// and the sampling rule depends on psi:
//   * psi <= psi_c (large variance relative to its noise): v' = a (b + Z)^2, Z ~ N(0,1), with
//     b^2 = 2/psi - 1 + sqrt(2/psi) sqrt(2/psi - 1) and a = m / (1 + b^2)   ("quadratic")
//   * psi >  psi_c (variance near zero): v' = 0 with probability p = (psi - 1)/(psi + 1),
//     otherwise exponentially distributed, v' = ln((1 - p)/(1 - U)) / beta with
//     beta = (1 - p)/m, U ~ Uniform(0,1)   ("exponential", with a point mass at zero)
// Both branches reproduce m and s2 exactly. psi_c = 1.5 is Andersen's recommendation (any value in
// [1, 2] is valid and the results are insensitive to it).
//
// The spot, given v_n and v_{n+1}, uses the central discretisation (gamma1 = gamma2 = 1/2):
//     ln S' = ln S + (r_d - r_f) dt + K0 + K1 v + K2 v' + sqrt(K3 v + K4 v') Z_perp
//     K1 = dt/2 (kappa rho/xi - 1/2) - rho/xi,   K2 = dt/2 (kappa rho/xi - 1/2) + rho/xi
//     K3 = dt/2 (1 - rho^2),                      K4 = dt/2 (1 - rho^2)
// K0 is chosen so that E[S' | S, v] = S e^{(r_d - r_f) dt} EXACTLY (martingale correction):
//     K0 = -ln M - (K1 + K3/2) v,   M = E[ exp(A v') | v ],   A = K2 + K4/2,
// with M available in closed form in each branch. M exists only if 2 A a < 1 (quadratic) or
// A < beta (exponential). When that fails (large dt and strongly positive rho), the scheme falls
// back to the uncorrected K0 = -rho kappa theta dt / xi, which is finite but not exactly a
// martingale; the Monte Carlo test that checks E[S_T] = forward would show a bias in that regime.
//
// Requirements: xi > 0 (the formulas divide by xi; for xi = 0 use the exact GBM / Euler models),
// expiry > 0, psi_c in [1, 2].
//
// Random numbers: one Philox block per step gives Z_v and Z_perp. The exponential branch needs a
// uniform; it is U = Phi(Z_v), computed with erfc (exact transformation of a normal to a uniform,
// and 1 - U is computed directly from erfc so the right tail keeps its precision).
//
// This is a new class that satisfies PathModel: the engine was not edited to support it
// (ADR 0005, open for extension).
class HestonQuadraticExponential {
public:
  static constexpr double default_psi_critical = 1.5;

  // invalid_argument if the expiry is not positive, xi is not positive or psi_c is outside [1, 2].
  [[nodiscard]] static Result<HestonQuadraticExponential> create(
      Spot spot,
      Rate domestic,
      Rate foreign,
      Years expiry,
      models::HestonParams params,
      double psi_critical = default_psi_critical);

  // One variance step of length dt from v >= 0 using the normal draw z (and, in the exponential
  // branch, the uniform Phi(z)). Public so tests can check the conditional mean and variance
  // against the exact formulas above without simulating the spot.
  [[nodiscard]] double next_variance(double v, double dt, double z) const noexcept {
    const Coefficients c = coefficients(dt);
    return variance_step(c, v, z).value;
  }

  // Full terminal state; used by tests that compare with an independent implementation.
  [[nodiscard]] HestonState simulate_terminal_state(NormalStream& rng,
                                                    std::size_t steps) const noexcept {
    const double dt = expiry_ / static_cast<double>(steps);
    const Coefficients c = coefficients(dt);
    const double rho = params_.rho();
    const double xi = params_.xi();
    const double kappa = params_.kappa();
    const double theta = params_.theta();
    const double rate_drift = rate_diff_ * dt;
    const double k_central = 0.5 * dt * (kappa * rho / xi - 0.5);
    const double k1 = k_central - rho / xi;
    const double k2 = k_central + rho / xi;
    const double k3 = 0.5 * dt * (1.0 - rho * rho);
    const double k4 = k3;
    const double a_coefficient = k2 + 0.5 * k4;
    const double k0_plain = -rho * kappa * theta * dt / xi;

    double log_spot = log_spot0_;
    double variance = params_.v0();
    for (std::size_t i = 0; i < steps; ++i) {
      double z_variance = 0.0;
      double z_independent = 0.0;
      rng.next_pair(z_variance, z_independent);
      const VarianceStep step = variance_step(c, variance, z_variance);
      double k0 = k0_plain;
      const double log_m = log_moment(step, a_coefficient);
      if (std::isfinite(log_m)) {
        k0 = -log_m - (k1 + 0.5 * k3) * variance;
      }
      const double spot_variance = std::max(k3 * variance + k4 * step.value, 0.0);
      log_spot += rate_drift + k0 + k1 * variance + k2 * step.value +
                  std::sqrt(spot_variance) * z_independent;
      variance = step.value;
    }
    return {std::exp(log_spot), variance};
  }

  [[nodiscard]] double simulate_terminal_spot(NormalStream& rng, std::size_t steps) const noexcept {
    return simulate_terminal_state(rng, steps).spot;
  }

private:
  // Quantities that depend on dt only.
  struct Coefficients {
    double decay;  // e^{-kappa dt}
    double one_minus_decay;
    double s2_v;      // coefficient of v in s2
    double s2_const;  // constant part of s2
  };

  // Result of one variance step with the information the martingale correction needs.
  struct VarianceStep {
    double value;  // v'
    bool quadratic;
    double a;     // quadratic: a, exponential: unused
    double b2;    // quadratic: b^2
    double p;     // exponential: p
    double beta;  // exponential: beta
  };

  HestonQuadraticExponential(double log_spot0,
                             double rate_diff,
                             double expiry,
                             models::HestonParams params,
                             double psi_critical) noexcept
      : log_spot0_(log_spot0), rate_diff_(rate_diff), expiry_(expiry), params_(params),
        psi_critical_(psi_critical) {}

  [[nodiscard]] Coefficients coefficients(double dt) const noexcept {
    const double kappa = params_.kappa();
    const double xi2 = params_.xi() * params_.xi();
    const double decay = std::exp(-kappa * dt);
    const double one_minus = -std::expm1(-kappa * dt);  // accurate for small kappa dt
    return {decay,
            one_minus,
            xi2 * decay * one_minus / kappa,
            params_.theta() * xi2 * one_minus * one_minus / (2.0 * kappa)};
  }

  [[nodiscard]] VarianceStep variance_step(const Coefficients& c,
                                           double v,
                                           double z) const noexcept {
    // m = theta + (v - theta) e^{-kappa dt}, written as a sum of non-negative terms so it never
    // loses precision or turns negative.
    const double m = params_.theta() * c.one_minus_decay + v * c.decay;
    const double s2 = c.s2_v * v + c.s2_const;
    const double psi = s2 / (m * m);
    if (psi <= psi_critical_) {
      const double inverse = 2.0 / psi;
      const double b2 = inverse - 1.0 + std::sqrt(inverse) * std::sqrt(inverse - 1.0);
      const double a = m / (1.0 + b2);
      const double root = std::sqrt(b2) + z;
      return {a * root * root, true, a, b2, 0.0, 0.0};
    }
    const double p = (psi - 1.0) / (psi + 1.0);
    const double beta = (1.0 - p) / m;
    constexpr double inv_sqrt2 = 1.0 / std::numbers::sqrt2;
    const double u = 0.5 * std::erfc(-z * inv_sqrt2);  // Phi(z)
    double value = 0.0;
    if (u > p) {
      const double one_minus_u = 0.5 * std::erfc(z * inv_sqrt2);  // 1 - Phi(z), kept accurate
      value = std::log((1.0 - p) / one_minus_u) / beta;
    }
    return {value, false, 0.0, 0.0, p, beta};
  }

  // ln M, M = E[exp(A v') | v]; NaN when the moment does not exist (the caller then falls back).
  [[nodiscard]] static double log_moment(const VarianceStep& step, double a_coeff) noexcept {
    if (step.quadratic) {
      const double d = 1.0 - 2.0 * a_coeff * step.a;
      if (d <= 0.0) {
        return std::nan("");
      }
      return a_coeff * step.b2 * step.a / d - 0.5 * std::log(d);
    }
    if (a_coeff >= step.beta) {
      return std::nan("");
    }
    return std::log(step.p + step.beta * (1.0 - step.p) / (step.beta - a_coeff));
  }

  double log_spot0_;
  double rate_diff_;  // r_d - r_f
  double expiry_;
  models::HestonParams params_;
  double psi_critical_;
};

}  // namespace fxval::mc
