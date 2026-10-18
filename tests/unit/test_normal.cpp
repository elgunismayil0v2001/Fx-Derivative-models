#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "fxval/numerics/normal.hpp"
#include "support/gk_test_support.hpp"

namespace {

using fxval::numerics::normal_cdf;
using fxval::numerics::normal_pdf;
using fxval::numerics::normal_quantile;
using fxval::testing::must;

TEST(NormalTest, DensityAndDistributionKnownValues) {
  EXPECT_NEAR(normal_pdf(0.0), 0.3989422804014327, 1e-16);
  EXPECT_NEAR(normal_cdf(0.0), 0.5, 1e-16);
  EXPECT_NEAR(normal_cdf(1.959963984540054), 0.975, 1e-15);
  // Lower tail keeps relative accuracy: N(-10) = 7.619853024160527e-24 (SciPy).
  EXPECT_NEAR(normal_cdf(-10.0) / 7.619853024160527e-24, 1.0, 1e-13);
}

TEST(NormalTest, QuantileKnownValues) {
  EXPECT_EQ(must(normal_quantile(0.5)), 0.0);
  EXPECT_NEAR(must(normal_quantile(0.975)), 1.959963984540054, 1e-14);
  EXPECT_NEAR(must(normal_quantile(0.25)), -0.6744897501960817, 1e-15);
  EXPECT_NEAR(must(normal_quantile(0.8413447460685429)), 1.0, 1e-14);
  EXPECT_NEAR(must(normal_quantile(1e-10)), -6.361340902404056, 1e-12);
}

TEST(NormalTest, QuantileInvertsTheDistributionFunctionAcrossTheRange) {
  // The check is made in the probability, which is what a strike conversion depends on. A quantile
  // x is only representable to one ulp, and an error dx in x is a relative error |x| dx in p, so
  // the best possible round trip has relative error about x^2 eps; the bound allows four times
  // that.
  const double eps = std::numeric_limits<double>::epsilon();
  double worst_ratio = 0.0;  // observed error divided by the allowed error
  for (double p = 1e-300; p < 0.5; p *= 1.7) {
    const double x = must(normal_quantile(p));
    const double allowed = 4.0 * x * x * eps + 1e-15;
    worst_ratio = std::max(worst_ratio, std::abs(normal_cdf(x) / p - 1.0) / allowed);
  }
  for (double p = 0.5; p < 1.0 - 1e-15; p += 0.0137) {
    const double x = must(normal_quantile(p));
    worst_ratio = std::max(worst_ratio, std::abs(normal_cdf(x) - p) / (4.0 * eps));
  }
  EXPECT_LT(worst_ratio, 1.0);
}

TEST(NormalTest, QuantileIsOddAroundOneHalfAndIncreasing) {
  double previous = -std::numeric_limits<double>::infinity();
  for (double p = 0.001; p < 0.5; p += 0.001) {
    const double lower = must(normal_quantile(p));
    EXPECT_NEAR(must(normal_quantile(1.0 - p)), -lower, 1e-12);  // 1 - p is rounded once
    EXPECT_GT(lower, previous);
    previous = lower;
  }
}

TEST(NormalTest, QuantileOfTinyProbabilityIsFiniteEvenWhenTheCorrectionOverflows) {
  const double x = must(normal_quantile(std::numeric_limits<double>::denorm_min()));
  EXPECT_TRUE(std::isfinite(x));
  EXPECT_LT(x, -38.0);
}

TEST(NormalTest, QuantileRejectsProbabilitiesOutsideTheOpenInterval) {
  for (double p : {0.0,
                   1.0,
                   -0.1,
                   1.5,
                   std::numeric_limits<double>::quiet_NaN(),
                   std::numeric_limits<double>::infinity()}) {
    const auto result = normal_quantile(p);
    ASSERT_FALSE(result.has_value()) << p;
    EXPECT_EQ(result.error().code, fxval::ErrorCode::invalid_argument);
  }
}

}  // namespace
