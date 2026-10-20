#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "support/calibration_test_support.hpp"

namespace {

using fxval::ErrorCode;
using fxval::calibration::calibrate_heston;
using fxval::calibration::CalibrationSettings;
using fxval::calibration::Pivot;
using fxval::smile::DeltaConvention;
using fxval::testing::heston_slices;
using fxval::testing::make_calibration_market;
using fxval::testing::make_params;
using fxval::testing::make_quotes;
using fxval::testing::must;

const std::vector<double> kExpiries{1.0 / 12.0, 0.25, 0.5, 1.0, 2.0};

TEST(HestonCalibrationTest, RecoversKnownParametersFromExactQuotes) {
  const auto truth = make_params(0.0225, 1.8, 0.0324, 0.55, -0.35);
  const auto market = make_calibration_market(1.1, 0.04, 0.025);
  CalibrationSettings settings;
  settings.random_starts = 2;
  const auto result =
      must(calibrate_heston(market, heston_slices(truth, market, kExpiries), settings));
  EXPECT_TRUE(result.converged);
  EXPECT_LT(result.max_abs_error, 1e-9);
  EXPECT_NEAR(result.params.v0(), truth.v0(), 1e-6);
  EXPECT_NEAR(result.params.kappa(), truth.kappa(), 1e-5);
  EXPECT_NEAR(result.params.theta(), truth.theta(), 1e-6);
  EXPECT_NEAR(result.params.xi(), truth.xi(), 1e-6);
  EXPECT_NEAR(result.params.rho(), truth.rho(), 1e-6);
  EXPECT_EQ(result.fits.size(), 15U);
  EXPECT_EQ(result.starts.size(), 3U);
  EXPECT_GT(result.evaluations, 0);
}

TEST(HestonCalibrationTest, FitListsEveryQuoteWithItsStrikeAndError) {
  const auto truth = make_params(0.0225, 1.8, 0.0324, 0.55, -0.35);
  const auto market = make_calibration_market(1.1, 0.04, 0.025);
  CalibrationSettings settings;
  settings.random_starts = 0;
  const auto slices = heston_slices(truth, market, {0.5, 1.0});
  const auto result = must(calibrate_heston(market, slices, settings));
  ASSERT_EQ(result.fits.size(), 6U);
  EXPECT_EQ(result.fits[0].pivot, Pivot::put);
  EXPECT_EQ(result.fits[1].pivot, Pivot::atm);
  EXPECT_EQ(result.fits[2].pivot, Pivot::call);
  EXPECT_LT(result.fits[0].strike, result.fits[1].strike);
  EXPECT_LT(result.fits[1].strike, result.fits[2].strike);
  for (const auto& fit : result.fits) {
    EXPECT_NEAR(fit.model_vol - fit.market_vol, fit.error, 1e-15);
  }
  // The market vol of the ATM pivot is the quoted ATM.
  EXPECT_NEAR(result.fits[1].market_vol, slices[0].quotes.atm.value(), 1e-15);
}

TEST(HestonCalibrationTest, ForwardDeltaConventionIsRecoveredToo) {
  const auto truth = make_params(0.04, 1.2, 0.05, 0.7, 0.25);
  const auto market = make_calibration_market(1.3, 0.02, 0.01, DeltaConvention::forward);
  CalibrationSettings settings;
  settings.random_starts = 2;
  const auto result =
      must(calibrate_heston(market, heston_slices(truth, market, kExpiries), settings));
  EXPECT_LT(result.max_abs_error, 1e-9);
  EXPECT_NEAR(result.params.rho(), 0.25, 1e-5);
  EXPECT_NEAR(result.params.xi(), 0.7, 1e-5);
}

TEST(HestonCalibrationTest, IsDeterministic) {
  const auto truth = make_params(0.0225, 1.8, 0.0324, 0.55, -0.35);
  const auto market = make_calibration_market(1.1, 0.04, 0.025);
  const auto slices = heston_slices(truth, market, {0.25, 1.0});
  CalibrationSettings settings;
  settings.random_starts = 3;
  const auto a = must(calibrate_heston(market, slices, settings));
  const auto b = must(calibrate_heston(market, slices, settings));
  EXPECT_EQ(a.params.v0(), b.params.v0());
  EXPECT_EQ(a.params.rho(), b.params.rho());
  EXPECT_EQ(a.best_start, b.best_start);
  for (std::size_t i = 0; i < a.starts.size(); ++i) {
    EXPECT_EQ(a.starts[i].start, b.starts[i].start);
    EXPECT_EQ(a.starts[i].cost, b.starts[i].cost);
  }
  // The random starts differ from each other (each has its own stream) and from the first one.
  EXPECT_NE(a.starts[1].start, a.starts[2].start);
  EXPECT_NE(a.starts[2].start, a.starts[3].start);
  EXPECT_NE(a.starts[0].start, a.starts[1].start);
  // A different seed changes the random starts but not the first (data-driven) one.
  settings.seed += 1;
  const auto c = must(calibrate_heston(market, slices, settings));
  EXPECT_EQ(a.starts[0].start, c.starts[0].start);
  EXPECT_NE(a.starts[1].start, c.starts[1].start);
}

TEST(HestonCalibrationTest, DataDrivenStartTakesItsSkewSignFromTheRiskReversal) {
  const auto market = make_calibration_market(1.1, 0.04, 0.025);
  CalibrationSettings settings;
  settings.random_starts = 0;
  for (double rho : {-0.5, 0.5}) {
    const auto truth = make_params(0.04, 1.5, 0.04, 0.6, rho);
    const auto result =
        must(calibrate_heston(market, heston_slices(truth, market, {0.5, 1.0}), settings));
    ASSERT_EQ(result.starts.size(), 1U);
    EXPECT_EQ(std::signbit(result.starts[0].start[4]), rho < 0.0);
  }
}

TEST(HestonCalibrationTest, AnExplicitInitialGuessReplacesTheDataDrivenStart) {
  const auto truth = make_params(0.0225, 1.8, 0.0324, 0.55, -0.35);
  const auto market = make_calibration_market(1.1, 0.04, 0.025);
  CalibrationSettings settings;
  settings.random_starts = 0;
  settings.initial_guess = make_params(0.03, 1.0, 0.03, 0.4, -0.2);
  const auto result =
      must(calibrate_heston(market, heston_slices(truth, market, kExpiries), settings));
  ASSERT_EQ(result.starts.size(), 1U);
  EXPECT_NEAR(result.starts[0].start[0], 0.03, 1e-12);
  EXPECT_LT(result.max_abs_error, 1e-9);
}

TEST(HestonCalibrationTest, ParametersOutsideTheBoxAreReportedNearTheBound) {
  // Truth has kappa = 12, but the box stops at 8: the optimiser must end at the bound and say so.
  const auto truth = make_params(0.03, 12.0, 0.03, 0.8, -0.3);
  const auto market = make_calibration_market(1.1, 0.04, 0.025);
  CalibrationSettings settings;
  settings.random_starts = 1;
  settings.bounds.upper[1] = 8.0;
  const auto result =
      must(calibrate_heston(market, heston_slices(truth, market, kExpiries), settings));
  EXPECT_LE(result.params.kappa(), 8.0);
  EXPECT_TRUE(result.near_bound[1]);
}

TEST(HestonCalibrationTest, OneExpiryHasMoreUnknownsThanQuotes) {
  const auto truth = make_params(0.0225, 1.8, 0.0324, 0.55, -0.35);
  const auto market = make_calibration_market(1.1, 0.04, 0.025);
  CalibrationSettings settings;
  settings.random_starts = 2;
  const auto result = must(calibrate_heston(market, heston_slices(truth, market, {1.0}), settings));
  // Three quotes can be fitted exactly by many parameter sets, so the fit is good but the
  // parameters are not identified and the standard errors say so.
  EXPECT_LT(result.max_abs_error, 1e-7);
  for (double se : result.standard_errors) {
    EXPECT_EQ(se, std::numeric_limits<double>::infinity());
  }
}

TEST(HestonCalibrationTest, StandardErrorsAreFiniteAndSmallForExactDataWithManyQuotes) {
  const auto truth = make_params(0.0225, 1.8, 0.0324, 0.55, -0.35);
  const auto market = make_calibration_market(1.1, 0.04, 0.025);
  CalibrationSettings settings;
  settings.random_starts = 0;
  const auto result =
      must(calibrate_heston(market, heston_slices(truth, market, kExpiries), settings));
  for (double se : result.standard_errors) {
    EXPECT_TRUE(std::isfinite(se));
    EXPECT_LT(se, 1e-6);
  }
  settings.compute_standard_errors = false;
  const auto without =
      must(calibrate_heston(market, heston_slices(truth, market, kExpiries), settings));
  for (double se : without.standard_errors) {
    EXPECT_EQ(se, std::numeric_limits<double>::infinity());
  }
}

TEST(HestonCalibrationTest, RejectsInvalidInput) {
  const auto market = make_calibration_market(1.1, 0.04, 0.025);
  CalibrationSettings settings;
  const auto empty = calibrate_heston(market, {}, settings);
  ASSERT_FALSE(empty.has_value());
  EXPECT_EQ(empty.error().code, ErrorCode::invalid_argument);

  const auto truth = make_params(0.0225, 1.8, 0.0324, 0.55, -0.35);
  auto slices = heston_slices(truth, market, {1.0});
  CalibrationSettings bad_bounds;
  bad_bounds.bounds.lower[0] = 2.0;
  EXPECT_FALSE(calibrate_heston(market, slices, bad_bounds).has_value());
  CalibrationSettings bad_starts;
  bad_starts.random_starts = -1;
  EXPECT_FALSE(calibrate_heston(market, slices, bad_starts).has_value());

  const fxval::calibration::MarketSlice zero_expiry{must(fxval::Years::from_years(0.0)),
                                                    make_quotes(0.08, 0.0, 0.0)};
  EXPECT_FALSE(calibrate_heston(market, {zero_expiry}, settings).has_value());
  // Quotes that cannot form a smile (wing volatility would be negative).
  const fxval::calibration::MarketSlice bad_quotes{must(fxval::Years::from_years(1.0)),
                                                   make_quotes(0.05, 0.3, 0.0)};
  EXPECT_FALSE(calibrate_heston(market, {bad_quotes}, settings).has_value());
}

TEST(HestonCalibrationTest, ExactCurrencySwapInvarianceOfTheModel) {
  // Swapping the two currencies of a Heston FX model gives a Heston model with
  //   rho' = -rho, kappa' = kappa - rho xi, theta' = kappa theta / kappa', same v0 and xi.
  // The quotes of the swapped market are therefore those of the dual parameters, with the roles
  // of the rates and the sign of the risk reversal exchanged: calibrating the original quotes and
  // the swapped quotes must give parameters related by that map. This checks the calibration
  // against a property of the model that no fitting code knows about.
  // (Disclosure: an earlier test assumed that flipping rho alone flips the risk reversal. That is
  // false for this model, because the dual also changes kappa and theta; it failed and was
  // replaced by this exact relation.)
  const double v0 = 0.0225, kappa = 1.8, theta = 0.0324, xi = 0.55, rho = -0.35;
  const auto truth = make_params(v0, kappa, theta, xi, rho);
  const double kappa_d = kappa - rho * xi;
  const auto dual = make_params(v0, kappa_d, kappa * theta / kappa_d, xi, -rho);

  // Foreign-currency view of the same market: spot 1/S, rates exchanged, forward delta.
  const auto market = make_calibration_market(1.25, 0.04, 0.025, DeltaConvention::forward);
  const auto swapped = make_calibration_market(1.0 / 1.25, 0.025, 0.04, DeltaConvention::forward);
  CalibrationSettings settings;
  settings.random_starts = 2;
  const auto direct =
      must(calibrate_heston(market, heston_slices(truth, market, kExpiries), settings));
  const auto from_swapped =
      must(calibrate_heston(swapped, heston_slices(dual, swapped, kExpiries), settings));
  EXPECT_NEAR(direct.params.rho(), -from_swapped.params.rho(), 1e-5);
  EXPECT_NEAR(direct.params.xi(), from_swapped.params.xi(), 1e-5);
  EXPECT_NEAR(direct.params.v0(), from_swapped.params.v0(), 1e-6);
  // The map is an involution, so kappa = kappa' - rho' xi. (Disclosure: the first version of this
  // line had the wrong sign, a mistake in the test and not in the library: the dual of the dual
  // is the original.)
  EXPECT_NEAR(direct.params.kappa(),
              from_swapped.params.kappa() - from_swapped.params.rho() * from_swapped.params.xi(),
              1e-4);
  EXPECT_NEAR(direct.params.theta() * direct.params.kappa(),
              from_swapped.params.theta() * from_swapped.params.kappa(),
              1e-6);
}

}  // namespace
