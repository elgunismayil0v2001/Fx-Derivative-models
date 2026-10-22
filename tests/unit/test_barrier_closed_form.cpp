// Unit tests of the barrier closed forms: limits, parity, monotonicity and error paths.
// (Agreement with an independent implementation is BARRIER-REF; in-out parity as a validation
// test is V-13.)

#include <gtest/gtest.h>

#include <cmath>

#include "support/barrier_test_support.hpp"

namespace {

using fxval::pricing::BarrierDirection;
using fxval::pricing::BarrierEffect;
using fxval::pricing::OptionType;
using fxval::testing::barrier_price;
using fxval::testing::level;
using fxval::testing::make_barrier_vanilla;
using fxval::testing::make_fx_market;
using fxval::testing::must;
using fxval::testing::vol;

constexpr double kRd = 0.03, kRf = 0.01, kT = 0.5, kSigma = 0.10;

TEST(BarrierClosedForm, FarBarrierKnockOutEqualsVanillaAndKnockInVanishes) {
  const auto m = make_fx_market(1.10, kRd, kRf, kT);
  const double vanilla = must(fxval::pricing::garman_kohlhagen_price(
      fxval::pricing::VanillaFx{m.spot,
                                must(fxval::Strike::from_value(1.10)),
                                m.domestic,
                                m.foreign,
                                m.expiry,
                                vol(kSigma)},
      OptionType::call));
  const auto ko = make_barrier_vanilla(
      OptionType::call, 1.10, 0.30, BarrierDirection::down, BarrierEffect::knock_out);
  auto ki = ko;
  ki.effect = BarrierEffect::knock_in;
  EXPECT_NEAR(barrier_price(m, kSigma, ko), vanilla, 1e-12);
  EXPECT_NEAR(barrier_price(m, kSigma, ki), 0.0, 1e-12);
}

TEST(BarrierClosedForm, KnockOutDecreasesAsBarrierApproachesSpot) {
  const auto m = make_fx_market(1.10, kRd, kRf, kT);
  double previous = 1e9;
  for (double b : {0.80, 0.90, 1.00, 1.05, 1.09}) {
    const double p = barrier_price(
        m,
        kSigma,
        make_barrier_vanilla(
            OptionType::call, 1.10, b, BarrierDirection::down, BarrierEffect::knock_out));
    EXPECT_LT(p, previous);
    EXPECT_GE(p, 0.0);
    previous = p;
  }
}

TEST(BarrierClosedForm, TouchProbabilityLimits) {
  const auto m = make_fx_market(1.10, kRd, kRf, kT);
  EXPECT_NEAR(
      must(fxval::pricing::touch_probability(m, vol(kSigma), level(0.2), BarrierDirection::down)),
      0.0,
      1e-12);
  EXPECT_NEAR(must(fxval::pricing::touch_probability(
                  m, vol(kSigma), level(1.1 - 1e-9), BarrierDirection::down)),
              1.0,
              1e-6);
}

TEST(BarrierClosedForm, OneTouchPlusNoTouchIsTheDiscountFactor) {
  const auto m = make_fx_market(1.10, kRd, kRf, kT);
  for (auto dir : {BarrierDirection::down, BarrierDirection::up}) {
    const double b = dir == BarrierDirection::down ? 1.02 : 1.18;
    const double sum = must(fxval::pricing::one_touch_price(m, vol(kSigma), level(b), dir)) +
                       must(fxval::pricing::no_touch_price(m, vol(kSigma), level(b), dir));
    EXPECT_NEAR(sum, std::exp(-kRd * kT), 1e-13);
  }
}

TEST(BarrierClosedForm, DoubleNoTouchApproachesNoTouchWhenOneBarrierIsFar) {
  const auto m = make_fx_market(1.10, kRd, kRf, kT);
  const double dnt =
      must(fxval::pricing::double_no_touch_price(m, vol(kSigma), level(1.00), level(5.0)));
  const double single =
      must(fxval::pricing::no_touch_price(m, vol(kSigma), level(1.00), BarrierDirection::down));
  EXPECT_NEAR(dnt, single, 1e-9);
}

TEST(BarrierClosedForm, DoubleNoTouchIsAtMostEitherSingleNoTouch) {
  const auto m = make_fx_market(1.10, kRd, kRf, kT);
  const double dnt =
      must(fxval::pricing::double_no_touch_price(m, vol(kSigma), level(1.02), level(1.18)));
  EXPECT_LT(
      dnt,
      must(fxval::pricing::no_touch_price(m, vol(kSigma), level(1.02), BarrierDirection::down)));
  EXPECT_LT(
      dnt, must(fxval::pricing::no_touch_price(m, vol(kSigma), level(1.18), BarrierDirection::up)));
  EXPECT_GT(dnt, 0.0);
}

TEST(BarrierClosedForm, BreachedBarrierIsAnError) {
  const auto m = make_fx_market(1.10, kRd, kRf, kT);
  const auto down = fxval::pricing::barrier_vanilla_price(
      m,
      vol(kSigma),
      make_barrier_vanilla(
          OptionType::call, 1.10, 1.12, BarrierDirection::down, BarrierEffect::knock_out));
  ASSERT_FALSE(down.has_value());
  EXPECT_EQ(down.error().code, fxval::ErrorCode::invalid_argument);
  const auto up =
      fxval::pricing::one_touch_price(m, vol(kSigma), level(1.05), BarrierDirection::up);
  EXPECT_FALSE(up.has_value());
  const auto dnt = fxval::pricing::double_no_touch_price(m, vol(kSigma), level(1.12), level(1.20));
  EXPECT_FALSE(dnt.has_value());
  const auto swapped =
      fxval::pricing::double_no_touch_price(m, vol(kSigma), level(1.20), level(1.00));
  EXPECT_FALSE(swapped.has_value());
}

TEST(BarrierClosedForm, NonPositiveBarrierLevelIsRejected) {
  EXPECT_FALSE(fxval::pricing::BarrierLevel::from_value(0.0).has_value());
  EXPECT_FALSE(fxval::pricing::BarrierLevel::from_value(-1.0).has_value());
}

// F-22: with a total volatility so small that 2 mu / sigma^2 overflows, the closed-form blocks used
// to return finite but meaningless prices (found by the fuzz replay). They are now refused.
TEST(BarrierClosedForm, RefusesVolatilityTooSmallForTheClosedForm) {
  const auto m = make_fx_market(1.10, kRd, kRf, kT);
  const auto ko = make_barrier_vanilla(
      OptionType::put, 1.10, 2.0, BarrierDirection::up, BarrierEffect::knock_out);
  for (const double sigma : {1e-12, 1e-100, 1e-277}) {
    const auto r = fxval::pricing::barrier_vanilla_price(m, vol(sigma), ko);
    ASSERT_FALSE(r.has_value()) << sigma;
    EXPECT_EQ(r.error().code, fxval::ErrorCode::invalid_argument);
  }
  // Just above the floor the price is accepted and still obeys in-out parity.
  auto ki = ko;
  ki.effect = BarrierEffect::knock_in;
  const double sigma = 0.05;
  const double sum = barrier_price(m, sigma, ko) + barrier_price(m, sigma, ki);
  const double plain = must(fxval::pricing::garman_kohlhagen_price(
      fxval::pricing::VanillaFx{m.spot,
                                must(fxval::Strike::from_value(1.10)),
                                m.domestic,
                                m.foreign,
                                m.expiry,
                                vol(sigma)},
      OptionType::put));
  EXPECT_NEAR(sum, plain, 1e-9);
}

}  // namespace
