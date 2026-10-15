// V-07: Monte Carlo reproducibility (docs/test_catalogue.md).
//
// Claim: the same seed gives the SAME result, bit for bit, for 1 thread and for N threads, and
// for repeated runs with the same number of threads (whatever the operating system's scheduling).
//
// How the claim is checked:
//  A. GBM and Heston-Euler, every statistic of the result (mean, standard error, variance),
//     compared as raw 64-bit patterns for 1, 2, 3, 4, 7, 8, 16 threads and hardware concurrency.
//  B. 25 repeated runs with 8 threads must all be identical (scheduling differs run to run).
//  C. Control that must FAIL: the usual "give each thread an equal share of the paths and add up
//     the partial sums" reduction, run on the same random numbers. Its result changes in the last
//     digits with the thread count. If this control did not differ, the bitwise comparison above
//     would prove nothing about the engine's design (fixed blocks, merge in block order).
//  D. Changing the block size changes the order of the arithmetic only: results must agree to
//     1e-12 relative, not bitwise (this is documented behaviour). Changing the seed must change
//     the result.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

#include "fxval/mc/gbm.hpp"
#include "fxval/mc/heston_euler.hpp"
#include "fxval/mc/vanilla.hpp"
#include "support/mc_test_support.hpp"

namespace {

using fxval::mc::McConfig;
using fxval::mc::McEstimate;
using fxval::mc::NormalStream;
using fxval::testing::bits_of;
using fxval::testing::make_config;
using fxval::testing::make_params;
using fxval::testing::must;
using fxval::testing::scaled_paths;

constexpr std::uint64_t kSeed = 20261014;
const std::vector<unsigned> kThreadCounts = {1, 2, 3, 4, 7, 8, 16, 0};

constexpr auto call_payoff = [](double s) noexcept {
  return s > 1.1 ? s - 1.1 : 0.0;
};

auto gbm_model() {
  return must(fxval::mc::GbmModel::create(must(fxval::Spot::from_value(1.1)),
                                          must(fxval::Rate::from_decimal(0.03)),
                                          must(fxval::Rate::from_decimal(0.01)),
                                          must(fxval::Volatility::from_decimal(0.15)),
                                          must(fxval::Years::from_years(1.0))));
}

auto heston_model() {
  return must(
      fxval::mc::HestonEulerFullTruncation::create(must(fxval::Spot::from_value(1.1)),
                                                   must(fxval::Rate::from_decimal(0.03)),
                                                   must(fxval::Rate::from_decimal(0.01)),
                                                   must(fxval::Years::from_years(1.0)),
                                                   make_params(0.04, 1.5, 0.05, 0.6, -0.6)));
}

void expect_bitwise_equal(const McEstimate& a, const McEstimate& b, const std::string& what) {
  EXPECT_EQ(bits_of(a.mean), bits_of(b.mean)) << what << " mean";
  EXPECT_EQ(bits_of(a.standard_error), bits_of(b.standard_error)) << what << " standard error";
  EXPECT_EQ(bits_of(a.variance), bits_of(b.variance)) << what << " variance";
  EXPECT_EQ(a.paths, b.paths) << what << " paths";
}

template <class Model>
McEstimate run_with(const Model& model, McConfig config, unsigned threads) {
  config.threads = threads;
  return must(fxval::mc::run_monte_carlo(model, call_payoff, config));
}

TEST(V07_McReproducibility, ResultIsBitwiseIdenticalForAnyNumberOfThreads) {
  // 50'021 paths: not a multiple of the block size, so a partial last block is involved.
  McConfig config = make_config(kSeed, scaled_paths(50'021) + 21, 24);
  config.block_size = 1000;
  const auto gbm = gbm_model();
  const auto heston = heston_model();
  const McEstimate gbm_one = run_with(gbm, config, 1);
  const McEstimate heston_one = run_with(heston, config, 1);
  for (const unsigned threads : kThreadCounts) {
    expect_bitwise_equal(
        gbm_one, run_with(gbm, config, threads), "GBM, threads=" + std::to_string(threads));
    expect_bitwise_equal(heston_one,
                         run_with(heston, config, threads),
                         "Heston, threads=" + std::to_string(threads));
  }
  RecordProperty("thread_counts_compared", static_cast<int>(kThreadCounts.size()));
}

TEST(V07_McReproducibility, RepeatedRunsWithManyThreadsAllAgree) {
  McConfig config = make_config(kSeed, 30'000, 8);
  config.block_size = 256;  // many small blocks: plenty of room for scheduling differences
  const auto heston = heston_model();
  const McEstimate first = run_with(heston, config, 8);
  for (int run = 0; run < 25; ++run) {
    expect_bitwise_equal(first, run_with(heston, config, 8), "run " + std::to_string(run));
  }
}

// The control. Each of T threads takes a contiguous share of the paths and keeps a plain sum;
// the shares are added in thread order. This is a common design, and it is NOT invariant.
double naive_partitioned_sum(const fxval::mc::HestonEulerFullTruncation& model,
                             std::size_t paths,
                             std::size_t steps,
                             unsigned threads) {
  std::vector<double> partial(threads, 0.0);
  std::vector<std::thread> pool;
  for (unsigned t = 0; t < threads; ++t) {
    pool.emplace_back([&, t] {
      const std::size_t first = paths * t / threads;
      const std::size_t last = paths * (t + 1) / threads;
      double sum = 0.0;
      for (std::size_t p = first; p < last; ++p) {
        NormalStream rng(kSeed, p);
        sum += call_payoff(model.simulate_terminal_spot(rng, steps));
      }
      partial[t] = sum;
    });
  }
  for (std::thread& th : pool) {
    th.join();
  }
  double total = 0.0;
  for (const double s : partial) {
    total += s;
  }
  return total;
}

TEST(V07_McReproducibility, ControlNaiveThreadPartitionIsNotInvariantSoTheCheckCanFail) {
  const auto heston = heston_model();
  const std::size_t paths = 20'000;
  const double one = naive_partitioned_sum(heston, paths, 8, 1);
  int differing = 0;
  for (const unsigned threads : {2U, 3U, 5U, 7U}) {
    differing += bits_of(naive_partitioned_sum(heston, paths, 8, threads)) != bits_of(one) ? 1 : 0;
  }
  EXPECT_GE(differing, 1) << "the control never differed: bitwise comparison would be too weak";
  RecordProperty("naive_variants_differing_of_4", differing);
}

TEST(V07_McReproducibility, BlockSizeChangesOnlyRoundingAndSeedChangesTheResult) {
  McConfig a = make_config(kSeed, 40'000, 8, 4);
  a.block_size = 512;
  McConfig b = a;
  b.block_size = 4096;
  const auto heston = heston_model();
  const McEstimate ra = run_with(heston, a, 4);
  const McEstimate rb = run_with(heston, b, 4);
  EXPECT_NEAR(ra.mean, rb.mean, 1e-12 * std::abs(ra.mean));
  EXPECT_NEAR(ra.variance, rb.variance, 1e-12 * ra.variance);
  McConfig other_seed = a;
  other_seed.seed = kSeed + 1;
  const McEstimate rc = run_with(heston, other_seed, 4);
  EXPECT_NE(bits_of(ra.mean), bits_of(rc.mean));
  // Two seeds are two draws of the same estimator: they differ by about one standard error.
  EXPECT_LT(std::abs(ra.mean - rc.mean), 6.0 * ra.standard_error);
}

}  // namespace
