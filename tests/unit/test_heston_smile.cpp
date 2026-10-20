#include <gtest/gtest.h>

#include <cmath>

#include "support/calibration_test_support.hpp"

namespace {

using fxval::ErrorCode;
using fxval::calibration::heston_delta_quotes;
using fxval::calibration::heston_implied_volatility;
using fxval::pricing::OptionType;
using fxval::smile::DeltaConvention;
using fxval::testing::heston_value;
using fxval::testing::make_heston_market;
using fxval::testing::make_params;
using fxval::testing::make_smile_market;
using fxval::testing::must;

TEST(HestonSmileTest, ImpliedVolatilityReproducesTheHestonPrice) {
  const auto params = make_params(0.0225, 1.8, 0.0324, 0.55, -0.35);
  for (double strike : {0.9, 1.05, 1.12, 1.3}) {
    const auto vol =
        must(heston_implied_volatility(make_heston_market(1.1, strike, 0.04, 0.025, 0.5), params));
    // Price the same option with Garman-Kohlhagen at the implied volatility: it must equal the
    // Heston price, call and put alike (parity holds for both models).
    for (OptionType type : {OptionType::call, OptionType::put}) {
      EXPECT_NEAR(fxval::testing::gk_value(type, 1.1, strike, 0.04, 0.025, vol.value(), 0.5),
                  heston_value(type, 1.1, strike, 0.04, 0.025, 0.5, params),
                  1e-12);
    }
  }
}

TEST(HestonSmileTest, ZeroVolOfVolGivesAFlatSmileAtTheExpectedVariance) {
  // xi -> 0 and v0 = theta: Heston is Black-Scholes with sigma = sqrt(theta).
  const auto params = make_params(0.04, 2.0, 0.04, 1e-6, 0.0);
  for (double strike : {0.95, 1.1, 1.25}) {
    EXPECT_NEAR(
        must(heston_implied_volatility(make_heston_market(1.1, strike, 0.03, 0.01, 1.0), params))
            .value(),
        0.2,
        1e-6);
  }
}

TEST(HestonSmileTest, NegativeCorrelationSkewsTheSmileTowardsLowStrikes) {
  const auto neg = make_params(0.04, 1.5, 0.04, 0.6, -0.7);
  const auto pos = make_params(0.04, 1.5, 0.04, 0.6, 0.7);
  const auto vol = [](const fxval::models::HestonParams& p, double k) {
    return must(heston_implied_volatility(make_heston_market(1.0, k, 0.0, 0.0, 1.0), p)).value();
  };
  EXPECT_GT(vol(neg, 0.85), vol(neg, 1.15));
  EXPECT_LT(vol(pos, 0.85), vol(pos, 1.15));
}

TEST(HestonSmileTest, DeltaQuotesAreSelfConsistentWithTheSmileSlice) {
  // The quotes returned for a model, read back by SmileSlice, must put the pivot strikes where
  // the model really has those deltas and volatilities.
  const auto params = make_params(0.0225, 1.8, 0.0324, 0.55, -0.35);
  for (auto convention : {DeltaConvention::spot, DeltaConvention::forward}) {
    for (double t : {1.0 / 12.0, 0.5, 2.0}) {
      const auto market = make_smile_market(1.1, 0.04, 0.025, t);
      const auto quotes = must(heston_delta_quotes(market, params, 0.25, convention));
      const auto slice = must(fxval::smile::SmileSlice::from_quotes(market, quotes, convention));
      const auto strikes = slice.pivot_strikes();
      const auto vols = slice.pivot_volatilities();
      const auto model_vol = [&](double k) {
        return must(heston_implied_volatility(make_heston_market(1.1, k, 0.04, 0.025, t), params))
            .value();
      };
      EXPECT_NEAR(model_vol(strikes.put), vols.put.value(), 1e-10);
      EXPECT_NEAR(model_vol(strikes.atm), vols.atm.value(), 1e-10);
      EXPECT_NEAR(model_vol(strikes.call), vols.call.value(), 1e-10);
      // ...and those strikes carry the stated deltas under the model volatility there.
      EXPECT_NEAR(must(fxval::smile::delta_at_strike(market,
                                                     must(fxval::Strike::from_value(strikes.call)),
                                                     vols.call,
                                                     OptionType::call,
                                                     convention)),
                  0.25,
                  1e-10);
      EXPECT_NEAR(must(fxval::smile::delta_at_strike(market,
                                                     must(fxval::Strike::from_value(strikes.put)),
                                                     vols.put,
                                                     OptionType::put,
                                                     convention)),
                  -0.25,
                  1e-10);
    }
  }
}

TEST(HestonSmileTest, LongExpiryWithAStrongSkewStillFindsTheWingStrikes) {
  // Regression test. CAL-REF set 4 (2Y, spot delta, rho = -0.6) failed when the volatility search
  // for the 25-delta call started at five times the model level: the strike was 8 forwards away,
  // the price underflowed and the evaluation errored although the root was nearby.
  const auto params = make_params(0.0515934473439497,
                                  2.992708457447459,
                                  0.045310097737563615,
                                  0.32797014384781686,
                                  -0.5986734053247122);
  const auto market = make_smile_market(2.0, 0.03195212526378123, 0.023664695948984293, 2.0);
  const auto quotes = must(heston_delta_quotes(market, params, 0.25, DeltaConvention::spot));
  EXPECT_LT(quotes.risk_reversal.value(), 0.0);
  EXPECT_NEAR(quotes.atm.value(), 0.2065, 0.002);
}

TEST(HestonSmileTest, WingVolatilitiesFarBelowTheModelLevelAreBracketed) {
  // Regression test. With kappa = 0.3, xi = 1, T = 2 and a low variance the ATM volatility is about
  // a third of sqrt(E[integral of v] / T); a search that starts at 0.3 times that level finds no
  // sign change and used to fail with "not bracketed".
  const auto params = make_params(0.0025, 0.3, 0.0025, 1.0, -0.5);
  const auto market = make_smile_market(1.1, 0.03, 0.01, 2.0);
  for (auto convention : {DeltaConvention::spot, DeltaConvention::forward}) {
    const auto quotes = must(heston_delta_quotes(market, params, 0.25, convention));
    const double level = std::sqrt(fxval::models::expected_integrated_variance(params, 2.0) / 2.0);
    EXPECT_LT(quotes.atm.value(), 0.3 * level);
    // Self-consistency: the model volatility at the ATM strike is the quoted ATM.
    const auto slice = must(fxval::smile::SmileSlice::from_quotes(market, quotes, convention));
    const double vol =
        must(heston_implied_volatility(
                 make_heston_market(1.1, slice.pivot_strikes().atm, 0.03, 0.01, 2.0), params))
            .value();
    EXPECT_NEAR(vol, quotes.atm.value(), 1e-10);
  }
}

TEST(HestonSmileTest, QuotesHaveTheSignsTheParametersImply) {
  const auto market = make_smile_market(1.1, 0.04, 0.025, 1.0);
  const auto neg = must(heston_delta_quotes(
      market, make_params(0.04, 1.5, 0.04, 0.6, -0.5), 0.25, DeltaConvention::spot));
  const auto pos = must(heston_delta_quotes(
      market, make_params(0.04, 1.5, 0.04, 0.6, 0.5), 0.25, DeltaConvention::spot));
  EXPECT_LT(neg.risk_reversal.value(), 0.0);
  EXPECT_GT(pos.risk_reversal.value(), 0.0);
  EXPECT_GT(neg.butterfly.value(), 0.0);  // vol of vol gives curvature
  // A skew-free, curvature-free model gives flat quotes.
  const auto flat = must(heston_delta_quotes(
      market, make_params(0.04, 1.5, 0.04, 1e-6, 0.0), 0.25, DeltaConvention::spot));
  EXPECT_NEAR(flat.risk_reversal.value(), 0.0, 1e-6);
  EXPECT_NEAR(flat.butterfly.value(), 0.0, 1e-6);
}

TEST(HestonSmileTest, RejectsInvalidDeltasAndExpiries) {
  const auto params = make_params(0.04, 1.5, 0.04, 0.6, -0.5);
  const auto market = make_smile_market(1.1, 0.04, 0.025, 1.0);
  for (double delta : {0.0, 0.5, 0.7, -0.1, std::nan("")}) {
    const auto result = heston_delta_quotes(market, params, delta, DeltaConvention::spot);
    ASSERT_FALSE(result.has_value()) << delta;
    EXPECT_EQ(result.error().code, ErrorCode::invalid_argument);
  }
  EXPECT_FALSE(heston_delta_quotes(
                   make_smile_market(1.1, 0.04, 0.025, 0.0), params, 0.25, DeltaConvention::spot)
                   .has_value());
}

}  // namespace
