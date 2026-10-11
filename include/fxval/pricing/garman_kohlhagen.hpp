#pragma once

#include <cstdint>

#include "fxval/core/units.hpp"

namespace fxval::pricing {

enum class OptionType : std::uint8_t { call, put };

// European FX vanilla under Garman-Kohlhagen (constant rates and volatility).
// Spot and strike are domestic per one unit of foreign. Time is a year fraction
// (ACT/365 fixed is the project convention; see act365_fixed).
struct VanillaFx {
  Spot spot;
  Strike strike;
  Rate domestic;
  Rate foreign;
  Years expiry;
  Volatility volatility;
};

// Analytic Greeks. Vega/vanna/volga are per unit decimal volatility (0.01 = one vol point).
// Theta is dV/dt with t calendar time (the PDE theta), per year. Rho is per unit decimal rate.
struct GarmanKohlhagenGreeks {
  double spot_delta;
  double forward_delta;
  double gamma;
  double vega;
  double vanna;
  double volga;
  double theta;
  double rho_domestic;
  double rho_foreign;
};

[[nodiscard]] Result<double> garman_kohlhagen_price(const VanillaFx& market, OptionType type);

// Greeks are not defined when volatility or expiry is zero (invalid_argument); a non-finite value
// returns numerical_failure. The price is defined there (discounted intrinsic value of the
// forward).
[[nodiscard]] Result<GarmanKohlhagenGreeks> garman_kohlhagen_greeks(const VanillaFx& market,
                                                                    OptionType type);

}  // namespace fxval::pricing
