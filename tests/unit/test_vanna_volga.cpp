// Unit tests of the Vanna-Volga module: flat smile, pivot weights, variants, error paths.

#include <gtest/gtest.h>

#include <cmath>

#include "fxval/exotics/vanna_volga.hpp"
#include "support/barrier_test_support.hpp"
#include "support/smile_test_support.hpp"

namespace {

using namespace fxval;
using exotics::ExoticContract;
using exotics::PlainVanilla;
using exotics::VannaVolga;
using exotics::VannaVolgaVariant;
using fxval::testing::must;

smile::SmileSlice skewed() {
  return fxval::testing::make_slice(1.10, 0.04, 0.025, 0.5, 0.08, -0.01, 0.003);
}
smile::SmileSlice flat() {
  return fxval::testing::make_slice(1.10, 0.04, 0.025, 0.5, 0.08, 0.0, 0.0);
}

ExoticContract down_out_call() {
  return fxval::testing::make_barrier_vanilla(pricing::OptionType::call,
                                              1.10,
                                              1.00,
                                              pricing::BarrierDirection::down,
                                              pricing::BarrierEffect::knock_out);
}

TEST(VannaVolgaTest, FlatSmileGivesTheFlatVolatilityPriceForEveryVariant) {
  const VannaVolga vv = must(VannaVolga::create(flat()));
  for (auto variant : {VannaVolgaVariant::unweighted,
                       VannaVolgaVariant::survival,
                       VannaVolgaVariant::vanilla_less_knock_in}) {
    const auto r = must(vv.price(down_out_call(), variant));
    EXPECT_NEAR(r.price, r.flat_vol_price, 1e-12);
  }
}

TEST(VannaVolgaTest, AVanillaPivotGetsItsOwnUnitWeight) {
  const auto slice = skewed();
  const VannaVolga vv = must(VannaVolga::create(slice));
  const auto k_atm = must(Strike::from_value(slice.pivot_strikes().atm));
  const auto r =
      must(vv.price(PlainVanilla{pricing::OptionType::call, k_atm}, VannaVolgaVariant::unweighted));
  EXPECT_NEAR(r.weights[0], 0.0, 1e-9);
  EXPECT_NEAR(r.weights[1], 1.0, 1e-9);
  EXPECT_NEAR(r.weights[2], 0.0, 1e-9);
}

TEST(VannaVolgaTest, SurvivalVariantIsBetweenFlatAndUnweightedForAKnockOut) {
  const VannaVolga vv = must(VannaVolga::create(skewed()));
  const auto un = must(vv.price(down_out_call(), VannaVolgaVariant::unweighted));
  const auto sv = must(vv.price(down_out_call(), VannaVolgaVariant::survival));
  EXPECT_GT(sv.weight_factor, 0.0);
  EXPECT_LT(sv.weight_factor, 1.0);
  EXPECT_NEAR(
      sv.price - sv.flat_vol_price, sv.weight_factor * (un.price - un.flat_vol_price), 1e-12);
}

TEST(VannaVolgaTest,
     SurvivalFactorIsTheNoTouchProbabilityForAKnockOutAndTheTouchProbabilityForAKnockIn) {
  const VannaVolga vv = must(VannaVolga::create(skewed()));
  const auto level = fxval::testing::level(1.00);
  const double touch = must(pricing::touch_probability(
      vv.market(), vv.flat_volatility(), level, pricing::BarrierDirection::down));
  const auto out = must(vv.price(down_out_call(), VannaVolgaVariant::survival));
  EXPECT_NEAR(out.weight_factor, 1.0 - touch, 1e-14);
  const ExoticContract knock_in =
      fxval::testing::make_barrier_vanilla(pricing::OptionType::call,
                                           1.10,
                                           1.00,
                                           pricing::BarrierDirection::down,
                                           pricing::BarrierEffect::knock_in);
  EXPECT_NEAR(must(vv.price(knock_in, VannaVolgaVariant::survival)).weight_factor, touch, 1e-14);
}

TEST(VannaVolgaTest, DecompositionIsNotDefinedForDoubleNoTouch) {
  const VannaVolga vv = must(VannaVolga::create(skewed()));
  const ExoticContract dnt =
      pricing::DoubleNoTouchContract{fxval::testing::level(1.0), fxval::testing::level(1.2)};
  const auto r = vv.price(dnt, VannaVolgaVariant::vanilla_less_knock_in);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::invalid_argument);
  EXPECT_TRUE(vv.price(dnt, VannaVolgaVariant::survival).has_value());
}

TEST(VannaVolgaTest, BreachedBarrierIsAnError) {
  const VannaVolga vv = must(VannaVolga::create(skewed()));
  const ExoticContract breached =
      fxval::testing::make_barrier_vanilla(pricing::OptionType::call,
                                           1.10,
                                           1.20,
                                           pricing::BarrierDirection::down,
                                           pricing::BarrierEffect::knock_out);
  EXPECT_FALSE(vv.price(breached, VannaVolgaVariant::survival).has_value());
}

TEST(VannaVolgaTest, GreeksWithTheBarrierVeryCloseToTheSpotDoNotStepAcrossIt) {
  // Barrier 0.05% below the spot: the default spot step (0.1%) would cross it.
  const auto m = fxval::testing::make_fx_market(1.10, 0.04, 0.025, 0.5);
  const ExoticContract close =
      fxval::testing::make_barrier_vanilla(pricing::OptionType::call,
                                           1.10,
                                           1.0995,
                                           pricing::BarrierDirection::down,
                                           pricing::BarrierEffect::knock_out);
  const auto g = exotics::flat_volatility_greeks(m, fxval::testing::vol(0.08), close);
  ASSERT_TRUE(g.has_value());
  EXPECT_TRUE(std::isfinite(g.value().vanna));
}

TEST(VannaVolgaTest, GreeksOfAVanillaMatchGarmanKohlhagen) {
  const auto m = fxval::testing::make_fx_market(1.10, 0.04, 0.025, 0.5);
  const auto k = must(Strike::from_value(1.12));
  const auto g = must(exotics::flat_volatility_greeks(
      m, fxval::testing::vol(0.08), PlainVanilla{pricing::OptionType::call, k}));
  const auto ref = must(pricing::garman_kohlhagen_greeks(
      pricing::VanillaFx{m.spot, k, m.domestic, m.foreign, m.expiry, fxval::testing::vol(0.08)},
      pricing::OptionType::call));
  EXPECT_NEAR(g.vega, ref.vega, 1e-5 * std::abs(ref.vega));
  EXPECT_NEAR(g.vanna, ref.vanna, 1e-3 * std::abs(ref.vanna));
  EXPECT_NEAR(g.volga, ref.volga, 1e-3 * std::abs(ref.volga));
}

}  // namespace
