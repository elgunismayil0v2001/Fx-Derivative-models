// "No heap allocation in the Monte Carlo inner loop", proved by counting.
//
// This executable replaces the global operator new and delete with versions that count calls
// while a gate is open. It is a separate program (fxval_alloc_tests) because the replacement
// applies to the whole process.
//
// Claims checked:
//  1. Simulating a path (all three models, plus the random-number stream) allocates nothing.
//  2. A whole engine run allocates a number of times that does NOT grow with the number of paths
//     (it is a function of the number of blocks and threads only), for plain, antithetic and
//     control-variate estimators.
//  3. The counter can fail: a model that allocates inside simulate_terminal_spot is detected.

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <vector>

#include "fxval/mc/engine.hpp"
#include "fxval/mc/gbm.hpp"
#include "fxval/mc/heston_euler.hpp"
#include "fxval/mc/heston_qe.hpp"
#include "fxval/mc/variance_reduction.hpp"
#include "support/mc_test_support.hpp"

namespace {

std::atomic<bool> g_counting{false};
std::atomic<std::size_t> g_allocations{0};

void* counted_allocate(std::size_t size, std::size_t alignment) {
  if (g_counting.load(std::memory_order_relaxed)) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
  }
  if (size == 0) {
    size = 1;
  }
  void* pointer = nullptr;
  if (alignment > alignof(std::max_align_t)) {
    if (posix_memalign(&pointer, alignment, size) != 0) {
      pointer = nullptr;
    }
  } else {
    pointer = std::malloc(size);
  }
  return pointer;
}

// Counts the allocations made by `work`.
template <class Work>
std::size_t count_allocations(Work&& work) {
  g_allocations.store(0);
  g_counting.store(true);
  work();
  g_counting.store(false);
  return g_allocations.load();
}

}  // namespace

void* operator new(std::size_t size) {
  void* pointer = counted_allocate(size, alignof(std::max_align_t));
  if (pointer == nullptr) {
    throw std::bad_alloc();
  }
  return pointer;
}
void* operator new[](std::size_t size) {
  return operator new(size);
}
void* operator new(std::size_t size, std::align_val_t alignment) {
  void* pointer = counted_allocate(size, static_cast<std::size_t>(alignment));
  if (pointer == nullptr) {
    throw std::bad_alloc();
  }
  return pointer;
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
  return operator new(size, alignment);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  return counted_allocate(size, alignof(std::max_align_t));
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  return counted_allocate(size, alignof(std::max_align_t));
}
// The operator new above allocates with malloc, so freeing with free is a matched pair. GCC 14
// cannot see that once std::allocator is inlined into the engine and reports
// -Wmismatched-new-delete (a false positive; it failed the g++-14 CI job). Silence it for these
// definitions only.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
void operator delete(void* pointer) noexcept {
  std::free(pointer);
}
void operator delete[](void* pointer) noexcept {
  std::free(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept {
  std::free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept {
  std::free(pointer);
}
void operator delete(void* pointer, std::align_val_t) noexcept {
  std::free(pointer);
}
void operator delete[](void* pointer, std::align_val_t) noexcept {
  std::free(pointer);
}
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept {
  std::free(pointer);
}
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
  std::free(pointer);
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace {

using fxval::testing::make_config;
using fxval::testing::make_params;
using fxval::testing::must;

constexpr std::size_t kBlock = 4096;

fxval::mc::GbmModel make_gbm() {
  return must(fxval::mc::GbmModel::create(must(fxval::Spot::from_value(1.10)),
                                          must(fxval::Rate::from_decimal(0.03)),
                                          must(fxval::Rate::from_decimal(0.01)),
                                          must(fxval::Volatility::from_decimal(0.1)),
                                          must(fxval::Years::from_years(1.0))));
}

fxval::mc::HestonEulerFullTruncation make_euler() {
  return must(
      fxval::mc::HestonEulerFullTruncation::create(must(fxval::Spot::from_value(1.10)),
                                                   must(fxval::Rate::from_decimal(0.03)),
                                                   must(fxval::Rate::from_decimal(0.01)),
                                                   must(fxval::Years::from_years(1.0)),
                                                   make_params(0.04, 2.0, 0.04, 0.3, -0.5)));
}

fxval::mc::HestonQuadraticExponential make_qe() {
  return must(
      fxval::mc::HestonQuadraticExponential::create(must(fxval::Spot::from_value(1.10)),
                                                    must(fxval::Rate::from_decimal(0.03)),
                                                    must(fxval::Rate::from_decimal(0.01)),
                                                    must(fxval::Years::from_years(1.0)),
                                                    make_params(0.04, 2.0, 0.04, 0.3, -0.5)));
}

// A model that breaks the rule on purpose.
struct AllocatingModel {
  [[nodiscard]] double simulate_terminal_spot(fxval::mc::NormalStream& rng,
                                              std::size_t /*steps*/) const noexcept {
    std::vector<double> scratch(8);  // heap allocation per path
    double z = 0.0;
    double unused = 0.0;
    rng.next_pair(z, unused);
    scratch[0] = z;
    return 1.0 + 0.01 * scratch[0];
  }
};

template <class Model>
std::size_t allocations_for_paths(const Model& model, std::size_t paths) {
  double sink = 0.0;
  const std::size_t count = count_allocations([&] {
    for (std::uint64_t path = 0; path < paths; ++path) {
      fxval::mc::NormalStream rng(20261017, path);
      sink += model.simulate_terminal_spot(rng, 32);
    }
  });
  EXPECT_TRUE(std::isfinite(sink));
  return count;
}

TEST(AllocationFreeTest, RandomNumberStreamDoesNotAllocate) {
  const std::size_t count = count_allocations([] {
    double total = 0.0;
    for (std::uint64_t stream = 0; stream < 50'000; ++stream) {
      fxval::mc::NormalStream rng(1, stream, (stream % 2) == 1);
      double a = 0.0;
      double b = 0.0;
      rng.next_pair(a, b);
      total += a + b;
    }
    EXPECT_TRUE(std::isfinite(total));
  });
  EXPECT_EQ(count, 0U);
}

TEST(AllocationFreeTest, SimulatingAPathDoesNotAllocate) {
  const auto gbm = make_gbm();
  const auto euler = make_euler();
  const auto qe = make_qe();
  EXPECT_EQ(allocations_for_paths(gbm, 100'000), 0U);
  EXPECT_EQ(allocations_for_paths(euler, 20'000), 0U);
  EXPECT_EQ(allocations_for_paths(qe, 20'000), 0U);
}

TEST(AllocationFreeTest, TheCounterCanFail) {
  // Control: a model that allocates per path is caught, one allocation per path.
  EXPECT_EQ(allocations_for_paths(AllocatingModel{}, 1000), 1000U);
}

template <class Run>
void expect_allocations_independent_of_paths(Run&& run, unsigned threads) {
  // Same configuration except the path count: 4 blocks against 64 blocks. The block summaries
  // live in one std::vector (one allocation) whatever its length; threads add a fixed amount.
  fxval::mc::McConfig small = make_config(5, 4 * kBlock, 8, threads);
  fxval::mc::McConfig large = make_config(5, 64 * kBlock, 8, threads);
  small.block_size = kBlock;
  large.block_size = kBlock;
  run(small);  // warm up: first use of anything lazily allocated (locale, thread tables)
  const std::size_t few = count_allocations([&] { run(small); });
  const std::size_t many = count_allocations([&] { run(large); });
  EXPECT_EQ(few, many) << "allocations grew with the number of paths";
  EXPECT_LE(few, 8U + 4U * threads);
}

TEST(AllocationFreeTest, EngineAllocationsDoNotGrowWithThePathCountSingleThread) {
  const auto qe = make_qe();
  const auto payoff = [](double s) noexcept {
    return s > 1.1 ? s - 1.1 : 0.0;
  };
  expect_allocations_independent_of_paths(
      [&](const fxval::mc::McConfig& config) {
        EXPECT_TRUE(fxval::mc::run_monte_carlo(qe, payoff, config).has_value());
      },
      1);
}

TEST(AllocationFreeTest, EngineAllocationsDoNotGrowWithThePathCountSeveralThreads) {
  const auto qe = make_qe();
  const auto payoff = [](double s) noexcept {
    return s > 1.1 ? s - 1.1 : 0.0;
  };
  expect_allocations_independent_of_paths(
      [&](const fxval::mc::McConfig& config) {
        EXPECT_TRUE(fxval::mc::run_monte_carlo(qe, payoff, config).has_value());
      },
      3);
}

TEST(AllocationFreeTest, AntitheticAndControlVariateRunsDoNotAllocatePerPath) {
  const auto qe = make_qe();
  const auto payoff = [](double s) noexcept {
    return s > 1.1 ? s - 1.1 : 0.0;
  };
  const auto control = [](double s) noexcept {
    return s;
  };
  const double control_mean = 1.10 * std::exp(0.02);
  expect_allocations_independent_of_paths(
      [&](const fxval::mc::McConfig& config) {
        EXPECT_TRUE(
            fxval::mc::run_variance_reduced(qe, payoff, control, control_mean, 0.5, true, config)
                .has_value());
      },
      2);
}

TEST(AllocationFreeTest, TheEngineCheckCanFailToo) {
  // Control: the allocating model makes the engine's allocation count grow with the path count.
  const AllocatingModel model;
  const auto payoff = [](double s) noexcept {
    return s;
  };
  fxval::mc::McConfig small = make_config(5, 4 * kBlock, 8, 1);
  fxval::mc::McConfig large = make_config(5, 16 * kBlock, 8, 1);
  const std::size_t few =
      count_allocations([&] { (void)fxval::mc::run_monte_carlo(model, payoff, small); });
  const std::size_t many =
      count_allocations([&] { (void)fxval::mc::run_monte_carlo(model, payoff, large); });
  EXPECT_GT(many, few + 10 * kBlock);
}

}  // namespace
