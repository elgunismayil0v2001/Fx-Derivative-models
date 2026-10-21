// Unit tests of the Nelder-Mead minimiser: known minima, restarts, failure handling.

#include <gtest/gtest.h>

#include <cmath>

#include "fxval/numerics/nelder_mead.hpp"

namespace {

using fxval::ErrorCode;
using fxval::numerics::nelder_mead;
using fxval::numerics::ObjectiveFunction;

TEST(NelderMeadTest, FindsTheMinimumOfAQuadratic) {
  const ObjectiveFunction f = [](const std::vector<double>& x) -> fxval::Result<double> {
    return (x[0] - 1.5) * (x[0] - 1.5) + 3.0 * (x[1] + 0.5) * (x[1] + 0.5);
  };
  const auto r = nelder_mead(f, {0.0, 0.0});
  ASSERT_TRUE(r.has_value());
  EXPECT_NEAR(r.value().x[0], 1.5, 1e-6);
  EXPECT_NEAR(r.value().x[1], -0.5, 1e-6);
  EXPECT_NEAR(r.value().value, 0.0, 1e-12);
  EXPECT_TRUE(r.value().converged);
}

TEST(NelderMeadTest, FindsTheRosenbrockValley) {
  const ObjectiveFunction f = [](const std::vector<double>& x) -> fxval::Result<double> {
    const double a = 1.0 - x[0];
    const double b = x[1] - x[0] * x[0];
    return a * a + 100.0 * b * b;
  };
  const auto r = nelder_mead(f, {-1.2, 1.0});
  ASSERT_TRUE(r.has_value());
  EXPECT_NEAR(r.value().x[0], 1.0, 1e-4);
  EXPECT_NEAR(r.value().x[1], 1.0, 1e-4);
}

TEST(NelderMeadTest, AnErrorFromTheObjectiveIsTreatedAsInfinity) {
  // The objective refuses x < 0; the minimum of (x - 2)^2 is at 2, away from the forbidden region.
  const ObjectiveFunction f = [](const std::vector<double>& x) -> fxval::Result<double> {
    if (x[0] < 0.0) {
      return fxval::Error{ErrorCode::numerical_failure, "forbidden"};
    }
    return (x[0] - 2.0) * (x[0] - 2.0);
  };
  const auto r = nelder_mead(f, {1.0});
  ASSERT_TRUE(r.has_value());
  EXPECT_NEAR(r.value().x[0], 2.0, 1e-6);
}

TEST(NelderMeadTest, ErrorPaths) {
  const ObjectiveFunction f = [](const std::vector<double>& x) -> fxval::Result<double> {
    return x[0] * x[0];
  };
  EXPECT_EQ(nelder_mead(f, {}).error().code, ErrorCode::invalid_argument);
  EXPECT_EQ(nelder_mead(f, {std::nan("")}).error().code, ErrorCode::invalid_argument);
  const ObjectiveFunction broken = [](const std::vector<double>&) -> fxval::Result<double> {
    return fxval::Error{ErrorCode::numerical_failure, "never"};
  };
  EXPECT_EQ(nelder_mead(broken, {1.0}).error().code, ErrorCode::numerical_failure);
}

}  // namespace
