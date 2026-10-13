// Unit tests for the Heston pricer: parameter validation, limits and identities that must hold
// exactly, error paths and the qualitative smile. They protect the code from regressions. The
// evidence that the prices are right is in tests/validation/ (V-03, V-04, HESTON-REF).

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <limits>

#include "fxval/models/heston.hpp"
#include "support/heston_test_support.hpp"

namespace {

using fxval::ErrorCode;
using fxval::models::expected_integrated_variance;
using fxval::models::heston_price;
using fxval::models::HestonParams;
using fxval::models::log_forward_characteristic_function;
using fxval::pricing::OptionType;
using fxval::testing::gk_call;
using fxval::testing::heston_value;
using fxval::testing::implied_vol_of_call;
using fxval::testing::make_heston_market;
using fxval::testing::make_params;
using fxval::testing::must;
using Complex = std::complex<double>;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

TEST(HestonParamsTest, AcceptsValidParametersAndKeepsTheValues) {
  const auto p = must(HestonParams::create(0.04, 2.0, 0.05, 0.3, -0.5));
  EXPECT_DOUBLE_EQ(p.v0(), 0.04);
  EXPECT_DOUBLE_EQ(p.kappa(), 2.0);
  EXPECT_DOUBLE_EQ(p.theta(), 0.05);
  EXPECT_DOUBLE_EQ(p.xi(), 0.3);
  EXPECT_DOUBLE_EQ(p.rho(), -0.5);
}

TEST(HestonParamsTest, AcceptsTheBoundaryValues) {
  EXPECT_TRUE(HestonParams::create(0.0, 1.0, 0.04, 0.0, -1.0).has_value());
  EXPECT_TRUE(HestonParams::create(25.0, 100.0, 25.0, 10.0, 1.0).has_value());
}

TEST(HestonParamsTest, RejectsEveryOutOfRangeParameterAndNamesIt) {
  struct Case {
    double v0, kappa, theta, xi, rho;
    const char* name;
  };
  const Case bad[] = {
      {-0.01, 1, 0.04, 0.3, 0, "v0"},
      {kNaN, 1, 0.04, 0.3, 0, "v0"},
      {26.0, 1, 0.04, 0.3, 0, "v0"},
      {0.04, 0.0, 0.04, 0.3, 0, "kappa"},
      {0.04, -1, 0.04, 0.3, 0, "kappa"},
      {0.04, 101, 0.04, 0.3, 0, "kappa"},
      {0.04, 1, 0.0, 0.3, 0, "theta"},
      {0.04, 1, kNaN, 0.3, 0, "theta"},
      {0.04, 1, 0.04, -0.1, 0, "xi"},
      {0.04, 1, 0.04, 11.0, 0, "xi"},
      {0.04, 1, 0.04, 0.3, 1.01, "rho"},
      {0.04, 1, 0.04, 0.3, kNaN, "rho"},
  };
  for (const auto& c : bad) {
    const auto p = HestonParams::create(c.v0, c.kappa, c.theta, c.xi, c.rho);
    ASSERT_FALSE(p.has_value()) << c.name;
    EXPECT_EQ(p.error().code, ErrorCode::invalid_argument);
    EXPECT_NE(p.error().message.find(c.name), std::string::npos) << p.error().message;
  }
}

TEST(HestonParamsTest, FellerCondition) {
  EXPECT_TRUE(make_params(0.04, 2.0, 0.04, 0.3, 0.0).feller_satisfied());   // 0.16 >= 0.09
  EXPECT_TRUE(make_params(0.04, 1.0, 0.045, 0.3, 0.0).feller_satisfied());  // 2 k theta == xi^2
  EXPECT_FALSE(make_params(0.04, 1.0, 0.04, 1.0, 0.0).feller_satisfied());  // 0.08 < 1
}

TEST(HestonVariance, ExpectedIntegratedVarianceLimits) {
  const auto p = make_params(0.09, 2.0, 0.04, 0.5, 0.0);
  EXPECT_EQ(expected_integrated_variance(p, 0.0), 0.0);
  // Short time: v0 * tau.
  EXPECT_NEAR(expected_integrated_variance(p, 1e-6), 0.09e-6, 1e-12);
  // Long time: theta * tau plus a constant (v0 - theta) / kappa.
  EXPECT_NEAR(expected_integrated_variance(p, 100.0), 0.04 * 100.0 + 0.05 / 2.0, 1e-12);
  // Starting at the long-run level it is exactly theta * tau.
  EXPECT_NEAR(
      expected_integrated_variance(make_params(0.04, 3.0, 0.04, 0.5, 0.0), 2.5), 0.1, 1e-15);
}

TEST(HestonCharacteristicFunction, EqualsOneAtZeroAndAtMinusI) {
  // phi(0) = 1 (total probability) and phi(-i) = E[e^X] = E[S_T / F] = 1 (the forward is the
  // expected spot: the martingale property).
  for (double xi : {0.0, 0.3, 1.5, 4.0}) {
    for (double tau : {0.01, 1.0, 10.0, 30.0}) {
      const auto p = make_params(0.04, 1.5, 0.06, xi, -0.6);
      const Complex at_zero = log_forward_characteristic_function(p, tau, Complex(0.0, 0.0));
      const Complex at_minus_i = log_forward_characteristic_function(p, tau, Complex(0.0, -1.0));
      EXPECT_NEAR(std::abs(at_zero - 1.0), 0.0, 1e-14) << "xi=" << xi << " tau=" << tau;
      EXPECT_NEAR(std::abs(at_minus_i - 1.0), 0.0, 1e-12) << "xi=" << xi << " tau=" << tau;
    }
  }
}

TEST(HestonCharacteristicFunction, IsABoundedHermitianFunctionOnTheRealLine) {
  // |phi(u)| <= 1 and phi(-u) = conj(phi(u)) for any characteristic function of a real variable.
  const auto p = make_params(0.04, 1.0, 0.09, 1.2, -0.7);
  for (double u : {0.1, 1.0, 5.0, 25.0, 100.0}) {
    const Complex plus = log_forward_characteristic_function(p, 2.0, Complex(u, 0.0));
    const Complex minus = log_forward_characteristic_function(p, 2.0, Complex(-u, 0.0));
    EXPECT_LE(std::abs(plus), 1.0 + 1e-14) << u;
    EXPECT_NEAR(std::abs(plus - std::conj(minus)), 0.0, 1e-14) << u;
  }
}

TEST(HestonCharacteristicFunction, FirstMomentIsMinusHalfTheExpectedIntegratedVariance) {
  // X = ln(S_T / F) has E[X] = -E[integral of v] / 2, and phi'(0) = i E[X]. The derivative is
  // taken by a central difference, so this checks the function without using the price code.
  const auto p = make_params(0.03, 1.8, 0.05, 0.7, -0.4);
  const double tau = 1.5;
  const double h = 1e-4;
  const Complex phi_plus = log_forward_characteristic_function(p, tau, Complex(h, 0.0));
  const Complex phi_minus = log_forward_characteristic_function(p, tau, Complex(-h, 0.0));
  const Complex derivative = (phi_plus - phi_minus) / (2.0 * h);
  EXPECT_NEAR(derivative.imag(), -0.5 * expected_integrated_variance(p, tau), 1e-7);
  EXPECT_NEAR(derivative.real(), 0.0, 1e-7);
}

TEST(HestonPrice, ZeroVolOfVolIsExactlyBlackScholesWithTheExpectedTotalVariance) {
  const auto p = make_params(0.04, 2.0, 0.05, 0.0, -0.5);
  const double tau = 1.0;
  const double sigma = std::sqrt(expected_integrated_variance(p, tau) / tau);
  for (double strike : {0.8, 1.0, 1.1, 1.3, 2.0}) {
    EXPECT_NEAR(heston_value(OptionType::call, 1.1, strike, 0.03, 0.01, tau, p),
                gk_call(1.1, strike, 0.03, 0.01, tau, sigma),
                1e-13)
        << "strike=" << strike;
  }
}

TEST(HestonPrice, PutCallParity) {
  const auto p = make_params(0.05, 1.2, 0.04, 0.8, -0.6);
  for (double strike : {0.7, 1.0, 1.2, 2.0}) {
    const double call = heston_value(OptionType::call, 1.1, strike, 0.04, 0.01, 2.0, p);
    const double put = heston_value(OptionType::put, 1.1, strike, 0.04, 0.01, 2.0, p);
    const double rhs = 1.1 * std::exp(-0.01 * 2.0) - strike * std::exp(-0.04 * 2.0);
    EXPECT_NEAR(call - put, rhs, 1e-13) << "strike=" << strike;
  }
}

TEST(HestonPrice, ZeroExpiryIsIntrinsicValue) {
  const auto p = make_params(0.04, 2.0, 0.05, 0.5, -0.5);
  EXPECT_DOUBLE_EQ(heston_value(OptionType::call, 1.20, 1.00, 0.03, 0.01, 0.0, p), 0.20);
  EXPECT_EQ(heston_value(OptionType::call, 0.90, 1.00, 0.03, 0.01, 0.0, p), 0.0);
  EXPECT_DOUBLE_EQ(heston_value(OptionType::put, 0.90, 1.00, 0.03, 0.01, 0.0, p), 0.10);
}

TEST(HestonPrice, PricesStayInsideTheNoArbitrageBounds) {
  const auto p = make_params(0.04, 2.0, 0.05, 0.5, -0.5);
  const double s = 1.1, rd = 0.03, rf = 0.01, tau = 1.0;
  const double forward = s * std::exp((rd - rf) * tau);
  for (double strike : {0.5, 0.9, 1.1, 1.5, 3.0}) {
    const double call = heston_value(OptionType::call, s, strike, rd, rf, tau, p);
    EXPECT_GE(call, std::max(0.0, std::exp(-rd * tau) * (forward - strike)) - 1e-14);
    EXPECT_LE(call, s * std::exp(-rf * tau) + 1e-14);
  }
}

TEST(HestonPrice, CallIsDecreasingAndConvexInTheStrike) {
  const auto p = make_params(0.04, 2.0, 0.05, 0.6, -0.5);
  double previous_price = 0.0, previous_slope = -1e9;
  bool first = true;
  const double strikes[] = {0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.4};
  double previous_strike = 0.0;
  for (double k : strikes) {
    const double price = heston_value(OptionType::call, 1.1, k, 0.03, 0.01, 1.0, p);
    if (!first) {
      const double slope = (price - previous_price) / (k - previous_strike);
      EXPECT_LT(slope, 0.0) << "k=" << k;
      EXPECT_GT(slope, previous_slope - 1e-12) << "convexity violated at k=" << k;
      previous_slope = slope;
    }
    previous_price = price;
    previous_strike = k;
    first = false;
  }
}

TEST(HestonPrice, TheSmileHasTheSignOfRho) {
  // Negative correlation: low strikes are more expensive in implied-vol terms (downward skew);
  // positive correlation reverses it; zero correlation gives a symmetric-ish smile (a "smirk" of
  // equal wings that is above the money, because vol of vol creates curvature).
  const double s = 1.1, rd = 0.02, rf = 0.02, tau = 1.0;  // equal rates: F = S
  const auto smile = [&](double rho, double strike) {
    const auto p = make_params(0.04, 2.0, 0.04, 0.6, rho);
    return implied_vol_of_call(
        heston_value(OptionType::call, s, strike, rd, rf, tau, p), s, strike, rd, rf, tau);
  };
  EXPECT_GT(smile(-0.7, 0.9), smile(-0.7, 1.3));
  EXPECT_LT(smile(0.7, 0.9), smile(0.7, 1.3));
  EXPECT_GT(smile(0.0, 0.9), smile(0.0, 1.1));  // wings above the money
  EXPECT_GT(smile(0.0, 1.3), smile(0.0, 1.1));
}

TEST(HestonPrice, ReportsAnOverflowingForward) {
  const auto p = make_params(0.04, 2.0, 0.05, 0.5, -0.5);
  const auto market = make_heston_market(1e308, 1.0, 1.0, 0.0, 10.0);
  const auto price = heston_price(market, p, OptionType::call);
  ASSERT_FALSE(price.has_value());
  EXPECT_EQ(price.error().code, ErrorCode::numerical_failure);
}

TEST(HestonPrice, ExtremeButValidInputsGiveFinitePrices) {
  // Very short expiry with high vol of vol; very long expiry with a Feller violation.
  EXPECT_TRUE(std::isfinite(heston_value(
      OptionType::call, 1.0, 1.0, 0.0, 0.0, 1e-6, make_params(0.04, 1.0, 0.04, 5.0, -0.9))));
  EXPECT_TRUE(std::isfinite(heston_value(
      OptionType::put, 1.0, 1.0, 0.05, 0.0, 30.0, make_params(0.04, 0.05, 0.04, 3.0, 0.9))));
}

TEST(HestonPrice, IsMonotoneInSpotAcrossTheMoney) {
  const auto p = make_params(0.04, 2.0, 0.05, 0.5, -0.5);
  double previous = 0.0;
  for (double s : {0.9, 1.0, 1.1, 1.2, 1.3}) {
    const double call = heston_value(OptionType::call, s, 1.1, 0.03, 0.01, 1.0, p);
    EXPECT_GT(call, previous) << "s=" << s;
    previous = call;
  }
}

}  // namespace
