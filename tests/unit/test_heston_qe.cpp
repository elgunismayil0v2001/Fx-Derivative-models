// Unit tests for the Heston QE scheme. They protect the code: argument checks, the exact
// conditional moments of the variance step (computed here from the closed formulas, not by calling
// the implementation's own helpers), positivity, the martingale property, determinism.
// Evidence about accuracy against the semi-analytic price is in V-05 and V-06 (tests/validation/).

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <numbers>

#include "fxval/mc/engine.hpp"
#include "fxval/mc/heston_qe.hpp"
#include "support/mc_test_support.hpp"

namespace {

using fxval::ErrorCode;
using fxval::mc::HestonQuadraticExponential;
using fxval::mc::NormalStream;
using fxval::mc::PathModel;
using fxval::mc::run_monte_carlo;
using fxval::testing::make_config;
using fxval::testing::make_params;
using fxval::testing::must;
using fxval::testing::scaled_paths;

static_assert(PathModel<HestonQuadraticExponential>);

HestonQuadraticExponential make_qe(double v0,
                                   double kappa,
                                   double theta,
                                   double xi,
                                   double rho,
                                   double years,
                                   double psi_c = 1.5) {
  return must(HestonQuadraticExponential::create(must(fxval::Spot::from_value(1.10)),
                                                 must(fxval::Rate::from_decimal(0.03)),
                                                 must(fxval::Rate::from_decimal(0.01)),
                                                 must(fxval::Years::from_years(years)),
                                                 make_params(v0, kappa, theta, xi, rho),
                                                 psi_c));
}

TEST(HestonQeTest, RejectsBadArguments) {
  const auto spot = must(fxval::Spot::from_value(1.1));
  const auto rate = must(fxval::Rate::from_decimal(0.0));
  const auto good = make_params(0.04, 2.0, 0.04, 0.3, -0.5);
  const auto years = must(fxval::Years::from_years(1.0));
  const auto zero_expiry = must(fxval::Years::from_years(0.0));
  EXPECT_EQ(HestonQuadraticExponential::create(spot, rate, rate, zero_expiry, good).error().code,
            ErrorCode::invalid_argument);
  const auto no_vol_of_vol = make_params(0.04, 2.0, 0.04, 0.0, -0.5);
  const auto r = HestonQuadraticExponential::create(spot, rate, rate, years, no_vol_of_vol);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::invalid_argument);
  EXPECT_NE(r.error().message.find("xi"), std::string::npos);
  for (const double bad : {0.99, 2.01, -1.0, std::nan("")}) {
    const auto b = HestonQuadraticExponential::create(spot, rate, rate, years, good, bad);
    ASSERT_FALSE(b.has_value()) << bad;
    EXPECT_NE(b.error().message.find("psi_critical"), std::string::npos);
  }
  EXPECT_TRUE(HestonQuadraticExponential::create(spot, rate, rate, years, good, 1.0).has_value());
  EXPECT_TRUE(HestonQuadraticExponential::create(spot, rate, rate, years, good, 2.0).has_value());
}

// Exact conditional moments of the CIR variance:
//   m  = theta + (v - theta) e^{-kappa dt}
//   s2 = v xi^2 e (1-e) / kappa + theta xi^2 (1-e)^2 / (2 kappa).
// The three starting variances put the sampling rule in the quadratic branch (v = 0.2),
// near the switch (0.04) and deep in the exponential branch (0.002).
TEST(HestonQeTest, VarianceStepReproducesTheExactConditionalMeanAndVariance) {
  const double kappa = 1.5;
  const double theta = 0.04;
  const double xi = 0.6;
  const double dt = 0.25;
  const auto model = make_qe(0.04, kappa, theta, xi, -0.5, 1.0);
  const double e = std::exp(-kappa * dt);
  const std::size_t n = fxval::testing::scaled_paths(400'000);
  for (const double v : {0.2, 0.04, 0.002}) {
    const double m = theta + (v - theta) * e;
    const double s2 = v * xi * xi * e * (1.0 - e) / kappa +
                      theta * xi * xi * (1.0 - e) * (1.0 - e) / (2.0 * kappa);
    double sum = 0.0;
    double sum2 = 0.0;
    double sum4 = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
      double z = 0.0;
      double unused = 0.0;
      NormalStream(31, i).next_pair(z, unused);
      const double next = model.next_variance(v, dt, z);
      ASSERT_GE(next, 0.0);
      sum += next;
    }
    const double mean = sum / static_cast<double>(n);
    for (std::size_t i = 0; i < n; ++i) {
      double z = 0.0;
      double unused = 0.0;
      NormalStream(31, i).next_pair(z, unused);
      const double d = model.next_variance(v, dt, z) - mean;
      sum2 += d * d;
      sum4 += d * d * d * d;
    }
    const double nn = static_cast<double>(n);
    const double var = sum2 / (nn - 1.0);
    const double var_se = std::sqrt(std::max(sum4 / nn - var * var, 0.0) / nn);
    EXPECT_NEAR(mean, m, 4.0 * std::sqrt(s2 / nn)) << "v = " << v;
    EXPECT_NEAR(var, s2, 4.0 * var_se) << "v = " << v;
  }
}

TEST(HestonQeTest, ExponentialBranchHasThePointMassAtZeroOfTheRightSize) {
  const double kappa = 1.5;
  const double theta = 0.04;
  const double xi = 0.6;
  const double dt = 0.25;
  const double v = 0.002;
  const auto model = make_qe(0.04, kappa, theta, xi, -0.5, 1.0);
  const double e = std::exp(-kappa * dt);
  const double m = theta + (v - theta) * e;
  const double s2 =
      v * xi * xi * e * (1.0 - e) / kappa + theta * xi * xi * (1.0 - e) * (1.0 - e) / (2.0 * kappa);
  const double psi = s2 / (m * m);
  ASSERT_GT(psi, 1.5) << "this start value must be in the exponential branch";
  const double p = (psi - 1.0) / (psi + 1.0);
  const std::size_t n = fxval::testing::scaled_paths(400'000);
  std::size_t zeros = 0;
  for (std::size_t i = 0; i < n; ++i) {
    double z = 0.0;
    double unused = 0.0;
    NormalStream(77, i).next_pair(z, unused);
    zeros += model.next_variance(v, dt, z) == 0.0 ? 1U : 0U;
  }
  const double nn = static_cast<double>(n);
  EXPECT_NEAR(static_cast<double>(zeros) / nn, p, 4.0 * std::sqrt(p * (1.0 - p) / nn));
}

TEST(HestonQeTest, VarianceStepFromZeroIsFiniteAndNonNegative) {
  const auto model = make_qe(0.0, 0.2, 0.04, 2.0, -0.7, 1.0);
  for (std::uint64_t i = 0; i < 1000; ++i) {
    double z = 0.0;
    double unused = 0.0;
    NormalStream(3, i).next_pair(z, unused);
    const double next = model.next_variance(0.0, 0.01, z);
    EXPECT_TRUE(std::isfinite(next));
    EXPECT_GE(next, 0.0);
  }
}

// The right tail of the exponential branch: v' = ln((1-p) / (1-U)) / beta with 1-U = 1 - Phi(z).
// If 1-U were computed as 1.0 - U, it would be 0 or a multiple of 2^-53 for z above about 8, so
// the result would stop growing (or become infinite). Computed from erfc it keeps growing with
// z at the exact rate: ln of the Gaussian tail ratio. The check uses the asymptotic Mills-ratio
// expansion of the tail, not the implementation's formula.
TEST(HestonQeTest, ExponentialBranchRightTailStaysAccurateForLargeNormalDraws) {
  const auto model = make_qe(0.0, 0.2, 0.04, 2.0, -0.7, 1.0);
  const double dt = 0.01;
  const auto tail = [](double z) {  // 1 - Phi(z), asymptotic series (error below 1e-4 for z >= 6)
    const double z2 = z * z;
    return std::exp(-0.5 * z2) / (z * std::sqrt(2.0 * std::numbers::pi)) *
           (1.0 - 1.0 / z2 + 3.0 / (z2 * z2) - 15.0 / (z2 * z2 * z2));
  };
  double previous = model.next_variance(0.0, dt, 6.0);
  ASSERT_TRUE(std::isfinite(previous));
  for (const double z : {7.0, 8.0, 8.4}) {
    const double value = model.next_variance(0.0, dt, z);
    ASSERT_TRUE(std::isfinite(value)) << "z = " << z;
    EXPECT_GT(value, previous) << "the variance step must keep growing in the tail, z = " << z;
    previous = value;
  }
  // Ratio check: (v'(z2) - v'(z1)) beta = ln(tail(z1) / tail(z2)); beta is recovered from the
  // two smallest points so the test does not need the implementation's internals.
  const double v6 = model.next_variance(0.0, dt, 6.0);
  const double v7 = model.next_variance(0.0, dt, 7.0);
  const double v8 = model.next_variance(0.0, dt, 8.0);
  const double beta = std::log(tail(6.0) / tail(7.0)) / (v7 - v6);
  EXPECT_NEAR((v8 - v7) * beta, std::log(tail(7.0) / tail(8.0)), 2e-3);
}

TEST(HestonQeTest, SpotStaysPositiveAndVarianceNonNegativeEvenWhenFellerFailsBadly) {
  const auto model = make_qe(0.04, 0.5, 0.04, 1.5, -0.9, 2.0);  // Feller ratio about 0.018
  for (std::uint64_t i = 0; i < 5000; ++i) {
    NormalStream rng(11, i);
    const auto state = model.simulate_terminal_state(rng, 8);
    ASSERT_TRUE(std::isfinite(state.spot));
    ASSERT_GT(state.spot, 0.0);
    ASSERT_GE(state.variance, 0.0);
  }
}

// The martingale correction makes E[S_T] equal the forward for the scheme itself (not just in the
// limit of many steps). 4 standard errors, fixed seed.
TEST(HestonQeTest, TerminalSpotIsAMartingaleWithFewSteps) {
  const double forward = 1.10 * std::exp((0.03 - 0.01) * 2.0);
  const auto identity = [](double x) noexcept {
    return x;
  };
  for (const std::size_t steps : {2U, 8U, 32U}) {
    const auto model = make_qe(0.04, 0.5, 0.04, 1.0, -0.9, 2.0);
    const auto r =
        must(run_monte_carlo(model, identity, make_config(5, scaled_paths(200'000), steps, 2)));
    EXPECT_NEAR(r.mean, forward, 4.0 * r.standard_error) << steps << " steps";
  }
}

TEST(HestonQeTest, SameStreamGivesTheSamePathAndDifferentStreamsDiffer) {
  const auto model = make_qe(0.04, 1.0, 0.04, 0.5, -0.4, 1.0);
  NormalStream a(9, 4);
  NormalStream b(9, 4);
  NormalStream c(9, 5);
  const auto sa = model.simulate_terminal_state(a, 10);
  const auto sb = model.simulate_terminal_state(b, 10);
  const auto sc = model.simulate_terminal_state(c, 10);
  EXPECT_EQ(fxval::testing::bits_of(sa.spot), fxval::testing::bits_of(sb.spot));
  EXPECT_EQ(fxval::testing::bits_of(sa.variance), fxval::testing::bits_of(sb.variance));
  EXPECT_NE(sa.spot, sc.spot);
}

TEST(HestonQeTest, ConsumesExactlyOneRandomBlockPerStep) {
  const auto model = make_qe(0.04, 1.0, 0.04, 0.5, -0.4, 1.0);
  NormalStream rng(9, 4);
  static_cast<void>(model.simulate_terminal_state(rng, 17));
  EXPECT_EQ(rng.blocks_used(), 17U);
}

}  // namespace
