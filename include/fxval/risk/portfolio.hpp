#pragma once

#include <span>
#include <vector>

#include "fxval/core/error.hpp"
#include "fxval/core/units.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"
#include "fxval/risk/var.hpp"

// An FX portfolio on one currency pair and the VaR methods that revalue or approximate it
// (docs/model_documentation.md section 9.5).
//
// The risk factor is the spot only. Volatility, rates and the expiries of the options are held
// fixed over the horizon (no vega risk, no theta, no rate risk: limitation L-26). One unit is one
// unit of foreign currency; values and P&L are in domestic currency.
//
// Scenario convention: a "return" x moves the spot to S0 (1 + x) in the delta-gamma and full
// revaluation functions of this header (so x is a simple return, the variable a delta-gamma
// expansion is written in); historical and filtered historical scenarios are LOG returns r and use
// S0 exp(r). The two differ by x - r = r^2 / 2, below 3e-4 of the move for a 1-day 99% shock.

namespace fxval::risk {

struct OptionLeg {
  pricing::OptionType type;
  Strike strike;
  Years expiry;
  Volatility volatility;
  double quantity;  // units of foreign notional; negative for a short position
};

struct FxPortfolio {
  Spot spot;
  Rate domestic;
  Rate foreign;
  double spot_units = 0.0;  // units of foreign currency held (negative: short)
  std::vector<OptionLeg> options;
};

// Value in domestic currency with the spot replaced by `spot_value`.
// Errors: invalid_argument for a non-positive spot_value; pricing errors otherwise.
[[nodiscard]] Result<double> portfolio_value(const FxPortfolio& portfolio, double spot_value);

// P&L of a scenario: value(S0 (1 + x)) - value(S0), simple return x > -1.
[[nodiscard]] Result<double> pnl_for_simple_return(const FxPortfolio& portfolio, double x);
// P&L of a scenario: value(S0 exp(r)) - value(S0), log return r.
[[nodiscard]] Result<double> pnl_for_log_return(const FxPortfolio& portfolio, double r);

// dV/dS and d2V/dS2 of the whole portfolio at the current spot (analytic Garman-Kohlhagen Greeks).
struct SpotSensitivities {
  double delta;
  double gamma;
};
[[nodiscard]] Result<SpotSensitivities> spot_sensitivities(const FxPortfolio& portfolio);

// The five methods.
// 1. Delta-normal: P&L = delta S0 x, x ~ N(0, sigma^2).
[[nodiscard]] Result<VarEs> delta_normal_var_es(const FxPortfolio& portfolio,
                                                double sigma,
                                                double level);
// 2. Delta-gamma: P&L = delta S0 x + gamma S0^2 x^2 / 2, x ~ N(0, sigma^2); VaR by Cornish-Fisher
//    from the EXACT first four moments of that quadratic form (section 9.5). No ES (no closed
//    form): `es` of the result is the ES of the quantile grid of the same quadratic (section 9.5).
[[nodiscard]] Result<VarEs> delta_gamma_var_es(const FxPortfolio& portfolio,
                                               double sigma,
                                               double level);
// 3. Full revaluation, normal returns: revalue at the quantile grid x_i = sigma z_{(i - 1/2)/N},
//    i = 1..N, and take the historical VaR of those P&Ls. Deterministic (no random numbers); the
//    error is the grid error, O(1/N) in the probability.
[[nodiscard]] Result<VarEs> full_revaluation_normal_var_es(const FxPortfolio& portfolio,
                                                           double sigma,
                                                           double level,
                                                           std::size_t grid = 100000);
// 4. Historical simulation with full revaluation: log-return scenarios (for example the last 750).
[[nodiscard]] Result<VarEs> historical_revaluation_var_es(const FxPortfolio& portfolio,
                                                          std::span<const double> log_returns,
                                                          double level);
// 5. Filtered historical simulation: standardised residuals z_j scaled by the volatility forecast
//    of the next day, sigma_next z_j, revalued as log returns.
[[nodiscard]] Result<VarEs> filtered_historical_var_es(
    const FxPortfolio& portfolio,
    std::span<const double> standardised_residuals,
    double sigma_next,
    double level);

}  // namespace fxval::risk
