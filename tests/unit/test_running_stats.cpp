// Unit tests for RunningStats (Welford accumulation and merge).

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <vector>

#include "fxval/mc/running_stats.hpp"

namespace {

using fxval::mc::RunningStats;

struct TwoPass {
  double mean;
  double variance;
};

TwoPass two_pass(const std::vector<double>& x) {
  double mean = 0.0;
  for (const double v : x) {
    mean += v;
  }
  mean /= static_cast<double>(x.size());
  double ss = 0.0;
  for (const double v : x) {
    ss += (v - mean) * (v - mean);
  }
  return {mean, ss / static_cast<double>(x.size() - 1)};
}

std::vector<double> wiggly(std::size_t n) {
  std::vector<double> x;
  for (std::size_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i);
    x.push_back(std::sin(0.37 * t) * 3.0 + std::cos(1.3 * t * t * 1e-3) + 0.01 * t);
  }
  return x;
}

TEST(RunningStatsTest, EmptyAndSingleObservationHaveZeroSpread) {
  RunningStats s;
  EXPECT_EQ(s.count(), 0U);
  EXPECT_EQ(s.variance(), 0.0);
  EXPECT_EQ(s.standard_error(), 0.0);
  s.add(4.0);
  EXPECT_EQ(s.count(), 1U);
  EXPECT_EQ(s.mean(), 4.0);
  EXPECT_EQ(s.variance(), 0.0);
  EXPECT_EQ(s.standard_error(), 0.0);
}

TEST(RunningStatsTest, AgreesWithTheTwoPassFormulas) {
  const std::vector<double> x = wiggly(1000);
  RunningStats s;
  for (const double v : x) {
    s.add(v);
  }
  const TwoPass ref = two_pass(x);
  EXPECT_NEAR(s.mean(), ref.mean, 1e-13);
  EXPECT_NEAR(s.variance(), ref.variance, 1e-12 * ref.variance);
  EXPECT_NEAR(s.standard_error(), std::sqrt(ref.variance / 1000.0), 1e-13);
}

TEST(RunningStatsTest, StaysAccurateWhenTheMeanIsHugeComparedWithTheSpread) {
  // x_i = 1e9 + i for i = 0..999 (exactly representable). The exact sample variance of i is
  // n (n + 1) / 12.
  RunningStats s;
  double naive_sum = 0.0;
  double naive_sum_sq = 0.0;
  const double n = 1000.0;
  for (int i = 0; i < 1000; ++i) {
    const double x = 1e9 + i;
    s.add(x);
    naive_sum += x;
    naive_sum_sq += x * x;
  }
  const double exact = n * (n + 1.0) / 12.0;
  EXPECT_NEAR(s.variance(), exact, 1e-7 * exact);
  // The textbook formula E[x^2] - E[x]^2 is what we do NOT use: here it loses every digit.
  const double naive = (naive_sum_sq - naive_sum * naive_sum / n) / (n - 1.0);
  EXPECT_GT(std::abs(naive - exact), 1e-3 * exact);
}

TEST(RunningStatsTest, MergeOfPartsEqualsOneSequentialPass) {
  const std::vector<double> x = wiggly(997);
  RunningStats whole;
  for (const double v : x) {
    whole.add(v);
  }
  for (const std::size_t cut :
       {std::size_t{0}, std::size_t{1}, std::size_t{500}, std::size_t{996}, std::size_t{997}}) {
    RunningStats left;
    RunningStats right;
    for (std::size_t i = 0; i < x.size(); ++i) {
      (i < cut ? left : right).add(x[i]);
    }
    left.merge(right);
    EXPECT_EQ(left.count(), whole.count()) << "cut " << cut;
    EXPECT_NEAR(left.mean(), whole.mean(), 1e-13) << "cut " << cut;
    EXPECT_NEAR(left.variance(), whole.variance(), 1e-12 * whole.variance()) << "cut " << cut;
  }
}

TEST(RunningStatsTest, MergingManySmallBlocksInOrderIsReproducible) {
  const std::vector<double> x = wiggly(1000);
  const auto merged = [&]() {
    RunningStats total;
    for (std::size_t first = 0; first < x.size(); first += 64) {
      RunningStats block;
      for (std::size_t i = first; i < std::min(first + 64, x.size()); ++i) {
        block.add(x[i]);
      }
      total.merge(block);
    }
    return total;
  };
  const RunningStats a = merged();
  const RunningStats b = merged();
  EXPECT_EQ(a.mean(), b.mean());          // bitwise, not approximately
  EXPECT_EQ(a.variance(), b.variance());  // same order, same rounding
  EXPECT_NEAR(a.variance(), two_pass(x).variance, 1e-12);
}

}  // namespace
