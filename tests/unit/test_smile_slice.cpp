#include <gtest/gtest.h>

#include <cmath>

#include "support/smile_test_support.hpp"

namespace {

using fxval::ErrorCode;
using fxval::smile::DeltaConvention;
using fxval::testing::make_quotes;
using fxval::testing::make_slice;
using fxval::testing::make_smile_market;
using fxval::testing::must;

TEST(SmileSliceTest, PassesThroughTheThreePivotsExactly) {
  for (auto convention : {DeltaConvention::spot, DeltaConvention::forward}) {
    const auto slice = make_slice(1.1, 0.04, 0.025, 0.5, 0.09, -0.012, 0.004, 0.25, convention);
    const auto& strikes = slice.pivot_strikes();
    const auto& vols = slice.pivot_volatilities();
    EXPECT_NEAR(must(slice.vol_at_strike(must(fxval::Strike::from_value(strikes.put)))).value(),
                vols.put.value(),
                1e-15);
    EXPECT_NEAR(must(slice.vol_at_strike(must(fxval::Strike::from_value(strikes.atm)))).value(),
                vols.atm.value(),
                1e-15);
    EXPECT_NEAR(must(slice.vol_at_strike(must(fxval::Strike::from_value(strikes.call)))).value(),
                vols.call.value(),
                1e-15);
  }
}

TEST(SmileSliceTest, FlatQuotesGiveAFlatSmile) {
  const auto slice = make_slice(1.1, 0.04, 0.025, 1.0, 0.08, 0.0, 0.0);
  for (double k : {-0.5, -0.1, 0.0, 0.2, 0.9}) {
    EXPECT_NEAR(slice.point(k).vol, 0.08, 1e-14);
    EXPECT_NEAR(slice.point(k).dvol_dk, 0.0, 1e-13);
    EXPECT_NEAR(slice.point(k).d2vol_dk2, 0.0, 1e-12);
  }
}

TEST(SmileSliceTest, DerivativesAreThoseOfTheQuadratic) {
  const auto slice = make_slice(1.1, 0.04, 0.025, 1.0, 0.09, -0.012, 0.004);
  const double h = 1e-5;
  for (double k : {-0.2, 0.0, 0.15}) {
    const auto p = slice.point(k);
    const auto up = slice.point(k + h);
    const auto down = slice.point(k - h);
    EXPECT_NEAR(p.dvol_dk, (up.vol - down.vol) / (2 * h), 1e-8);
    EXPECT_NEAR(p.d2vol_dk2, (up.dvol_dk - down.dvol_dk) / (2 * h), 1e-8);
  }
}

TEST(SmileSliceTest, NegativeRiskReversalMakesPutsRicherThanCalls) {
  const auto slice = make_slice(1.1, 0.04, 0.025, 0.5, 0.09, -0.012, 0.004);
  EXPECT_GT(slice.pivot_volatilities().put.value(), slice.pivot_volatilities().call.value());
  EXPECT_LT(slice.pivot_strikes().put, slice.pivot_strikes().atm);
  EXPECT_LT(slice.pivot_strikes().atm, slice.pivot_strikes().call);
}

TEST(SmileSliceTest, AccessorsReturnWhatWasPassedIn) {
  const auto slice =
      make_slice(1.1, 0.04, 0.025, 0.5, 0.09, -0.012, 0.004, 0.10, DeltaConvention::forward);
  EXPECT_EQ(slice.convention(), DeltaConvention::forward);
  EXPECT_EQ(slice.quotes().delta, 0.10);
  EXPECT_EQ(slice.expiry().value(), 0.5);
  EXPECT_NEAR(slice.forward(), 1.1 * std::exp(0.015 * 0.5), 1e-15);
  EXPECT_NEAR(slice.atm_scale(), 0.09 * std::sqrt(0.5), 1e-16);
}

TEST(SmileSliceTest, PolynomialThatLeavesThePositiveRealsIsReportedNotReturned) {
  // A strongly concave smile (negative butterfly) goes negative far from the money.
  const auto slice = make_slice(1.1, 0.04, 0.025, 1.0, 0.10, 0.0, -0.04);
  EXPECT_TRUE(slice.vol_at_log_moneyness(0.0).has_value());
  const auto far = slice.vol_at_log_moneyness(2.0);
  ASSERT_FALSE(far.has_value());
  EXPECT_EQ(far.error().code, ErrorCode::numerical_failure);
  EXPECT_FALSE(slice.vol_at_log_moneyness(std::nan("")).has_value());
  // Too steep the other way: exceeds the volatility limit instead.
  const auto steep = make_slice(1.1, 0.04, 0.025, 1.0, 0.10, 0.0, 0.04);
  EXPECT_FALSE(steep.vol_at_log_moneyness(40.0).has_value());
}

TEST(SmileSliceTest, ErrorsFromQuotesAndMarketPropagate) {
  const auto market = make_smile_market(1.1, 0.04, 0.025, 1.0);
  EXPECT_FALSE(fxval::smile::SmileSlice::from_quotes(
                   market, make_quotes(0.05, 0.3, 0.0), DeltaConvention::spot)
                   .has_value());
  EXPECT_FALSE(fxval::smile::SmileSlice::from_quotes(make_smile_market(1.1, 0.04, 0.025, 0.0),
                                                     make_quotes(0.08, 0.0, 0.0),
                                                     DeltaConvention::spot)
                   .has_value());
}

TEST(SmileSliceTest, InconsistentRateAndDeltaGiveOrderedStrikesError) {
  // r_f T = 0.9: a 25-delta SPOT put is a forward delta of 0.615, which is in the money, so the
  // "put strike" lands above the ATM strike. The slice must refuse rather than interpolate.
  const auto result = fxval::smile::SmileSlice::from_quotes(
      make_smile_market(1.1, 0.0, 0.09, 10.0), make_quotes(0.10, 0.0, 0.0), DeltaConvention::spot);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::numerical_failure);
  EXPECT_NE(result.error().message.find("increasing"), std::string::npos);
}

}  // namespace
