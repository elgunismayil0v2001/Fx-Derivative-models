// Unit tests of EWMA and GARCH: recursions by hand, parameter validation, recovery, errors.

#include <gtest/gtest.h>

#include <cmath>

#include "fxval/timeseries/volatility_models.hpp"
#include "support/risk_test_support.hpp"

namespace {

using fxval::ErrorCode;
using fxval::testing::Innovations;
using fxval::testing::must;
using fxval::timeseries::GarchModel;
using fxval::timeseries::GarchParams;

TEST(EwmaTest, MatchesTheRecursionByHand) {
  const std::vector<double> r{0.01, -0.02, 0.005};
  const auto v = must(fxval::timeseries::ewma_variance(r, 0.9));
  ASSERT_EQ(v.size(), 4U);
  const double v0 = (1e-4 + 4e-4 + 2.5e-5) / 3.0;
  EXPECT_NEAR(v[0], v0, 1e-18);
  EXPECT_NEAR(v[1], 0.9 * v0 + 0.1 * 1e-4, 1e-18);
  EXPECT_NEAR(v[2], 0.9 * v[1] + 0.1 * 4e-4, 1e-18);
  EXPECT_NEAR(v[3], 0.9 * v[2] + 0.1 * 2.5e-5, 1e-18);
}

TEST(EwmaTest, ErrorPaths) {
  const std::vector<double> r{0.01, 0.02};
  EXPECT_FALSE(fxval::timeseries::ewma_variance(r, 0.0).has_value());
  EXPECT_FALSE(fxval::timeseries::ewma_variance(r, 1.0).has_value());
  EXPECT_FALSE(fxval::timeseries::ewma_variance({}, 0.94).has_value());
  const std::vector<double> bad{0.01, std::nan("")};
  EXPECT_FALSE(fxval::timeseries::ewma_variance(bad, 0.94).has_value());
  const std::vector<double> zeros(10, 0.0);
  EXPECT_FALSE(fxval::timeseries::ewma_variance(zeros, 0.94).has_value());
}

TEST(GarchTest, VariancePathAndLikelihoodByHand) {
  const std::vector<double> r{0.01, -0.02};
  const GarchParams p{1e-6, 0.05, 0.10, 0.85};
  const auto path = must(fxval::timeseries::garch_variance_path(r, GarchModel::gjr, p));
  const double v0 = (1e-4 + 4e-4) / 2.0;
  const double v1 = 1e-6 + 0.05 * 1e-4 + 0.85 * v0;           // r0 > 0: no gamma
  const double v2 = 1e-6 + (0.05 + 0.10) * 4e-4 + 0.85 * v1;  // r1 < 0: gamma
  ASSERT_EQ(path.size(), 3U);
  EXPECT_NEAR(path[1], v1, 1e-18);
  EXPECT_NEAR(path[2], v2, 1e-18);
  const double two_pi = 2.0 * 3.141592653589793238462643383279502884;
  const double ll = -0.5 * (std::log(two_pi) + std::log(v0) + 1e-4 / v0) -
                    0.5 * (std::log(two_pi) + std::log(v1) + 4e-4 / v1);
  EXPECT_NEAR(must(fxval::timeseries::garch_log_likelihood(r, GarchModel::gjr, p)), ll, 1e-12);
}

TEST(GarchTest, ParameterValidation) {
  const std::vector<double> r{0.01, -0.02, 0.005};
  const auto bad = [&](GarchModel m, GarchParams p) {
    return fxval::timeseries::garch_variance_path(r, m, p).has_value();
  };
  EXPECT_FALSE(bad(GarchModel::garch, {0.0, 0.05, 0.0, 0.9}));    // omega
  EXPECT_FALSE(bad(GarchModel::garch, {1e-6, -0.01, 0.0, 0.9}));  // alpha
  EXPECT_FALSE(bad(GarchModel::garch, {1e-6, 0.05, 0.0, 0.96}));  // persistence 1.01
  EXPECT_FALSE(bad(GarchModel::garch, {1e-6, 0.05, 0.02, 0.9}));  // gamma in the symmetric model
  EXPECT_FALSE(bad(GarchModel::gjr, {1e-6, 0.05, 0.2, 0.9}));     // persistence 1.05
  EXPECT_TRUE(bad(GarchModel::gjr, {1e-6, 0.03, 0.04, 0.9}));
}

TEST(GarchTest, FitRecoversParametersFromALongSimulatedSeries) {
  const GarchParams truth{4e-7, 0.07, 0.0, 0.91};
  const auto r = fxval::testing::simulate_garch_returns(20000, truth, Innovations::normal, 11);
  const auto fit = must(fxval::timeseries::fit_garch(r, GarchModel::garch));
  EXPECT_NEAR(fit.params.alpha, 0.07, 4.0 * fit.std_errors[1]);
  EXPECT_NEAR(fit.params.beta, 0.91, 4.0 * fit.std_errors[3]);
  EXPECT_NEAR(fit.params.persistence(), 0.98, 0.01);
  EXPECT_EQ(fit.variance_path.size(), r.size() + 1);
  EXPECT_TRUE(fit.converged);
  EXPECT_EQ(fit.std_errors[2], 0.0);  // symmetric model: no gamma
}

TEST(GarchTest, FitIsDeterministicAndRespectsTheConstraints) {
  const GarchParams truth{4e-7, 0.07, 0.0, 0.91};
  const auto r = fxval::testing::simulate_garch_returns(1500, truth, Innovations::student_t6, 12);
  const auto a = must(fxval::timeseries::fit_garch(r, GarchModel::gjr));
  const auto b = must(fxval::timeseries::fit_garch(r, GarchModel::gjr));
  EXPECT_EQ(a.log_likelihood, b.log_likelihood);
  EXPECT_EQ(a.params.omega, b.params.omega);
  EXPECT_GT(a.params.omega, 0.0);
  EXPECT_GE(a.params.alpha, 0.0);
  EXPECT_GE(a.params.gamma, 0.0);
  EXPECT_GE(a.params.beta, 0.0);
  EXPECT_LT(a.params.persistence(), 1.0);
}

TEST(GarchTest, FitErrorPaths) {
  const std::vector<double> few(50, 0.01);
  EXPECT_EQ(fxval::timeseries::fit_garch(few, GarchModel::garch).error().code,
            ErrorCode::invalid_argument);
  std::vector<double> zeros(200, 0.0);
  EXPECT_FALSE(fxval::timeseries::fit_garch(zeros, GarchModel::garch).has_value());
  std::vector<double> bad(200, 0.01);
  bad[10] = std::nan("");
  EXPECT_FALSE(fxval::timeseries::fit_garch(bad, GarchModel::garch).has_value());
}

TEST(GarchTest, StandardisedResiduals) {
  const std::vector<double> r{0.01, -0.02};
  const std::vector<double> v{1e-4, 4e-4, 9e-4};
  const auto z = must(fxval::timeseries::standardised_residuals(r, v));
  EXPECT_NEAR(z[0], 1.0, 1e-12);
  EXPECT_NEAR(z[1], -1.0, 1e-12);
  EXPECT_FALSE(
      fxval::timeseries::standardised_residuals(r, std::vector<double>{1e-4, 4e-4}).has_value());
  EXPECT_FALSE(fxval::timeseries::standardised_residuals(r, std::vector<double>{1e-4, 0.0, 1e-4})
                   .has_value());
}

}  // namespace
