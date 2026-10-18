#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "fxval/core/limits.hpp"
#include "support/smile_test_support.hpp"

namespace {

using fxval::ErrorCode;
using fxval::smile::check_butterfly;
using fxval::smile::check_calendar;
using fxval::smile::check_surface;
using fxval::smile::CheckGrid;
using fxval::smile::density_factor;
using fxval::testing::make_slice;
using fxval::testing::must;

TEST(ArbitrageChecksTest, FlatSmileHasUnitDensityFactorEverywhere) {
  const auto slice = make_slice(1.1, 0.04, 0.025, 1.0, 0.08, 0.0, 0.0);
  for (double k : {-0.6, -0.1, 0.0, 0.3, 0.8}) {
    EXPECT_NEAR(must(density_factor(slice, k)), 1.0, 1e-9);
  }
  const auto report = must(check_butterfly(slice));
  EXPECT_TRUE(report.clean());
  EXPECT_EQ(report.violations, 0);
  EXPECT_FALSE(report.innermost.has_value());
  EXPECT_NEAR(report.min_density_factor, 1.0, 1e-9);
}

TEST(ArbitrageChecksTest, ModerateSmileIsCleanOnTheDefaultGridButTheQuadraticFailsFarOut) {
  const auto slice = make_slice(1.1, 0.04, 0.025, 1.0, 0.08, -0.0075, 0.0028);
  EXPECT_TRUE(
      must(check_butterfly(slice)).clean());  // +-4 standard deviations: observed, not proved
  // A quadratic volatility grows like k^2, so total variance grows like k^4 and the density factor
  // is eventually negative whatever the quotes (theory). The report must say where.
  const auto wide = must(check_butterfly(slice, CheckGrid{40.0, 4001}));
  ASSERT_GT(wide.violations, 0);
  ASSERT_TRUE(wide.innermost.has_value());
  EXPECT_GT(std::abs(wide.innermost->standardised), 10.0);
  EXPECT_LT(wide.innermost->value, 0.0);
  EXPECT_LT(wide.min_density_factor, 0.0);
  EXPECT_FALSE(wide.clean());
}

TEST(ArbitrageChecksTest, ConcaveSmileViolatesTheButterflyConditionNearTheMoney) {
  // Negative butterfly: wings below the ATM level. A concave smile has negative curvature in w
  // near the money, which is exactly what g < 0 means, and the report places it in delta terms.
  const auto slice = make_slice(1.1, 0.04, 0.025, 1.0, 0.10, 0.0, -0.04);
  const auto report = must(check_butterfly(slice));
  ASSERT_GT(report.violations, 0);
  ASSERT_TRUE(report.innermost.has_value());
  EXPECT_GT(report.innermost->abs_delta, 0.0);
  EXPECT_NEAR(
      report.innermost->abs_delta, 0.5, 0.05);  // at the forward: the violation is at the money
}

TEST(ArbitrageChecksTest, CountsNonPositiveVolatilityPointsAndDoesNotEvaluateTheDensityThere) {
  const auto slice = make_slice(1.1, 0.04, 0.025, 1.0, 0.10, 0.0, -0.04);
  const auto report = must(check_butterfly(slice, CheckGrid{40.0, 4001}));
  EXPECT_GT(report.non_positive_vol_points, 0);
  EXPECT_FALSE(report.clean());
  // The first point (largest |x|) is non-positive; no density factor exists there.
  const double k = -40.0 * slice.atm_scale();
  ASSERT_LE(slice.point(k).vol, 0.0);
  EXPECT_FALSE(density_factor(slice, k).has_value());
}

TEST(ArbitrageChecksTest, DensityFactorIsUndefinedWherePolynomialIsNotPositive) {
  const auto slice = make_slice(1.1, 0.04, 0.025, 1.0, 0.10, 0.0, -0.04);
  const auto r = density_factor(slice, 3.0);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::numerical_failure);
  EXPECT_FALSE(density_factor(slice, std::nan("")).has_value());
}

TEST(ArbitrageChecksTest, CalendarCheckPassesWhenTotalVarianceGrows) {
  const auto early = make_slice(1.1, 0.04, 0.025, 0.25, 0.08, 0.0, 0.0);
  const auto late = make_slice(1.1, 0.04, 0.025, 1.0, 0.08, 0.0, 0.0);
  const auto report = must(check_calendar(early, late));
  EXPECT_TRUE(report.clean());
  EXPECT_NEAR(report.worst_relative_change, 3.0, 1e-9);  // same vol, 4x the time
}

TEST(ArbitrageChecksTest, CalendarCheckFlagsFallingTotalVariance) {
  // 1 week at 20% against 2 weeks at 9%: 7.7e-4 against 3.1e-4.
  const auto early = make_slice(1.1, 0.04, 0.025, 7.0 / 365, 0.20, 0.0, 0.0);
  const auto late = make_slice(1.1, 0.04, 0.025, 14.0 / 365, 0.09, 0.0, 0.0);
  const auto report = must(check_calendar(early, late));
  EXPECT_EQ(report.violations, report.grid_points);
  EXPECT_LT(report.worst_relative_change, -0.5);
  ASSERT_TRUE(report.innermost.has_value());
  EXPECT_NEAR(report.innermost->standardised, 0.0, 0.01);
}

TEST(ArbitrageChecksTest, CalendarCheckNeedsTheLaterSliceSecond) {
  const auto a = make_slice(1.1, 0.04, 0.025, 0.25, 0.08, 0.0, 0.0);
  const auto b = make_slice(1.1, 0.04, 0.025, 1.0, 0.08, 0.0, 0.0);
  const auto r = check_calendar(b, a);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::invalid_argument);
}

TEST(ArbitrageChecksTest, GridMustBeUsable) {
  const auto slice = make_slice(1.1, 0.04, 0.025, 1.0, 0.08, 0.0, 0.0);
  for (const CheckGrid grid : {CheckGrid{0.0, 801},
                               CheckGrid{-1.0, 801},
                               CheckGrid{4.0, 2},
                               CheckGrid{std::nan(""), 801}}) {
    const auto r = check_butterfly(slice, grid);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::invalid_argument);
  }
}

TEST(ArbitrageChecksTest, SurfaceReportHasOneButterflyPerSliceAndOneCalendarPerPair) {
  std::vector<fxval::smile::SmileSlice> slices;
  slices.push_back(make_slice(1.1, 0.04, 0.025, 0.25, 0.08, 0.0, 0.0));
  slices.push_back(make_slice(1.1, 0.04, 0.025, 0.5, 0.08, 0.0, 0.0));
  slices.push_back(make_slice(1.1, 0.04, 0.025, 1.0, 0.08, 0.0, 0.0));
  const auto report =
      must(check_surface(must(fxval::smile::VolSurface::create(std::move(slices)))));
  EXPECT_EQ(report.butterfly.size(), 3u);
  EXPECT_EQ(report.calendar.size(), 2u);
  EXPECT_TRUE(report.clean());
}

TEST(ArbitrageChecksTest, SlackIsRoundingLevelNotAModellingChoice) {
  EXPECT_LE(fxval::tol::arbitrage_slack, 1e-10);
}

}  // namespace
