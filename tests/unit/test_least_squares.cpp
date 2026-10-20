#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "fxval/numerics/least_squares.hpp"

namespace {

using fxval::Error;
using fxval::ErrorCode;
using fxval::Result;
using fxval::numerics::finite_difference_jacobian;
using fxval::numerics::LeastSquaresOptions;
using fxval::numerics::LeastSquaresTermination;
using fxval::numerics::levenberg_marquardt;
using fxval::numerics::parameter_standard_errors;
using fxval::numerics::ResidualFunction;
using Vector = std::vector<double>;

TEST(LeastSquaresTest, SolvesALinearProblemExactly) {
  // r = A x - b with a known solution x = (1, -2).
  const ResidualFunction f = [](const Vector& x) -> Result<Vector> {
    return Vector{x[0] + x[1] + 1.0, 2.0 * x[0] - x[1] - 4.0, x[0] - 3.0 * x[1] - 7.0};
  };
  const auto result = levenberg_marquardt(f, {5.0, 5.0}, LeastSquaresOptions{});
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result.value().converged());
  EXPECT_NEAR(result.value().x[0], 1.0, 1e-9);
  EXPECT_NEAR(result.value().x[1], -2.0, 1e-9);
  EXPECT_LT(result.value().cost, 1e-18);
}

TEST(LeastSquaresTest, FitsAnExponentialDecayFromAFarStart) {
  // y = a exp(-b t) sampled without noise: a = 2.5, b = 1.3.
  Vector t, y;
  for (int i = 0; i < 12; ++i) {
    t.push_back(0.25 * i);
    y.push_back(2.5 * std::exp(-1.3 * 0.25 * i));
  }
  const ResidualFunction f = [&](const Vector& x) -> Result<Vector> {
    Vector r(t.size());
    for (std::size_t i = 0; i < t.size(); ++i) {
      r[i] = x[0] * std::exp(-x[1] * t[i]) - y[i];
    }
    return r;
  };
  const auto result = levenberg_marquardt(f, {0.5, 4.0}, LeastSquaresOptions{});
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result.value().converged());
  EXPECT_NEAR(result.value().x[0], 2.5, 1e-8);
  EXPECT_NEAR(result.value().x[1], 1.3, 1e-8);
}

TEST(LeastSquaresTest, SolvesTheRosenbrockResidualForm) {
  const ResidualFunction f = [](const Vector& x) -> Result<Vector> {
    return Vector{10.0 * (x[1] - x[0] * x[0]), 1.0 - x[0]};
  };
  const auto result = levenberg_marquardt(f, {-1.2, 1.0}, LeastSquaresOptions{});
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result.value().converged());
  EXPECT_NEAR(result.value().x[0], 1.0, 1e-8);
  EXPECT_NEAR(result.value().x[1], 1.0, 1e-8);
}

TEST(LeastSquaresTest, ReturnsALeastSquaresSolutionWhenTheDataCannotBeFitted) {
  // Fit a constant to {0, 1, 5}: the minimiser is the mean, the cost is half the sum of squares
  // about the mean.
  const ResidualFunction f = [](const Vector& x) -> Result<Vector> {
    return Vector{x[0] - 0.0, x[0] - 1.0, x[0] - 5.0};
  };
  const auto result = levenberg_marquardt(f, {10.0}, LeastSquaresOptions{});
  ASSERT_TRUE(result.has_value());
  EXPECT_NEAR(result.value().x[0], 2.0, 1e-8);
  EXPECT_NEAR(result.value().cost, 0.5 * (4.0 + 1.0 + 9.0), 1e-9);
}

TEST(LeastSquaresTest, IsInsensitiveToTheScaleOfTheParameters) {
  // Marquardt scaling makes the step independent of the units of each parameter. Here the two
  // parameters differ in sensitivity by eight orders of magnitude; with unscaled damping the
  // insensitive one is damped to a crawl: 18 iterations instead of 10 on this problem.
  const ResidualFunction f = [](const Vector& x) -> Result<Vector> {
    return Vector{1e4 * x[0] - 1e4 * 0.3, 1e-4 * x[1] - 1e-4 * 70.0, 1e-4 * x[0] * x[1]};
  };
  LeastSquaresOptions options;
  options.max_iterations = 60;
  const auto result = levenberg_marquardt(f, {1.0, 1.0}, options);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result.value().converged());
  EXPECT_LT(result.value().iterations, 14);  // measured: 10 scaled, 18 unscaled
}

TEST(LeastSquaresTest, WorksWithFewerResidualsThanUnknowns) {
  // One equation, two unknowns: a whole line of solutions. The optimiser must still reach one.
  const ResidualFunction f = [](const Vector& x) -> Result<Vector> {
    return Vector{x[0] + 2.0 * x[1] - 3.0};
  };
  const auto result = levenberg_marquardt(f, {4.0, -1.0}, LeastSquaresOptions{});
  ASSERT_TRUE(result.has_value());
  EXPECT_LT(result.value().cost, 1e-20);
  EXPECT_NEAR(result.value().x[0] + 2.0 * result.value().x[1], 3.0, 1e-9);
}

TEST(LeastSquaresTest, StartingAtTheSolutionStopsImmediately) {
  const ResidualFunction f = [](const Vector& x) -> Result<Vector> {
    return Vector{x[0] - 1.0, x[1] - 2.0};
  };
  const auto result = levenberg_marquardt(f, {1.0, 2.0}, LeastSquaresOptions{});
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().termination, LeastSquaresTermination::cost_small);
  EXPECT_EQ(result.value().iterations, 0);
}

TEST(LeastSquaresTest, RejectsStepsIntoRegionsWhereTheModelFails) {
  // ln(x) - 1 has its root at e, but the function is undefined for x <= 0. Starting at 10, the
  // first Gauss-Newton step overshoots into the negative axis; it must be rejected and damped,
  // not returned. (Disclosure: an earlier version of this test used sqrt(x) - 2 from 0.5, where
  // the failing region was never visited, so it tested nothing.)
  int failures = 0;
  const ResidualFunction f = [&failures](const Vector& x) -> Result<Vector> {
    if (!(x[0] > 0.0)) {
      ++failures;
      return Error{ErrorCode::numerical_failure, "outside the domain"};
    }
    return Vector{std::log(x[0]) - 1.0};
  };
  const auto result = levenberg_marquardt(f, {10.0}, LeastSquaresOptions{});
  ASSERT_TRUE(result.has_value());
  EXPECT_NEAR(result.value().x[0], std::exp(1.0), 1e-8);
  EXPECT_GT(failures, 0);
}

TEST(LeastSquaresTest, ReportsErrorsAtTheStartAndBadInput) {
  const ResidualFunction failing = [](const Vector&) -> Result<Vector> {
    return Error{ErrorCode::numerical_failure, "nope"};
  };
  const auto at_start = levenberg_marquardt(failing, {1.0}, LeastSquaresOptions{});
  ASSERT_FALSE(at_start.has_value());
  EXPECT_EQ(at_start.error().message, "nope");

  const ResidualFunction fine = [](const Vector& x) -> Result<Vector> {
    return Vector{x[0]};
  };
  EXPECT_FALSE(levenberg_marquardt(fine, {}, LeastSquaresOptions{}).has_value());
  EXPECT_FALSE(levenberg_marquardt(fine, {std::nan("")}, LeastSquaresOptions{}).has_value());
  const ResidualFunction nan_residual = [](const Vector&) -> Result<Vector> {
    return Vector{std::nan("")};
  };
  EXPECT_FALSE(levenberg_marquardt(nan_residual, {1.0}, LeastSquaresOptions{}).has_value());
  const ResidualFunction empty = [](const Vector&) -> Result<Vector> {
    return Vector{};
  };
  EXPECT_FALSE(levenberg_marquardt(empty, {1.0}, LeastSquaresOptions{}).has_value());
}

TEST(LeastSquaresTest, IterationLimitIsReportedAsNotConverged) {
  const ResidualFunction f = [](const Vector& x) -> Result<Vector> {
    return Vector{10.0 * (x[1] - x[0] * x[0]), 1.0 - x[0]};
  };
  LeastSquaresOptions options;
  options.max_iterations = 2;
  const auto result = levenberg_marquardt(f, {-1.2, 1.0}, options);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().termination, LeastSquaresTermination::max_iterations);
  EXPECT_FALSE(result.value().converged());
  EXPECT_STREQ(fxval::numerics::to_string(result.value().termination), "max_iterations");
}

TEST(LeastSquaresTest, FiniteDifferenceJacobianMatchesTheAnalyticOne) {
  const ResidualFunction f = [](const Vector& x) -> Result<Vector> {
    return Vector{x[0] * x[0] * x[1], std::sin(x[0]) + std::exp(x[1]), x[0] - x[1] * x[1]};
  };
  const Vector x{0.7, -0.4};
  int evaluations = 0;
  const auto jacobian = finite_difference_jacobian(f, x, 1e-6, &evaluations);
  ASSERT_TRUE(jacobian.has_value());
  EXPECT_EQ(evaluations, 4);
  const Vector& j = jacobian.value();
  ASSERT_EQ(j.size(), 6U);
  EXPECT_NEAR(j[0], 2.0 * 0.7 * -0.4, 1e-8);
  EXPECT_NEAR(j[1], 0.7 * 0.7, 1e-8);
  EXPECT_NEAR(j[2], std::cos(0.7), 1e-8);
  EXPECT_NEAR(j[3], std::exp(-0.4), 1e-8);
  EXPECT_NEAR(j[4], 1.0, 1e-8);
  EXPECT_NEAR(j[5], 0.8, 1e-8);
}

TEST(LeastSquaresTest, StandardErrorsMatchTheClassicalRegressionFormula) {
  // Fit y = a + b t: Var(b) = sigma^2 / sum (t - mean)^2, Var(a) = sigma^2 (1/m + mean^2/Stt).
  const std::size_t m = 6;
  Vector jacobian;
  double mean = 0.0;
  for (std::size_t i = 0; i < m; ++i) {
    mean += static_cast<double>(i) / static_cast<double>(m);
  }
  double stt = 0.0;
  for (std::size_t i = 0; i < m; ++i) {
    jacobian.push_back(1.0);
    jacobian.push_back(static_cast<double>(i));
    stt += (static_cast<double>(i) - mean * static_cast<double>(m) / static_cast<double>(m)) * 0.0;
  }
  const double t_mean = 2.5;
  stt = 0.0;
  for (std::size_t i = 0; i < m; ++i) {
    stt += (static_cast<double>(i) - t_mean) * (static_cast<double>(i) - t_mean);
  }
  const double sigma2 = 0.04;
  const Vector se = parameter_standard_errors(jacobian, m, 2, sigma2);
  EXPECT_NEAR(se[1], std::sqrt(sigma2 / stt), 1e-12);
  EXPECT_NEAR(se[0], std::sqrt(sigma2 * (1.0 / 6.0 + t_mean * t_mean / stt)), 1e-12);
}

TEST(LeastSquaresTest, StandardErrorsAreInfiniteWhenTheDataDoNotDetermineTheParameters) {
  const double inf = std::numeric_limits<double>::infinity();
  // Fewer rows than unknowns.
  for (double se : parameter_standard_errors({1.0, 2.0, 3.0, 4.0}, 2, 2, 1.0)) {
    EXPECT_EQ(se, inf);
  }
  // Two identical columns: J'J is singular although m > n.
  for (double se : parameter_standard_errors({1.0, 1.0, 2.0, 2.0, 3.0, 3.0}, 3, 2, 1.0)) {
    EXPECT_EQ(se, inf);
  }
  // A bad variance or a size mismatch also gives infinity rather than a number.
  for (double se : parameter_standard_errors({1.0, 0.0, 0.0, 1.0, 1.0, 1.0}, 3, 2, std::nan(""))) {
    EXPECT_EQ(se, inf);
  }
  for (double se : parameter_standard_errors({1.0, 0.0}, 3, 2, 1.0)) {
    EXPECT_EQ(se, inf);
  }
}

}  // namespace
