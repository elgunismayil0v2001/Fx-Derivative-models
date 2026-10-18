#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "support/smile_test_support.hpp"

namespace {

using fxval::ErrorCode;
using fxval::smile::SmileSlice;
using fxval::smile::VolSurface;
using fxval::testing::make_slice;
using fxval::testing::must;

std::vector<SmileSlice> three_slices() {
  std::vector<SmileSlice> slices;
  slices.push_back(make_slice(1.1, 0.04, 0.025, 0.25, 0.07, -0.005, 0.002));
  slices.push_back(make_slice(1.1, 0.04, 0.025, 0.5, 0.075, -0.006, 0.0025));
  slices.push_back(make_slice(1.1, 0.04, 0.025, 1.0, 0.08, -0.007, 0.003));
  return slices;
}

double w_of(const SmileSlice& s, double k) {
  const double v = s.point(k).vol;
  return v * v * s.expiry().value();
}

TEST(VolSurfaceTest, AtANodeItReturnsTheSliceExactly) {
  const auto slices = three_slices();
  const auto surface = must(VolSurface::create(three_slices()));
  for (const auto& slice : slices) {
    EXPECT_EQ(must(surface.total_variance(0.03, slice.expiry())), w_of(slice, 0.03));
  }
}

TEST(VolSurfaceTest, BetweenNodesTotalVarianceIsLinearInExpiryAtFixedMoneyness) {
  const auto slices = three_slices();
  const auto surface = must(VolSurface::create(three_slices()));
  const double k = -0.04;
  const double t = 0.4;  // between 0.25 and 0.5
  const double weight = (t - 0.25) / 0.25;
  const double expected = (1 - weight) * w_of(slices[0], k) + weight * w_of(slices[1], k);
  EXPECT_NEAR(must(surface.total_variance(k, must(fxval::Years::from_years(t)))), expected, 1e-16);
  const double vol = must(surface.vol(k, must(fxval::Years::from_years(t)))).value();
  EXPECT_NEAR(vol * vol * t, expected, 1e-16);
}

TEST(VolSurfaceTest, FlatTermStructureOfVolatilityGivesFlatVolatilityBetweenNodes) {
  std::vector<SmileSlice> slices;
  slices.push_back(make_slice(1.1, 0.04, 0.025, 0.25, 0.08, 0.0, 0.0));
  slices.push_back(make_slice(1.1, 0.04, 0.025, 1.0, 0.08, 0.0, 0.0));
  const auto surface = must(VolSurface::create(std::move(slices)));
  EXPECT_NEAR(must(surface.vol(0.0, must(fxval::Years::from_years(0.6)))).value(), 0.08, 1e-14);
}

TEST(VolSurfaceTest, NoExtrapolation) {
  const auto surface = must(VolSurface::create(three_slices()));
  for (double t : {0.1, 1.5}) {
    const auto r = surface.total_variance(0.0, must(fxval::Years::from_years(t)));
    ASSERT_FALSE(r.has_value()) << t;
    EXPECT_EQ(r.error().code, ErrorCode::invalid_argument);
  }
  EXPECT_FALSE(
      surface.total_variance(std::nan(""), must(fxval::Years::from_years(0.5))).has_value());
}

TEST(VolSurfaceTest, CreationRejectsEmptyAndUnorderedInput) {
  EXPECT_FALSE(VolSurface::create({}).has_value());
  std::vector<SmileSlice> equal;
  equal.push_back(make_slice(1.1, 0.04, 0.025, 0.5, 0.07, 0.0, 0.0));
  equal.push_back(make_slice(1.1, 0.04, 0.025, 0.5, 0.08, 0.0, 0.0));
  const auto r = VolSurface::create(std::move(equal));
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::invalid_argument);
  std::vector<SmileSlice> reversed;
  reversed.push_back(make_slice(1.1, 0.04, 0.025, 1.0, 0.07, 0.0, 0.0));
  reversed.push_back(make_slice(1.1, 0.04, 0.025, 0.5, 0.08, 0.0, 0.0));
  EXPECT_FALSE(VolSurface::create(std::move(reversed)).has_value());
}

TEST(VolSurfaceTest, InvalidSliceVolatilityAtThePointPropagatesAsAnError) {
  std::vector<SmileSlice> slices;
  slices.push_back(make_slice(1.1, 0.04, 0.025, 0.25, 0.10, 0.0, -0.04));  // negative far out
  slices.push_back(make_slice(1.1, 0.04, 0.025, 1.0, 0.10, 0.0, 0.0));
  const auto surface = must(VolSurface::create(std::move(slices)));
  const auto r = surface.total_variance(2.0, must(fxval::Years::from_years(0.5)));
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::numerical_failure);
}

}  // namespace
