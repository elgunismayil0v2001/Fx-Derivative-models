#pragma once

// Helpers shared by the Garman-Kohlhagen tests. Typed inputs are built from plain numbers many
// times; each constructor returns a Result, so `must` unwraps one and throws with the library's
// own message if the value was rejected (the test then fails with that message).

#include <stdexcept>
#include <string>
#include <utility>

#include "fxval/core/error.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"

namespace fxval::testing {

template <class T>
T must(Result<T> result) {
  if (!result) {
    throw std::runtime_error(std::string(to_string(result.error().code)) + ": " +
                             result.error().message);
  }
  return std::move(result).value();
}

inline pricing::VanillaFx make_market(
    double spot, double strike, double domestic, double foreign, double years, double vol) {
  return pricing::VanillaFx{must(Spot::from_value(spot)),
                            must(Strike::from_value(strike)),
                            must(Rate::from_decimal(domestic)),
                            must(Rate::from_decimal(foreign)),
                            must(Years::from_years(years)),
                            must(Volatility::from_decimal(vol))};
}

// Argument order here follows the formula (s, k, r_d, r_f, sigma, t), not VanillaFx.
inline double gk_value(
    pricing::OptionType type, double s, double k, double rd, double rf, double sigma, double t) {
  return must(pricing::garman_kohlhagen_price(make_market(s, k, rd, rf, t, sigma), type));
}

inline pricing::GarmanKohlhagenGreeks gk_all(
    pricing::OptionType type, double s, double k, double rd, double rf, double sigma, double t) {
  return must(pricing::garman_kohlhagen_greeks(make_market(s, k, rd, rf, t, sigma), type));
}

}  // namespace fxval::testing
