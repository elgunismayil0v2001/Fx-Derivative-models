#include <gtest/gtest.h>

#include <chrono>

#include "fxval/core/daycount.hpp"

namespace {

using fxval::ErrorCode;
using namespace std::chrono;

TEST(Act365Fixed, SameDayIsZero) {
  const auto y = fxval::act365_fixed(sys_days{year{2024} / January / 2},
                                     sys_days{year{2024} / January / 2});
  ASSERT_TRUE(y.has_value());
  EXPECT_DOUBLE_EQ(y.value().value(), 0.0);
}

TEST(Act365Fixed, CalendarDaysOver365) {
  const auto y = fxval::act365_fixed(sys_days{year{2024} / January / 1},
                                     sys_days{year{2025} / January / 1});
  ASSERT_TRUE(y.has_value());
  EXPECT_DOUBLE_EQ(y.value().value(), 366.0 / 365.0);  // 2024 is a leap year; ACT/365 still uses 365
}

TEST(Act365Fixed, RejectsInvertedDates) {
  const auto y = fxval::act365_fixed(sys_days{year{2025} / January / 1},
                                     sys_days{year{2024} / January / 1});
  ASSERT_FALSE(y.has_value());
  EXPECT_EQ(y.error().code, ErrorCode::invalid_argument);
}

}  // namespace
