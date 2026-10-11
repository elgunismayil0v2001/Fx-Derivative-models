// Unit tests for the Garman-Kohlhagen edge cases: exactly known values, degenerate inputs and
// every error path. They protect the code from regressions. They are not validation evidence
// about the model (that is tests/validation/).

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "fxval/pricing/garman_kohlhagen.hpp"
#include "support/gk_test_support.hpp"

namespace {

using fxval::ErrorCode;
using fxval::pricing::garman_kohlhagen_greeks;
using fxval::pricing::garman_kohlhagen_price;
using fxval::pricing::OptionType;
using fxval::testing::gk_value;
using fxval::testing::make_market;

// With a zero foreign rate the formula is Black-Scholes. S = K = 100, r = 5%, sigma = 20%, T = 1
// is the standard textbook example.
TEST(GarmanKohlhagenKnownValues, MatchesTheTextbookBlackScholesValues) {
  EXPECT_NEAR(gk_value(OptionType::call, 100, 100, 0.05, 0.0, 0.20, 1.0), 10.450583572185565, 1e-9);
  EXPECT_NEAR(gk_value(OptionType::put, 100, 100, 0.05, 0.0, 0.20, 1.0), 5.573526022256971, 1e-9);
}

TEST(GarmanKohlhagenKnownValues, AtTheMoneyForwardWithZeroRatesHasTheClosedFormValue) {
  // F = K and r_d = r_f = 0: price = S * (2 N(sigma sqrt(T) / 2) - 1).
  const double sigma = 0.10;
  const double n = 0.5 * std::erfc(-0.5 * sigma / std::numbers::sqrt2);
  EXPECT_NEAR(gk_value(OptionType::call, 1.0, 1.0, 0.0, 0.0, sigma, 1.0), 2.0 * n - 1.0, 1e-15);
}

TEST(GarmanKohlhagenKnownValues, CurrencyInversionSymmetry) {
  // A call on the foreign currency struck at K equals K * S times a put in the inverted quote
  // 1/S with strike 1/K and the two interest rates swapped.
  const double s = 1.12, k = 1.05, rd = 0.04, rf = 0.015, sigma = 0.09, t = 0.75;
  EXPECT_NEAR(gk_value(OptionType::call, s, k, rd, rf, sigma, t),
              k * s * gk_value(OptionType::put, 1.0 / s, 1.0 / k, rf, rd, sigma, t),
              1e-14);
}

TEST(GarmanKohlhagenDegenerate, ZeroVolatilityPutIsDiscountedIntrinsicOfTheForward) {
  const double s = 1.0, k = 1.2, rd = 0.03, rf = 0.01, t = 1.0;
  const double forward = s * std::exp((rd - rf) * t);
  EXPECT_NEAR(
      gk_value(OptionType::put, s, k, rd, rf, 0.0, t), std::exp(-rd * t) * (k - forward), 1e-15);
  EXPECT_EQ(gk_value(OptionType::call, s, k, rd, rf, 0.0, t), 0.0);
}

TEST(GarmanKohlhagenDegenerate, SmallVolatilityApproachesTheZeroVolatilityLimit) {
  EXPECT_NEAR(gk_value(OptionType::call, 1.10, 1.00, 0.04, 0.01, 1e-8, 1.0),
              gk_value(OptionType::call, 1.10, 1.00, 0.04, 0.01, 0.0, 1.0),
              1e-9);
}

TEST(GarmanKohlhagenDegenerate, PricesAreNeverNegativeDeepOutOfTheMoney) {
  EXPECT_GE(gk_value(OptionType::call, 1.0, 5.0, 0.01, 0.01, 0.05, 0.1), 0.0);
  EXPECT_GE(gk_value(OptionType::put, 5.0, 1.0, 0.01, 0.01, 0.05, 0.1), 0.0);
}

// Greeks divide by sigma * sqrt(T), so they are undefined when either is zero. Returning zeros
// would look like a valid answer (gamma at the money is not zero, it is infinite), so the
// library reports an error instead (ADR 0001).
TEST(GarmanKohlhagenErrors, GreeksAreRejectedWhenVolatilityOrExpiryIsZero) {
  for (const auto& m : {make_market(1.0, 1.0, 0.01, 0.01, 1.0, 0.0),
                        make_market(1.0, 1.0, 0.01, 0.01, 0.0, 0.1),
                        make_market(1.0, 1.2, 0.03, 0.01, 1.0, 0.0)}) {
    for (auto type : {OptionType::call, OptionType::put}) {
      const auto g = garman_kohlhagen_greeks(m, type);
      ASSERT_FALSE(g.has_value());
      EXPECT_EQ(g.error().code, ErrorCode::invalid_argument);
    }
  }
}

TEST(GarmanKohlhagenErrors, OverflowIsReportedNotReturnedAsInfinity) {
  // exp(+100) times 1e300 exceeds the largest double, although every input is a valid unit.
  const auto m = make_market(1e300, 1e300, -1.0, -1.0, 100.0, 0.1);
  const auto price = garman_kohlhagen_price(m, OptionType::call);
  ASSERT_FALSE(price.has_value());
  EXPECT_EQ(price.error().code, ErrorCode::numerical_failure);
  const auto greeks = garman_kohlhagen_greeks(m, OptionType::call);
  ASSERT_FALSE(greeks.has_value());
  EXPECT_EQ(greeks.error().code, ErrorCode::numerical_failure);
}

TEST(GarmanKohlhagenErrors, ExtremeButValidInputsStayFinite) {
  // Very short expiry with high volatility, and a very long expiry with low volatility.
  for (const auto& m : {make_market(1.0, 1.0, 0.0, 0.0, 1e-9, 5.0),
                        make_market(1.0, 1.0, 0.05, 0.0, 100.0, 0.001)}) {
    const auto g = garman_kohlhagen_greeks(m, OptionType::call);
    ASSERT_TRUE(g.has_value());
    EXPECT_TRUE(std::isfinite(g.value().volga));
    EXPECT_TRUE(std::isfinite(g.value().theta));
  }
}

}  // namespace
