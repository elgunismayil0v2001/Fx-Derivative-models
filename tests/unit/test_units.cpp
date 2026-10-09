#include <gtest/gtest.h>

#include <concepts>
#include <limits>
#include <type_traits>

#include "fxval/core/limits.hpp"
#include "fxval/core/units.hpp"

namespace {

using fxval::ErrorCode;
using fxval::Notional;
using fxval::Rate;
using fxval::Strike;
using fxval::Volatility;
using fxval::Years;

constexpr double nan = std::numeric_limits<double>::quiet_NaN();
constexpr double inf = std::numeric_limits<double>::infinity();

// ---- Compile-time guarantees: units cannot be mixed up or built from raw doubles ----
template <class A, class B>
concept EqualityComparable = requires(A a, B b) { a == b; };

static_assert(EqualityComparable<Volatility, Volatility>);
static_assert(!EqualityComparable<Volatility, Rate>);
static_assert(!EqualityComparable<Strike, Notional>);
static_assert(!std::is_convertible_v<double, Volatility>);
static_assert(!std::is_convertible_v<Volatility, double>);
static_assert(!std::is_constructible_v<Volatility, double>);
static_assert(!std::is_constructible_v<Rate, Volatility>);

template <class T>
void expect_invalid(const fxval::Result<T>& r) {
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::invalid_argument);
  EXPECT_FALSE(r.error().message.empty());
}

// ---- Volatility ----
TEST(Volatility, DecimalRoundTrips) {
  const auto v = Volatility::from_decimal(0.08);
  ASSERT_TRUE(v.has_value());
  EXPECT_DOUBLE_EQ(v.value().value(), 0.08);
  EXPECT_DOUBLE_EQ(v.value().percent(), 8.0);
}

TEST(Volatility, PercentInputIsConvertedToDecimal) {
  const auto v = Volatility::from_percent(8.0);
  ASSERT_TRUE(v.has_value());
  EXPECT_DOUBLE_EQ(v.value().value(), 0.08);
}

TEST(Volatility, AcceptsBoundaryValues) {
  EXPECT_TRUE(Volatility::from_decimal(0.0).has_value());
  EXPECT_TRUE(Volatility::from_decimal(fxval::limits::max_volatility).has_value());
}

TEST(Volatility, RejectsNonFiniteNegativeAndUnitMistakes) {
  expect_invalid(Volatility::from_decimal(nan));
  expect_invalid(Volatility::from_decimal(inf));
  expect_invalid(Volatility::from_decimal(-inf));
  expect_invalid(Volatility::from_decimal(-0.01));
  // 8 passed as a decimal almost certainly means 8%: caught by the sanity bound.
  expect_invalid(Volatility::from_decimal(8.0));
  expect_invalid(Volatility::from_percent(nan));
  expect_invalid(Volatility::from_percent(-5.0));
  expect_invalid(Volatility::from_percent(1000.0));
}

TEST(Volatility, ErrorMessageNamesTheQuantityAndTheValue) {
  const auto v = Volatility::from_decimal(8.0);
  ASSERT_FALSE(v.has_value());
  EXPECT_NE(v.error().message.find("volatility"), std::string::npos);
  EXPECT_NE(v.error().message.find('8'), std::string::npos);
}

TEST(Volatility, OrdersByValue) {
  const auto low = Volatility::from_decimal(0.05).value();
  const auto high = Volatility::from_decimal(0.10).value();
  EXPECT_LT(low, high);
  EXPECT_EQ(low, Volatility::from_percent(5.0).value());
}

// ---- Rate ----
TEST(Rate, AllowsNegativeRates) {
  const auto r = Rate::from_percent(-0.5);
  ASSERT_TRUE(r.has_value());
  EXPECT_DOUBLE_EQ(r.value().value(), -0.005);
  EXPECT_DOUBLE_EQ(r.value().percent(), -0.5);
}

TEST(Rate, RejectsNonFiniteAndUnitMistakes) {
  expect_invalid(Rate::from_decimal(nan));
  expect_invalid(Rate::from_decimal(inf));
  expect_invalid(Rate::from_decimal(5.0));  // 5 meaning 5%
  expect_invalid(Rate::from_decimal(-5.0));
  expect_invalid(Rate::from_percent(nan));
  expect_invalid(Rate::from_percent(500.0));
}

// ---- Years ----
TEST(Years, AcceptsZeroAndTheMaximum) {
  EXPECT_TRUE(Years::from_years(0.0).has_value());
  EXPECT_TRUE(Years::from_years(fxval::limits::max_years).has_value());
  EXPECT_DOUBLE_EQ(Years::from_years(0.25).value().value(), 0.25);
}

TEST(Years, RejectsNegativeNonFiniteAndHuge) {
  expect_invalid(Years::from_years(-1e-9));
  expect_invalid(Years::from_years(nan));
  expect_invalid(Years::from_years(inf));
  expect_invalid(Years::from_years(fxval::limits::max_years + 1.0));
}

// ---- Strike and Notional ----
TEST(Strike, MustBeStrictlyPositiveAndFinite) {
  EXPECT_DOUBLE_EQ(Strike::from_value(1.0850).value().value(), 1.0850);
  expect_invalid(Strike::from_value(0.0));
  expect_invalid(Strike::from_value(-1.0));
  expect_invalid(Strike::from_value(nan));
  expect_invalid(Strike::from_value(inf));
}

TEST(Notional, MustBeStrictlyPositiveAndFinite) {
  EXPECT_DOUBLE_EQ(Notional::from_value(1e6).value().value(), 1e6);
  expect_invalid(Notional::from_value(0.0));
  expect_invalid(Notional::from_value(-1e6));
  expect_invalid(Notional::from_value(nan));
  expect_invalid(Notional::from_value(inf));
}

}  // namespace
