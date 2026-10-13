#pragma once

// Helpers shared by the Heston tests (unit and validation).

#include <cmath>

#include "fxval/models/heston.hpp"
#include "support/gk_test_support.hpp"

namespace fxval::testing {

inline models::HestonParams make_params(
    double v0, double kappa, double theta, double xi, double rho) {
  return must(models::HestonParams::create(v0, kappa, theta, xi, rho));
}

inline models::HestonVanilla make_heston_market(
    double spot, double strike, double domestic, double foreign, double years) {
  return models::HestonVanilla{must(Spot::from_value(spot)),
                               must(Strike::from_value(strike)),
                               must(Rate::from_decimal(domestic)),
                               must(Rate::from_decimal(foreign)),
                               must(Years::from_years(years))};
}

inline double heston_value(pricing::OptionType type,
                           double spot,
                           double strike,
                           double domestic,
                           double foreign,
                           double years,
                           const models::HestonParams& params) {
  return must(models::heston_price(
      make_heston_market(spot, strike, domestic, foreign, years), params, type));
}

// Garman-Kohlhagen call price with an explicit volatility, same argument order as the Heston
// helper.
inline double gk_call(
    double spot, double strike, double domestic, double foreign, double years, double sigma) {
  return gk_value(pricing::OptionType::call, spot, strike, domestic, foreign, sigma, years);
}

// Black-Scholes implied volatility of a call price, by bisection. Test helper only: slow and
// simple on purpose, so that it is obviously correct.
inline double implied_vol_of_call(
    double price, double spot, double strike, double domestic, double foreign, double years) {
  double lo = 1e-4;
  double hi = 3.0;
  for (int i = 0; i < 200; ++i) {
    const double mid = 0.5 * (lo + hi);
    (gk_call(spot, strike, domestic, foreign, years, mid) < price ? lo : hi) = mid;
  }
  return 0.5 * (lo + hi);
}

}  // namespace fxval::testing
