#include <gtest/gtest.h>

#include <cmath>

#include "fxval/core/limits.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"

namespace {

using fxval::Rate;
using fxval::Spot;
using fxval::Strike;
using fxval::Volatility;
using fxval::Years;
using fxval::pricing::garman_kohlhagen_greeks;
using fxval::pricing::garman_kohlhagen_price;
using fxval::pricing::OptionType;
using fxval::pricing::VanillaFx;

VanillaFx market(
    double spot, double strike, double domestic, double foreign, double years, double vol) {
  return VanillaFx{
      Spot::from_value(spot).value(),
      Strike::from_value(strike).value(),
      Rate::from_decimal(domestic).value(),
      Rate::from_decimal(foreign).value(),
      Years::from_years(years).value(),
      Volatility::from_decimal(vol).value(),
  };
}

TEST(GarmanKohlhagen, AtExpiryEqualsIntrinsic) {
  const auto m = market(1.20, 1.10, 0.03, 0.01, 0.0, 0.12);
  const auto call = garman_kohlhagen_price(m, OptionType::call);
  const auto put = garman_kohlhagen_price(m, OptionType::put);
  ASSERT_TRUE(call.has_value());
  ASSERT_TRUE(put.has_value());
  EXPECT_NEAR(call.value(), 0.10, 1e-15);
  EXPECT_NEAR(put.value(), 0.0, 1e-15);
}

TEST(GarmanKohlhagen, ZeroVolUsesDiscountedForwardIntrinsic) {
  const auto m = market(1.00, 1.00, 0.05, 0.01, 1.0, 0.0);
  const double fwd = 1.00 * std::exp(0.04);
  const auto call = garman_kohlhagen_price(m, OptionType::call);
  ASSERT_TRUE(call.has_value());
  EXPECT_NEAR(call.value(), std::exp(-0.05) * (fwd - 1.00), 1e-12);
}

TEST(GarmanKohlhagen, PutCallParityHoldsOnATypicalFxPoint) {
  const auto m = market(4.35, 4.40, 0.05, 0.03, 0.5, 0.11);
  const auto call = garman_kohlhagen_price(m, OptionType::call);
  const auto put = garman_kohlhagen_price(m, OptionType::put);
  ASSERT_TRUE(call.has_value());
  ASSERT_TRUE(put.has_value());
  const double s = m.spot.value();
  const double k = m.strike.value();
  const double t = m.expiry.value();
  const double rhs = s * std::exp(-m.foreign.value() * t) - k * std::exp(-m.domestic.value() * t);
  EXPECT_NEAR(call.value() - put.value(), rhs, fxval::tol::put_call_parity);
}

TEST(GarmanKohlhagen, CallDeltaMinusPutDeltaIsForeignDiscount) {
  const auto m = market(1.10, 1.05, 0.04, 0.02, 1.25, 0.09);
  const auto call_g = garman_kohlhagen_greeks(m, OptionType::call);
  const auto put_g = garman_kohlhagen_greeks(m, OptionType::put);
  ASSERT_TRUE(call_g.has_value());
  ASSERT_TRUE(put_g.has_value());
  const double df_f = std::exp(-m.foreign.value() * m.expiry.value());
  EXPECT_NEAR(call_g.value().spot_delta - put_g.value().spot_delta, df_f, 1e-12);
  EXPECT_NEAR(call_g.value().vega, put_g.value().vega, 1e-12);
  EXPECT_NEAR(call_g.value().gamma, put_g.value().gamma, 1e-12);
}

TEST(GarmanKohlhagen, DeepInTheMoneyCallDeltaApproachesForeignDiscount) {
  const auto m = market(2.00, 1.00, 0.03, 0.01, 1.0, 0.08);
  const auto g = garman_kohlhagen_greeks(m, OptionType::call);
  ASSERT_TRUE(g.has_value());
  EXPECT_NEAR(g.value().spot_delta, std::exp(-0.01), 1e-4);
}

}  // namespace
