#pragma once

// Helpers shared by the barrier tests: typed inputs from plain numbers.

#include "fxval/pricing/barrier_closed_form.hpp"
#include "support/calibration_test_support.hpp"

namespace fxval::testing {

inline pricing::FxMarket make_fx_market(double spot, double rd, double rf, double years) {
  return pricing::FxMarket{must(Spot::from_value(spot)),
                           must(Rate::from_decimal(rd)),
                           must(Rate::from_decimal(rf)),
                           must(Years::from_years(years))};
}

inline pricing::BarrierLevel level(double x) {
  return must(pricing::BarrierLevel::from_value(x));
}

inline Volatility vol(double x) {
  return must(Volatility::from_decimal(x));
}

inline pricing::BarrierVanilla make_barrier_vanilla(pricing::OptionType type,
                                                    double strike,
                                                    double barrier,
                                                    pricing::BarrierDirection direction,
                                                    pricing::BarrierEffect effect) {
  return pricing::BarrierVanilla{
      type, must(Strike::from_value(strike)), level(barrier), direction, effect};
}

inline double barrier_price(const pricing::FxMarket& market,
                            double sigma,
                            const pricing::BarrierVanilla& contract) {
  return must(pricing::barrier_vanilla_price(market, vol(sigma), contract));
}

}  // namespace fxval::testing
