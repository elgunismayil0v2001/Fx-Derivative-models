#include <gtest/gtest.h>

#include <cmath>

#include "fxval/numerics/brent.hpp"

namespace {

using fxval::ErrorCode;
using fxval::Result;
using fxval::numerics::brent_root;

TEST(BrentTest, FindsSimpleRoots) {
  const auto square = [](double x) -> Result<double> {
    return x * x - 2.0;
  };
  const auto root = brent_root(square, 0.0, 2.0);
  ASSERT_TRUE(root.has_value());
  EXPECT_NEAR(root.value(), std::sqrt(2.0), 1e-14);

  const auto cubic = [](double x) -> Result<double> {
    return x * x * x - x - 2.0;
  };
  EXPECT_NEAR(brent_root(cubic, 1.0, 2.0).value(), 1.5213797068045676, 1e-13);
}

TEST(BrentTest, ReturnsAnEndpointThatIsAlreadyARoot) {
  const auto f = [](double x) -> Result<double> {
    return x - 1.0;
  };
  EXPECT_EQ(brent_root(f, 1.0, 3.0).value(), 1.0);
  EXPECT_EQ(brent_root(f, -1.0, 1.0).value(), 1.0);
}

TEST(BrentTest, HandlesFunctionsThatAreFlatNearTheRoot) {
  // (x - 1)^3 has a triple root: bisection-like behaviour must still converge.
  const auto f = [](double x) -> Result<double> {
    return std::pow(x - 1.0, 3.0);
  };
  EXPECT_NEAR(brent_root(f, 0.0, 3.0).value(), 1.0, 1e-4);
}

TEST(BrentTest, HandlesSteepAndDecreasingFunctions) {
  const auto steep = [](double x) -> Result<double> {
    return std::exp(50.0 * x) - 1e5;
  };
  EXPECT_NEAR(brent_root(steep, 0.0, 1.0).value(), std::log(1e5) / 50.0, 1e-13);
  const auto down = [](double x) -> Result<double> {
    return 1.0 - std::exp(x);
  };
  EXPECT_NEAR(brent_root(down, -1.0, 2.0).value(), 0.0, 1e-13);
}

TEST(BrentTest, ConvergesFasterThanBisection) {
  int calls = 0;
  const auto f = [&calls](double x) -> Result<double> {
    ++calls;
    return std::cos(x) - x;
  };
  EXPECT_NEAR(brent_root(f, 0.0, 1.0, 1e-14).value(), 0.7390851332151607, 1e-13);
  EXPECT_LT(calls, 15);  // bisection to 1e-14 needs about 47
}

TEST(BrentTest, RejectsBadBrackets) {
  const auto f = [](double x) -> Result<double> {
    return x * x + 1.0;
  };
  const auto same_sign = brent_root(f, -1.0, 1.0);
  ASSERT_FALSE(same_sign.has_value());
  EXPECT_EQ(same_sign.error().code, ErrorCode::invalid_argument);
  EXPECT_FALSE(brent_root(f, 1.0, 1.0).has_value());
  EXPECT_FALSE(brent_root(f, 2.0, 1.0).has_value());
  EXPECT_FALSE(brent_root(f, std::nan(""), 1.0).has_value());
  EXPECT_FALSE(brent_root(f, 0.0, 1.0, 0.0).has_value());
}

TEST(BrentTest, ReportsNonFiniteValuesAndFunctionErrors) {
  const auto nan_fn = [](double x) -> Result<double> {
    return x > 0.5 ? std::nan("") : x - 0.7;
  };
  const auto nan_result = brent_root(nan_fn, 0.0, 1.0);
  ASSERT_FALSE(nan_result.has_value());
  EXPECT_EQ(nan_result.error().code, ErrorCode::numerical_failure);

  const auto failing = [](double) -> Result<double> {
    return fxval::Error{ErrorCode::numerical_failure, "boom"};
  };
  const auto failed = brent_root(failing, 0.0, 1.0);
  ASSERT_FALSE(failed.has_value());
  EXPECT_EQ(failed.error().message, "boom");

  // Fails only in the interior: the error from inside the search is returned unchanged.
  const auto interior = [](double x) -> Result<double> {
    if (x > 0.1 && x < 0.9) {
      return fxval::Error{ErrorCode::invalid_argument, "interior"};
    }
    return x - 0.5;
  };
  const auto interior_result = brent_root(interior, 0.0, 1.0);
  ASSERT_FALSE(interior_result.has_value());
  EXPECT_EQ(interior_result.error().message, "interior");
}

TEST(BrentTest, ReportsNotConvergedWhenTheIterationLimitIsTooLow) {
  const auto f = [](double x) -> Result<double> {
    return std::cos(x) - x;
  };
  const auto result = brent_root(f, 0.0, 1.0, 1e-14, 2);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::not_converged);
}

}  // namespace
