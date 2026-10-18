#include <gtest/gtest.h>

#include <cmath>

#include "fxval/smile/delta_convention.hpp"
#include "support/smile_test_support.hpp"

namespace {

using fxval::ErrorCode;
using fxval::Volatility;
using fxval::pricing::OptionType;
using fxval::smile::DeltaConvention;
using fxval::testing::gk_all;
using fxval::testing::make_smile_market;
using fxval::testing::must;

Volatility vol(double v) {
  return must(Volatility::from_decimal(v));
}

TEST(DeltaConventionTest, ForwardDeltaStrikeMatchesTheClosedFormByHand) {
  // F = 1 (zero rates, spot 1), sigma = 10%, T = 1, forward call delta 25%:
  // d1 = N^-1(0.25) = -0.6744897501960817, K = exp(-sigma d1 + sigma^2 / 2).
  const auto market = make_smile_market(1.0, 0.0, 0.0, 1.0);
  const double k =
      must(strike_from_delta(market, vol(0.10), 0.25, OptionType::call, DeltaConvention::forward));
  EXPECT_NEAR(k, std::exp(0.1 * 0.6744897501960817 + 0.005), 1e-14);
  // A put of the same delta is on the other side of the forward.
  const double kp =
      must(strike_from_delta(market, vol(0.10), 0.25, OptionType::put, DeltaConvention::forward));
  EXPECT_NEAR(kp, std::exp(-0.1 * 0.6744897501960817 + 0.005), 1e-14);
}

TEST(DeltaConventionTest, AtmStrikeIsTheDeltaNeutralStraddle) {
  const auto market = make_smile_market(1.1, 0.04, 0.025, 0.5);
  const double k = must(fxval::smile::atm_strike(market, vol(0.09)));
  EXPECT_NEAR(k, market.forward() * std::exp(0.5 * 0.09 * 0.09 * 0.5), 1e-15);
  for (auto convention : {DeltaConvention::spot, DeltaConvention::forward}) {
    const auto strike = must(fxval::Strike::from_value(k));
    const double call =
        must(delta_at_strike(market, strike, vol(0.09), OptionType::call, convention));
    const double put =
        must(delta_at_strike(market, strike, vol(0.09), OptionType::put, convention));
    EXPECT_NEAR(call + put, 0.0, 1e-15);
  }
}

TEST(DeltaConventionTest, SpotDeltaStrikeIsForwardDeltaStrikeAtAScaledDelta) {
  // Spot delta d is forward delta d e^{r_f T}; the two conversions must agree on that identity.
  const auto market = make_smile_market(1.1, 0.04, 0.03, 2.0);
  const double scale = std::exp(0.03 * 2.0);
  for (auto type : {OptionType::call, OptionType::put}) {
    const double spot_k =
        must(strike_from_delta(market, vol(0.1), 0.25, type, DeltaConvention::spot));
    const double fwd_k =
        must(strike_from_delta(market, vol(0.1), 0.25 * scale, type, DeltaConvention::forward));
    EXPECT_NEAR(spot_k / fwd_k, 1.0, 1e-14);
  }
}

TEST(DeltaConventionTest, DeltaAtStrikeAgreesWithGarmanKohlhagenGreeks) {
  // The independent code path: the library's own Greeks.
  const auto market = make_smile_market(1.1, 0.04, 0.025, 0.75);
  for (double strike : {0.9, 1.05, 1.1, 1.25}) {
    for (auto type : {OptionType::call, OptionType::put}) {
      const auto greeks = gk_all(type, 1.1, strike, 0.04, 0.025, 0.12, 0.75);
      const auto k = must(fxval::Strike::from_value(strike));
      EXPECT_NEAR(must(delta_at_strike(market, k, vol(0.12), type, DeltaConvention::spot)),
                  greeks.spot_delta,
                  1e-14);
      EXPECT_NEAR(must(delta_at_strike(market, k, vol(0.12), type, DeltaConvention::forward)),
                  greeks.forward_delta,
                  1e-14);
    }
  }
}

TEST(DeltaConventionTest, LargerCallDeltaMeansLowerStrike) {
  const auto market = make_smile_market(1.1, 0.04, 0.025, 1.0);
  double previous = 1e300;
  for (double delta : {0.05, 0.10, 0.25, 0.40, 0.45}) {
    const double k =
        must(strike_from_delta(market, vol(0.1), delta, OptionType::call, DeltaConvention::spot));
    EXPECT_LT(k, previous);
    previous = k;
  }
}

TEST(DeltaConventionTest, NoRandomnessMeansNoStrikeFromDelta) {
  const auto market = make_smile_market(1.1, 0.04, 0.025, 1.0);
  const auto expired = make_smile_market(1.1, 0.04, 0.025, 0.0);
  for (const auto& result :
       {strike_from_delta(market, vol(0.0), 0.25, OptionType::call, DeltaConvention::spot),
        strike_from_delta(expired, vol(0.1), 0.25, OptionType::call, DeltaConvention::spot),
        fxval::smile::atm_strike(market, vol(0.0)),
        fxval::smile::atm_strike(expired, vol(0.1))}) {
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::invalid_argument);
  }
}

TEST(DeltaConventionTest, DeltaOutsideTheAdmissibleRangeIsRejected) {
  const auto market = make_smile_market(1.1, 0.04, 0.05, 1.0);  // e^{-r_f T} = 0.951
  struct Case {
    double delta;
    DeltaConvention convention;
  };
  for (const Case c : {Case{0.0, DeltaConvention::spot},
                       Case{-0.1, DeltaConvention::forward},
                       Case{0.96, DeltaConvention::spot},
                       Case{1.0, DeltaConvention::forward},
                       Case{std::nan(""), DeltaConvention::spot}}) {
    const auto r = strike_from_delta(market, vol(0.1), c.delta, OptionType::call, c.convention);
    ASSERT_FALSE(r.has_value()) << c.delta;
    EXPECT_EQ(r.error().code, ErrorCode::invalid_argument);
  }
  // 0.96 is a legal FORWARD delta (< 1) even though it is not a legal spot delta here.
  EXPECT_TRUE(strike_from_delta(market, vol(0.1), 0.96, OptionType::call, DeltaConvention::forward)
                  .has_value());
}

}  // namespace
