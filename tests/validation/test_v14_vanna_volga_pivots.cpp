// V-14: Vanna-Volga reproduces the three pivot prices.
//
// By construction the hedge of a pivot contract is that pivot itself (weights 1, 0, 0), so each of
// the three pivots must come back at its MARKET price (the Garman-Kohlhagen price at the pivot's
// own quoted volatility) for every variant. Criterion fixed in advance: 1e-11 absolute, on 6
// scenario slices x 3 pivots x 3 variants.
// CONTROL: a non-pivot vanilla (strike at 95% and 105% of the forward) priced by Vanna-Volga must
// differ from the price at the smile's own volatility (the slice polynomial) by more than 1e-8:
// the test would otherwise be passing because VV returns the smile price everywhere. The
// difference is reported (it is the interpolation error of VV against the quadratic smile, not an
// error of either model).

#include <gtest/gtest.h>

#include <cmath>

#include "fxval/exotics/vanna_volga.hpp"
#include "support/barrier_test_support.hpp"
#include "support/smile_test_support.hpp"

namespace {

using namespace fxval;
using exotics::PlainVanilla;
using exotics::VannaVolga;
using exotics::VannaVolgaVariant;
using fxval::testing::must;

std::vector<smile::SmileSlice> slices() {
  return {fxval::testing::make_slice(1.10, 0.04, 0.025, 0.25, 0.071, -0.0055, 0.0022),
          fxval::testing::make_slice(1.10, 0.04, 0.025, 1.0, 0.077, -0.0075, 0.0028),
          fxval::testing::make_slice(1.10, 0.04, 0.025, 0.5, 0.125, -0.022, 0.009),
          fxval::testing::make_slice(4.30, 0.055, 0.025, 0.5, 0.105, 0.019, 0.008),
          fxval::testing::make_slice(1.10, 0.04, 0.025, 1.0, 0.10, -0.08, 0.003),
          fxval::testing::make_slice(1.10, 0.04, 0.025, 0.08, 0.068, -0.0045, 0.0017)};
}

TEST(V14_VannaVolgaPivots, ThreePivotsAreReproducedAtTheirMarketPrice) {
  int checked = 0;
  for (const auto& slice : slices()) {
    const VannaVolga vv = must(VannaVolga::create(slice));
    const auto m = vv.market();
    const auto& ks = slice.pivot_strikes();
    const auto& vs = slice.pivot_volatilities();
    const PlainVanilla pivots[3] = {{pricing::OptionType::put, must(Strike::from_value(ks.put))},
                                    {pricing::OptionType::call, must(Strike::from_value(ks.atm))},
                                    {pricing::OptionType::call, must(Strike::from_value(ks.call))}};
    const Volatility quoted[3] = {vs.put, vs.atm, vs.call};
    for (int i = 0; i < 3; ++i) {
      const double market_price = must(exotics::flat_volatility_price(m, quoted[i], pivots[i]));
      for (auto variant : {VannaVolgaVariant::unweighted,
                           VannaVolgaVariant::survival,
                           VannaVolgaVariant::vanilla_less_knock_in}) {
        const auto r = must(vv.price(pivots[i], variant));
        EXPECT_NEAR(r.price, market_price, 1e-11) << "pivot " << i;
        ++checked;
      }
    }
  }
  EXPECT_EQ(checked, 54);
}

TEST(V14_VannaVolgaPivots, ControlNonPivotVanillaIsNotReproducedExactly) {
  for (const auto& slice : slices()) {
    const VannaVolga vv = must(VannaVolga::create(slice));
    for (double moneyness : {0.95, 1.05}) {
      const Strike k = must(Strike::from_value(slice.forward() * moneyness));
      const PlainVanilla v{moneyness < 1.0 ? pricing::OptionType::put : pricing::OptionType::call,
                           k};
      const double smile_price =
          must(exotics::flat_volatility_price(vv.market(), must(slice.vol_at_strike(k)), v));
      const double vv_price = must(vv.price(v, VannaVolgaVariant::unweighted)).price;
      RecordProperty("vv_minus_smile_" + std::to_string(moneyness),
                     std::to_string(vv_price - smile_price));
      EXPECT_GT(std::abs(vv_price - smile_price), 1e-8);
    }
  }
}

}  // namespace
