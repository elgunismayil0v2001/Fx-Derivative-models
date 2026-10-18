#include <gtest/gtest.h>

#include <cmath>

#include "fxval/smile/quotes.hpp"
#include "support/smile_test_support.hpp"

namespace {

using fxval::ErrorCode;
using fxval::smile::pivot_volatilities;
using fxval::smile::quotes_from_pivots;
using fxval::smile::VolSpread;
using fxval::testing::make_quotes;
using fxval::testing::must;

TEST(QuotesTest, WingVolatilitiesFollowTheDefinitionsOfRiskReversalAndButterfly) {
  // RR = sigma_c - sigma_p = -0.01, BF = (sigma_c + sigma_p)/2 - atm = 0.003.
  const auto pivots = must(pivot_volatilities(make_quotes(0.09, -0.01, 0.003)));
  EXPECT_NEAR(pivots.call.value(), 0.09 + 0.003 - 0.005, 1e-17);
  EXPECT_NEAR(pivots.put.value(), 0.09 + 0.003 + 0.005, 1e-17);
  EXPECT_EQ(pivots.atm.value(), 0.09);
}

TEST(QuotesTest, QuotesFromPivotsIsTheInverse) {
  const auto quotes = make_quotes(0.12, 0.02, 0.006, 0.10);
  const auto back = quotes_from_pivots(must(pivot_volatilities(quotes)), 0.10);
  EXPECT_NEAR(back.atm.value(), 0.12, 1e-17);
  EXPECT_NEAR(back.risk_reversal.value(), 0.02, 1e-16);
  EXPECT_NEAR(back.butterfly.value(), 0.006, 1e-16);
  EXPECT_EQ(back.delta, 0.10);
}

TEST(QuotesTest, NonPositiveWingVolatilityIsRejectedAndNamed) {
  const auto call_bad =
      pivot_volatilities(make_quotes(0.05, 0.20, 0.0));  // call = 0.15, put = -0.05
  ASSERT_FALSE(call_bad.has_value());
  EXPECT_EQ(call_bad.error().code, ErrorCode::invalid_argument);
  EXPECT_NE(call_bad.error().message.find("put"), std::string::npos);

  const auto put_bad = pivot_volatilities(make_quotes(0.05, -0.20, 0.0));
  ASSERT_FALSE(put_bad.has_value());
  EXPECT_NE(put_bad.error().message.find("call"), std::string::npos);

  EXPECT_FALSE(pivot_volatilities(make_quotes(0.05, 0.0, -0.05)).has_value());  // both wings zero
}

TEST(QuotesTest, QuoteDeltaMustBeAnOutOfTheMoneyDelta) {
  for (double delta : {0.0, 0.5, 0.75, -0.25, std::nan("")}) {
    const auto r = pivot_volatilities(make_quotes(0.09, 0.0, 0.0, delta));
    ASSERT_FALSE(r.has_value()) << delta;
    EXPECT_EQ(r.error().code, ErrorCode::invalid_argument);
  }
}

TEST(QuotesTest, VolSpreadIsADecimalAndMayBeNegative) {
  EXPECT_EQ(must(VolSpread::from_decimal(-0.01)).value(), -0.01);
  EXPECT_NEAR(must(VolSpread::from_percent(-1.0)).value(), -0.01, 1e-18);
  for (double bad : {5.5, -5.5, std::nan(""), HUGE_VAL}) {
    EXPECT_FALSE(VolSpread::from_decimal(bad).has_value()) << bad;
  }
  EXPECT_FALSE(VolSpread::from_percent(std::nan("")).has_value());
}

}  // namespace
