// Unit tests for the adaptive Gauss-Kronrod routine used by the Heston pricer. They test the
// routine on integrals with known answers, so a wrong node or weight is found here directly
// and not only through a Heston price that is slightly off.

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "fxval/numerics/quadrature.hpp"

namespace {

using fxval::numerics::adaptive_gauss_kronrod;

TEST(Quadrature, NodesAndWeightsIntegratePolynomialsExactly) {
  // A 15-point Kronrod rule is exact up to degree 22 (at least). Eight initial panels each apply
  // the rule, so a polynomial of degree 20 must come out to rounding error with no refinement.
  for (int degree : {0, 1, 2, 5, 10, 15, 20}) {
    const auto f = [degree](double x) {
      return std::pow(x, degree);
    };
    const auto r = adaptive_gauss_kronrod(f, 0.0, 1.0, 1e-14, 0.0, 8);
    ASSERT_TRUE(r.converged) << "degree " << degree;
    EXPECT_NEAR(r.value, 1.0 / (degree + 1), 1e-15) << "degree " << degree;
  }
}

TEST(Quadrature, SmoothIntegralsWithKnownValues) {
  const auto sine = adaptive_gauss_kronrod(
      [](double x) { return std::sin(x); }, 0.0, std::numbers::pi, 1e-14, 1e-13, 1000);
  ASSERT_TRUE(sine.converged);
  EXPECT_NEAR(sine.value, 2.0, 1e-14);

  // integral of exp(-x^2) over [-6, 6] is sqrt(pi) up to erfc(6) ~ 2e-17.
  const auto gauss = adaptive_gauss_kronrod(
      [](double x) { return std::exp(-x * x); }, -6.0, 6.0, 1e-14, 1e-13, 1000);
  ASSERT_TRUE(gauss.converged);
  EXPECT_NEAR(gauss.value, std::sqrt(std::numbers::pi), 1e-13);
}

TEST(Quadrature, RefinesWhereTheIntegrandIsDifficult) {
  // sqrt(x) has an infinite derivative at 0; integral over [0, 1] is 2/3.
  const auto r =
      adaptive_gauss_kronrod([](double x) { return std::sqrt(x); }, 0.0, 1.0, 1e-12, 0.0, 4000);
  ASSERT_TRUE(r.converged);
  EXPECT_NEAR(r.value, 2.0 / 3.0, 1e-11);

  // A narrow peak that the 8 initial panels could miss entirely if there were fewer of them.
  const auto peak =
      adaptive_gauss_kronrod([](double x) { return std::exp(-1e5 * (x - 0.3217) * (x - 0.3217)); },
                             0.0,
                             1.0,
                             1e-12,
                             0.0,
                             4000);
  ASSERT_TRUE(peak.converged);
  EXPECT_NEAR(peak.value, std::sqrt(std::numbers::pi / 1e5), 1e-11);
}

TEST(Quadrature, IntegralOverAReversedIntervalHasTheOppositeSign) {
  const auto f = [](double x) {
    return std::cos(x);
  };
  const auto forward = adaptive_gauss_kronrod(f, 0.0, 1.0, 1e-14, 1e-13, 1000);
  const auto backward = adaptive_gauss_kronrod(f, 1.0, 0.0, 1e-14, 1e-13, 1000);
  EXPECT_NEAR(forward.value, std::sin(1.0), 1e-14);
  EXPECT_NEAR(backward.value, -forward.value, 1e-15);
}

TEST(Quadrature, ReportsNonConvergenceInsteadOfAnUnreliableValue) {
  // A rapidly oscillating integrand cannot reach 1e-14 with only the 8 initial panels.
  const auto r = adaptive_gauss_kronrod(
      [](double x) { return std::sin(2000.0 * x); }, 0.0, 1.0, 1e-14, 0.0, 8);
  EXPECT_FALSE(r.converged);
  EXPECT_GT(r.error, 1e-14);
}

TEST(Quadrature, ATolerancePastTheRoundingLevelIsRelaxedNotImpossible) {
  // Asking for 1e-300 cannot be met by a floating-point sum. The routine accepts the rounding
  // level (about 10 eps times the integral of |f|) instead of refining until the panel limit, and
  // the value is still accurate to that level. A jump discontinuity, placed away from the initial
  // panel edges, makes it bisect many times before it gets there.
  const auto r = adaptive_gauss_kronrod(
      [](double x) { return x < 0.3 ? 0.0 : 1.0; }, 0.0, 1.0, 1e-300, 0.0, 100000);
  EXPECT_TRUE(r.converged);
  EXPECT_NEAR(r.value, 0.7, 1e-14);
}

}  // namespace
