#pragma once

#include <complex>

#include "fxval/core/error.hpp"
#include "fxval/core/units.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"

// Heston (1993) stochastic-volatility model for FX, semi-analytic European prices.
//
// Dynamics under the domestic risk-neutral measure (docs/model_documentation.md, section 4):
//   dS = (r_d - r_f) S dt + sqrt(v) S dW1
//   dv = kappa (theta - v) dt + xi sqrt(v) dW2,     d<W1, W2> = rho dt
//
// The characteristic function uses the numerically stable formulation of Albrecher, Mayer,
// Schoutens and Tistaert ("the little Heston trap"), written so that it stays accurate down to
// xi = 0. Prices come from a single Fourier integral (Lewis 2001) evaluated with adaptive
// Gauss-Kronrod quadrature. Accuracy and method: docs/model_documentation.md, section 4.

namespace fxval::models {

// Domain bounds. Like the bounds in limits.hpp they catch unit mistakes; they are generous.
inline constexpr double max_variance = 25.0;  // v0, theta: volatility up to 500%
inline constexpr double max_kappa = 100.0;
inline constexpr double max_vol_of_vol = 10.0;

// Validated Heston parameters, immutable once built.
//   v0     initial variance (not volatility), >= 0
//   kappa  speed of mean reversion, > 0
//   theta  long-run variance, > 0
//   xi     volatility of variance, >= 0 (xi = 0 is the deterministic-variance limit)
//   rho    correlation of the two Brownian motions, in [-1, 1]
class HestonParams final {
public:
  [[nodiscard]] static Result<HestonParams> create(
      double v0, double kappa, double theta, double xi, double rho);

  [[nodiscard]] double v0() const noexcept { return v0_; }
  [[nodiscard]] double kappa() const noexcept { return kappa_; }
  [[nodiscard]] double theta() const noexcept { return theta_; }
  [[nodiscard]] double xi() const noexcept { return xi_; }
  [[nodiscard]] double rho() const noexcept { return rho_; }

  // True when 2 kappa theta >= xi^2: the variance process never reaches zero.
  [[nodiscard]] bool feller_satisfied() const noexcept;

private:
  HestonParams(double v0, double kappa, double theta, double xi, double rho) noexcept
      : v0_(v0), kappa_(kappa), theta_(theta), xi_(xi), rho_(rho) {}

  double v0_;
  double kappa_;
  double theta_;
  double xi_;
  double rho_;
};

// Market side of a European option. Volatility is not an input: it comes from the model.
struct HestonVanilla {
  Spot spot;
  Strike strike;
  Rate domestic;
  Rate foreign;
  Years expiry;
};

// E[ integral_0^tau v_t dt ] = theta tau + (v0 - theta)(1 - e^{-kappa tau}) / kappa.
// Also the total variance of the Black-Scholes model that Heston reduces to when xi = 0.
[[nodiscard]] double expected_integrated_variance(const HestonParams& params, double tau) noexcept;

// Characteristic function E[exp(i u X)] of X = ln(S_tau / F), where F is the forward. The drift
// is removed, so the function does not depend on the interest rates. It is defined for complex
// u with Im(u) in [-1, 0] (finite moments of order 0 to 1); phi(0) = 1 and phi(-i) = 1.
[[nodiscard]] std::complex<double> log_forward_characteristic_function(
    const HestonParams& params, double tau, std::complex<double> u) noexcept;

// European option price (domestic currency per one unit of foreign notional).
// Errors: numerical_failure if a value is not finite or violates the no-arbitrage bounds,
// not_converged if the quadrature does not reach its tolerance.
[[nodiscard]] Result<double> heston_price(const HestonVanilla& market,
                                          const HestonParams& params,
                                          pricing::OptionType type);

}  // namespace fxval::models
