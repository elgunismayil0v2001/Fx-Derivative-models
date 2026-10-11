#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <variant>

#include "fxval/core/error.hpp"

namespace {

using fxval::Error;
using fxval::ErrorCode;
using fxval::Result;

static_assert(!std::is_default_constructible_v<Result<int>>,
              "A Result must always hold either a value or an error");

Result<int> parse_positive(int x) {
  if (x <= 0) {
    return Error{ErrorCode::invalid_argument, "x must be positive"};
  }
  return x;
}

TEST(Result, HoldsAValue) {
  const auto r = parse_positive(7);
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(static_cast<bool>(r));
  EXPECT_EQ(r.value(), 7);
}

TEST(Result, HoldsAnError) {
  const auto r = parse_positive(-1);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::invalid_argument);
  EXPECT_EQ(r.error().message, "x must be positive");
}

TEST(Result, ValueOrReturnsFallbackOnlyOnError) {
  EXPECT_EQ(parse_positive(5).value_or(-9), 5);
  EXPECT_EQ(parse_positive(0).value_or(-9), -9);
}

TEST(Result, SupportsMoveOnlyValues) {
  Result<std::unique_ptr<int>> r{std::make_unique<int>(42)};
  ASSERT_TRUE(r.has_value());
  const auto owned = std::move(r).value();
  ASSERT_NE(owned, nullptr);
  EXPECT_EQ(*owned, 42);
}

TEST(Result, ErrorsCompareByCodeAndMessage) {
  const Error a{ErrorCode::parse_error, "bad token"};
  EXPECT_EQ(a, (Error{ErrorCode::parse_error, "bad token"}));
  EXPECT_NE(a, (Error{ErrorCode::parse_error, "other"}));
  EXPECT_NE(a, (Error{ErrorCode::io_error, "bad token"}));
}

TEST(ErrorCode, EveryCodeHasADistinctName) {
  const ErrorCode all[] = {ErrorCode::invalid_argument,
                           ErrorCode::numerical_failure,
                           ErrorCode::not_converged,
                           ErrorCode::parse_error,
                           ErrorCode::io_error};
  for (std::size_t i = 0; i < std::size(all); ++i) {
    EXPECT_NE(fxval::to_string(all[i]), "unknown");
    for (std::size_t j = i + 1; j < std::size(all); ++j) {
      EXPECT_NE(fxval::to_string(all[i]), fxval::to_string(all[j]));
    }
  }
}

// Wrong access is a programmer error: it aborts in Debug builds (contracts on) and throws
// std::bad_variant_access otherwise. Either way it never returns garbage.
TEST(ResultDeathTest, ReadingTheValueOfAnErrorIsRejected) {
  const auto r = parse_positive(-1);
#if defined(FXVAL_ENABLE_CONTRACTS)
  EXPECT_DEATH({ [[maybe_unused]] const auto v = r.value(); }, "contract violated");
#else
  EXPECT_THROW({ [[maybe_unused]] const auto v = r.value(); }, std::bad_variant_access);
#endif
}

TEST(ResultDeathTest, ReadingTheErrorOfAValueIsRejected) {
  const auto r = parse_positive(3);
#if defined(FXVAL_ENABLE_CONTRACTS)
  EXPECT_DEATH({ [[maybe_unused]] const auto& e = r.error(); }, "contract violated");
#else
  EXPECT_THROW({ [[maybe_unused]] const auto& e = r.error(); }, std::bad_variant_access);
#endif
}

}  // namespace
