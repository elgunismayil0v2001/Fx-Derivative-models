// Unit tests for the random number layer: Philox4x32-10 and the NormalStream built on it.
// Philox is checked against the published known-answer vectors (Random123 reference
// implementation). The statistical tests use fixed seeds, so they are deterministic; their
// bounds are 4.5 standard errors of the statistic, so a failure means a defect, not bad luck.

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

#include "fxval/mc/normal_stream.hpp"
#include "fxval/mc/philox.hpp"

namespace {

using fxval::mc::NormalStream;
using fxval::mc::Philox4x32;

// Evaluated by the compiler: the generator is usable in constant expressions and the known
// answers hold at compile time too.
static_assert(Philox4x32::generate({0U, 0U, 0U, 0U}, {0U, 0U}) ==
              Philox4x32::Block{0x6627e8d5U, 0xe169c58dU, 0xbc57ac4cU, 0x9b00dbd8U});

TEST(PhiloxTest, MatchesKnownAnswerVectorsOfTheReferenceImplementation) {
  // From the Random123 distribution, file kat_vectors, lines for philox4x32 with 10 rounds.
  EXPECT_EQ(Philox4x32::generate({0U, 0U, 0U, 0U}, {0U, 0U}),
            (Philox4x32::Block{0x6627e8d5U, 0xe169c58dU, 0xbc57ac4cU, 0x9b00dbd8U}));
  EXPECT_EQ(Philox4x32::generate({0xffffffffU, 0xffffffffU, 0xffffffffU, 0xffffffffU},
                                 {0xffffffffU, 0xffffffffU}),
            (Philox4x32::Block{0x408f276dU, 0x41c83b0eU, 0xa20bc7c6U, 0x6d5451fdU}));
  // The digits of pi (counter and key).
  EXPECT_EQ(Philox4x32::generate({0x243f6a88U, 0x85a308d3U, 0x13198a2eU, 0x03707344U},
                                 {0xa4093822U, 0x299f31d0U}),
            (Philox4x32::Block{0xd16cfe09U, 0x94fdccebU, 0x5001e420U, 0x24126ea1U}));
}

TEST(PhiloxTest, FlippingOneCounterBitChangesAboutHalfTheOutputBits) {
  // Avalanche: a good counter-based generator turns a one-bit change of the counter into an
  // unrelated block. Average over every one of the 128 counter bits.
  const Philox4x32::Key key{0x12345678U, 0x9abcdef0U};
  const Philox4x32::Block base_counter{1U, 2U, 3U, 4U};
  const auto base = Philox4x32::generate(base_counter, key);
  int total_changed = 0;
  for (int bit = 0; bit < 128; ++bit) {
    auto counter = base_counter;
    counter[static_cast<std::size_t>(bit / 32)] ^= 1U << (bit % 32);
    const auto out = Philox4x32::generate(counter, key);
    for (std::size_t word = 0; word < 4; ++word) {
      total_changed += std::popcount(out[word] ^ base[word]);
    }
  }
  const double mean_changed = static_cast<double>(total_changed) / 128.0;
  EXPECT_NEAR(mean_changed, 64.0, 3.0);  // 128 output bits, each flipping with probability 1/2
}

TEST(NormalStreamTest, UniformMappingStaysStrictlyInsideZeroAndOne) {
  const double smallest = NormalStream::to_open_unit(0U, 0U);
  const double largest = NormalStream::to_open_unit(0xffffffffU, 0xffffffffU);
  EXPECT_EQ(smallest, 0x1.0p-53);
  EXPECT_EQ(largest, 1.0 - 0x1.0p-53);
  EXPECT_GT(smallest, 0.0);
  EXPECT_LT(largest, 1.0);
  // Both extremes give finite normals, so log(u) can never be -inf.
  EXPECT_TRUE(std::isfinite(std::sqrt(-2.0 * std::log(smallest))));
}

TEST(NormalStreamTest, SameSeedAndStreamRepeatExactly) {
  NormalStream a(42, 7);
  NormalStream b(42, 7);
  for (int i = 0; i < 100; ++i) {
    double a1 = 0.0;
    double a2 = 0.0;
    double b1 = 0.0;
    double b2 = 0.0;
    a.next_pair(a1, a2);
    b.next_pair(b1, b2);
    ASSERT_EQ(std::bit_cast<std::uint64_t>(a1), std::bit_cast<std::uint64_t>(b1));
    ASSERT_EQ(std::bit_cast<std::uint64_t>(a2), std::bit_cast<std::uint64_t>(b2));
  }
  EXPECT_EQ(a.blocks_used(), 100U);
}

TEST(NormalStreamTest, DifferentSeedsAndStreamsGiveDifferentNumbers) {
  const auto first = [](std::uint64_t seed, std::uint64_t stream) {
    NormalStream s(seed, stream);
    double z1 = 0.0;
    double z2 = 0.0;
    s.next_pair(z1, z2);
    return z1;
  };
  const double base = first(1, 5);
  EXPECT_NE(first(2, 5), base);
  EXPECT_NE(first(1, 6), base);
  // The high 32 bits of the seed and of the stream number are used, not silently dropped.
  EXPECT_NE(first(1 + (std::uint64_t{1} << 32U), 5), base);
  EXPECT_NE(first(1, 5 + (std::uint64_t{1} << 32U)), base);
}

TEST(NormalStreamTest, LaterBlocksDifferFromEarlierOnes) {
  NormalStream s(9, 9);
  double a1 = 0.0;
  double a2 = 0.0;
  double b1 = 0.0;
  double b2 = 0.0;
  s.next_pair(a1, a2);
  s.next_pair(b1, b2);
  EXPECT_NE(a1, b1);
  EXPECT_NE(a2, b2);
}

struct Moments {
  double mean;
  double variance;
  double skewness;
  double excess_kurtosis;
};

Moments sample_moments(const std::vector<double>& x) {
  const double n = static_cast<double>(x.size());
  double mean = 0.0;
  for (const double v : x) {
    mean += v;
  }
  mean /= n;
  double m2 = 0.0;
  double m3 = 0.0;
  double m4 = 0.0;
  for (const double v : x) {
    const double d = v - mean;
    m2 += d * d;
    m3 += d * d * d;
    m4 += d * d * d * d;
  }
  m2 /= n;
  m3 /= n;
  m4 /= n;
  return {mean, m2, m3 / std::pow(m2, 1.5), m4 / (m2 * m2) - 3.0};
}

std::vector<double> draw(std::uint64_t seed, std::uint64_t stream, std::size_t pairs) {
  NormalStream s(seed, stream);
  std::vector<double> out;
  out.reserve(2 * pairs);
  for (std::size_t i = 0; i < pairs; ++i) {
    double z1 = 0.0;
    double z2 = 0.0;
    s.next_pair(z1, z2);
    out.push_back(z1);
    out.push_back(z2);
  }
  return out;
}

TEST(NormalStreamTest, MomentsAreThoseOfAStandardNormal) {
  const std::vector<double> x = draw(20261014, 0, 500'000);  // one million numbers
  const double n = static_cast<double>(x.size());
  const Moments m = sample_moments(x);
  EXPECT_NEAR(m.mean, 0.0, 4.5 / std::sqrt(n));
  EXPECT_NEAR(m.variance, 1.0, 4.5 * std::sqrt(2.0 / n));
  EXPECT_NEAR(m.skewness, 0.0, 4.5 * std::sqrt(6.0 / n));
  EXPECT_NEAR(m.excess_kurtosis, 0.0, 4.5 * std::sqrt(24.0 / n));
}

TEST(NormalStreamTest, TailFrequenciesMatchTheNormalDistribution) {
  const std::vector<double> x = draw(20261014, 1, 500'000);
  const double n = static_cast<double>(x.size());
  const double p3 = std::erfc(3.0 / std::sqrt(2.0));  // P(|Z| > 3) = 0.0026998
  const double p2 = std::erfc(2.0 / std::sqrt(2.0));  // P(|Z| > 2) = 0.0455003
  const auto frac = [&](double c) {
    return static_cast<double>(
               std::count_if(x.begin(), x.end(), [&](double v) { return std::abs(v) > c; })) /
           n;
  };
  EXPECT_NEAR(frac(3.0), p3, 4.5 * std::sqrt(p3 * (1.0 - p3) / n));
  EXPECT_NEAR(frac(2.0), p2, 4.5 * std::sqrt(p2 * (1.0 - p2) / n));
}

TEST(NormalStreamTest, KolmogorovSmirnovDistanceToTheNormalCdfIsSmall) {
  std::vector<double> x = draw(77, 3, 100'000);
  std::sort(x.begin(), x.end());
  const double n = static_cast<double>(x.size());
  double worst = 0.0;
  for (std::size_t i = 0; i < x.size(); ++i) {
    const double cdf = 0.5 * std::erfc(-x[i] / std::sqrt(2.0));
    const double above = static_cast<double>(i + 1) / n - cdf;
    const double below = cdf - static_cast<double>(i) / n;
    worst = std::max({worst, above, below});
  }
  // P(sqrt(n) D > 1.95) is about 0.001 under the hypothesis of exact normality.
  EXPECT_LT(worst * std::sqrt(n), 1.95);
}

TEST(NormalStreamTest, NumbersWithinABlockAndAcrossStreamsAreUncorrelated) {
  const std::size_t pairs = 200'000;
  const double bound = 4.5 / std::sqrt(static_cast<double>(pairs));
  const std::vector<double> s0 = draw(5, 0, pairs);
  const std::vector<double> s1 = draw(5, 1, pairs);
  double within = 0.0;
  double across = 0.0;
  double across_shifted = 0.0;
  for (std::size_t i = 0; i < pairs; ++i) {
    within += s0[2 * i] * s0[2 * i + 1];
    across += s0[2 * i] * s1[2 * i];
    across_shifted += s0[2 * i + 1] * s1[2 * i];
  }
  const double n = static_cast<double>(pairs);
  EXPECT_NEAR(within / n, 0.0, bound);
  EXPECT_NEAR(across / n, 0.0, bound);
  EXPECT_NEAR(across_shifted / n, 0.0, bound);
}

}  // namespace
