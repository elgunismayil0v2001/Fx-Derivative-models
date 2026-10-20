#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <utility>

#include "fxval/pricing/implied_volatility.hpp"
#include "support/gk_test_support.hpp"

namespace {

using fxval::ErrorCode;
using fxval::pricing::garman_kohlhagen_implied_volatility;
using fxval::pricing::OptionType;
using fxval::pricing::VanillaMarket;
using fxval::testing::gk_all;
using fxval::testing::gk_value;
using fxval::testing::must;

VanillaMarket market(double s, double k, double rd, double rf, double t) {
  return VanillaMarket{must(fxval::Spot::from_value(s)),
                       must(fxval::Strike::from_value(k)),
                       must(fxval::Rate::from_decimal(rd)),
                       must(fxval::Rate::from_decimal(rf)),
                       must(fxval::Years::from_years(t))};
}

TEST(ImpliedVolatilityTest, RoundTripsPricesAcrossStrikesExpiriesAndTypes) {
  int checked = 0;
  for (double sigma : {0.02, 0.08, 0.2, 0.6, 1.5}) {
    for (double t : {0.02, 0.25, 1.0, 5.0}) {
      for (double moneyness : {0.6, 0.85, 1.0, 1.2, 1.7}) {
        for (OptionType type : {OptionType::call, OptionType::put}) {
          const double s = 1.1;
          const double k = s * moneyness;
          const double price = gk_value(type, s, k, 0.04, 0.025, sigma, t);
          const auto m = market(s, k, 0.04, 0.025, t);
          // Where the option has no volatility information left (deep ITM and tiny vega) the
          // inversion legitimately fails; a failure is acceptable only for a non-positive OTM
          // price.
          const auto implied = garman_kohlhagen_implied_volatility(m, type, price);
          if (!implied) {
            continue;
          }
          ++checked;
          const double vega =
              std::max(gk_all(type == OptionType::call ? OptionType::put : OptionType::call,
                              s,
                              k,
                              0.04,
                              0.025,
                              sigma,
                              t)
                           .vega,
                       1e-300);
          // Disclosure: with a flat 1e-9 bound this failed at sigma = 2.5, t = 5, three standard
          // deviations out, where vega is about 1e-5: a price rounded to 1e-16 cannot give the
          // volatility more precisely than 1e-16 / vega. The bound below is that limit.
          const double bound = 1e-12 + 1e-13 * std::max(1.0, price) / vega;
          EXPECT_NEAR(implied.value().value(), sigma, bound)
              << "sigma=" << sigma << " t=" << t << " m=" << moneyness;
          // The inverse is exact in the price whatever the volatility error.
          const double repriced = gk_value(type, s, k, 0.04, 0.025, implied.value().value(), t);
          EXPECT_NEAR(repriced, price, 1e-14);
        }
      }
    }
  }
  EXPECT_GT(checked, 150);
}

TEST(ImpliedVolatilityTest, InTheMoneyPricesAreInvertedThroughTheOtmOption) {
  // Deep ITM call: the call price is almost pure intrinsic value; going through the put keeps
  // the volatility to full precision.
  const double s = 1.1;
  const double k = 0.7;
  const double price = gk_value(OptionType::call, s, k, 0.04, 0.025, 0.1, 0.5);
  const auto implied =
      garman_kohlhagen_implied_volatility(market(s, k, 0.04, 0.025, 0.5), OptionType::call, price);
  ASSERT_TRUE(implied.has_value());
  EXPECT_NEAR(implied.value().value(), 0.1, 1e-6);
}

TEST(ImpliedVolatilityTest, CallAndPutOfTheSameStrikeGiveTheSameVolatility) {
  const auto m = market(1.1, 1.25, 0.04, 0.025, 1.0);
  const double call = gk_value(OptionType::call, 1.1, 1.25, 0.04, 0.025, 0.12, 1.0);
  const double put = gk_value(OptionType::put, 1.1, 1.25, 0.04, 0.025, 0.12, 1.0);
  EXPECT_NEAR(garman_kohlhagen_implied_volatility(m, OptionType::call, call).value().value(),
              garman_kohlhagen_implied_volatility(m, OptionType::put, put).value().value(),
              1e-12);
}

TEST(ImpliedVolatilityTest, RejectsPricesThatViolateNoArbitrageBounds) {
  const auto m = market(1.1, 1.1, 0.04, 0.025, 1.0);
  const double forward_value = 1.1 * std::exp(-0.025);
  const double strike_value = 1.1 * std::exp(-0.04);
  const double intrinsic = std::max(forward_value - strike_value, 0.0);

  const auto below = garman_kohlhagen_implied_volatility(m, OptionType::call, intrinsic - 1e-6);
  ASSERT_FALSE(below.has_value());
  EXPECT_EQ(below.error().code, ErrorCode::invalid_argument);
  // Above the upper bound: the error must be the no-arbitrage one (invalid_argument), not a later
  // failure of the search.
  for (const auto& [type, bound] :
       {std::pair{OptionType::call, forward_value}, std::pair{OptionType::put, strike_value}}) {
    const auto above = garman_kohlhagen_implied_volatility(m, type, bound + 1e-6);
    ASSERT_FALSE(above.has_value());
    EXPECT_EQ(above.error().code, ErrorCode::invalid_argument);
  }
  EXPECT_FALSE(garman_kohlhagen_implied_volatility(m, OptionType::call, std::nan("")).has_value());
  EXPECT_FALSE(garman_kohlhagen_implied_volatility(m, OptionType::call, -0.01).has_value());
}

TEST(ImpliedVolatilityTest, RejectsZeroExpiry) {
  const auto result = garman_kohlhagen_implied_volatility(
      market(1.1, 1.1, 0.04, 0.025, 0.0), OptionType::call, 0.01);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::invalid_argument);
}

TEST(ImpliedVolatilityTest, ReportsAVolatilityOutsideTheSearchRange) {
  // A price at the very top of the no-arbitrage range needs a volatility above 500%.
  const auto m = market(1.1, 1.1, 0.04, 0.025, 1.0);
  const double almost_forward = 1.1 * std::exp(-0.025) * (1.0 - 1e-12);
  const auto too_high = garman_kohlhagen_implied_volatility(m, OptionType::call, almost_forward);
  ASSERT_FALSE(too_high.has_value());
  EXPECT_EQ(too_high.error().code, ErrorCode::numerical_failure);
  // An OTM price of zero means volatility below the range.
  const auto m_otm = market(1.1, 1.5, 0.04, 0.025, 1.0);
  const auto zero = garman_kohlhagen_implied_volatility(m_otm, OptionType::call, 0.0);
  ASSERT_FALSE(zero.has_value());
  EXPECT_EQ(zero.error().code, ErrorCode::numerical_failure);
}

}  // namespace
