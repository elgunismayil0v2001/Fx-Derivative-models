#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <thread>
#include <type_traits>
#include <vector>

#include "fxval/core/error.hpp"
#include "fxval/mc/normal_stream.hpp"
#include "fxval/mc/running_stats.hpp"

// Monte Carlo engine: simulate paths of a model, apply a payoff, return the mean and its
// standard error.
//
// Design (docs/adr/0005-monte-carlo-engine.md):
//  * The engine knows nothing about FX or Heston. A model is anything that satisfies PathModel,
//    so a new discretisation scheme (QE in week 5, a Brownian-bridge barrier scheme in week 9)
//    is a new class and does not change this file (open for extension, closed for modification).
//  * Dispatch is static (templates and a concept), so the per-path call is inlined. There are no
//    virtual calls in the inner loop.
//  * Results do not depend on the number of threads: path p always uses random stream p, paths
//    are grouped into fixed-size blocks, each block is summarised on its own, and the blocks are
//    merged in block order. Which thread ran which block cannot change a single bit (test V-07).

namespace fxval::mc {

struct McConfig {
  std::uint64_t seed = 0;
  std::size_t paths = 100'000;
  std::size_t steps = 100;        // time steps per path (ignored by exact schemes)
  unsigned threads = 1;           // 0 means "use the hardware concurrency"
  std::size_t block_size = 4096;  // paths per block; changing it changes rounding, not the law
};

struct McEstimate {
  double mean;
  double standard_error;
  double variance;  // sample variance of the payoff
  std::size_t paths;
};

// Limits that catch unit mistakes and runaway allocations.
inline constexpr std::size_t max_paths = 2'000'000'000;
inline constexpr std::size_t max_steps = 1'000'000;
inline constexpr unsigned max_threads = 256;

// A model that can produce the terminal spot of one path from a random stream.
// simulate_terminal_spot must be const, noexcept, and use only the stream it is given
// (no global state): the engine calls it concurrently from several threads.
template <class M>
concept PathModel = requires(const M& model, NormalStream& rng, std::size_t steps) {
  { model.simulate_terminal_spot(rng, steps) } noexcept -> std::same_as<double>;
};

namespace detail {

// True if the configuration is usable. Otherwise returns false and describes the first problem
// in `problem` (an out-parameter, so the caller's Error is always fully initialised).
[[nodiscard]] bool validate(const McConfig& config, Error& problem);

// Number of threads that will really work: bounded by the number of blocks.
[[nodiscard]] unsigned resolve_threads(unsigned requested, std::size_t blocks) noexcept;

// Merge blocks in index order and check the result is finite. Defined here (not in a .cpp) so
// that static analysers can see how the returned Result is built.
[[nodiscard]] inline Result<McEstimate> combine_blocks(const std::vector<RunningStats>& blocks) {
  RunningStats total;
  for (const RunningStats& block : blocks) {  // fixed order: this is what makes V-07 hold
    total.merge(block);
  }
  const double mean = total.mean();
  const double variance = total.variance();
  if (!std::isfinite(mean) || !std::isfinite(variance)) {
    return Error{ErrorCode::numerical_failure,
                 "Monte Carlo estimate is not finite (a path produced an overflow or NaN)"};
  }
  return McEstimate{mean, total.standard_error(), variance, total.count()};
}

}  // namespace detail

// The lowest level of the engine: estimate the mean of any sample function of the path index.
// `sample(path)` must be callable as double(std::uint64_t), noexcept, and may use only that index
// (typically to build NormalStream(seed, path)); the engine calls it concurrently. Everything
// else (blocks, threads, merge order, V-07) is decided here, so every estimator built on top of
// it, plain, antithetic or control variate, is reproducible for any thread count.
template <class Sample>
  requires std::is_nothrow_invocable_r_v<double, const Sample&, std::uint64_t>
[[nodiscard]] Result<McEstimate> run_samples(const Sample& sample, const McConfig& config) {
  Error problem{ErrorCode::invalid_argument, {}};
  if (!detail::validate(config, problem)) {
    return problem;
  }
  const std::size_t block_size = config.block_size;
  const std::size_t block_count = (config.paths + block_size - 1) / block_size;
  std::vector<RunningStats> blocks(block_count);
  std::atomic<std::size_t> next_block{0};

  // Each block is written by exactly one thread, so no lock is needed. The join below makes
  // the writes visible to the merging thread.
  const auto work = [&]() noexcept {
    for (;;) {
      const std::size_t block = next_block.fetch_add(1, std::memory_order_relaxed);
      if (block >= block_count) {
        return;
      }
      const std::size_t first = block * block_size;
      const std::size_t last = std::min(first + block_size, config.paths);
      RunningStats local;
      for (std::size_t path = first; path < last; ++path) {
        local.add(sample(static_cast<std::uint64_t>(path)));
      }
      blocks[block] = local;
    }
  };

  const unsigned workers = detail::resolve_threads(config.threads, block_count);
  {
    std::vector<std::jthread> pool;
    pool.reserve(workers - 1);
    try {
      for (unsigned i = 1; i < workers; ++i) {
        pool.emplace_back(work);
      }
    } catch (const std::system_error&) {  // NOLINT(bugprone-empty-catch): see comment below
      // The operating system refused another thread. Carry on with the ones we have: the
      // calling thread works too, and the result does not depend on the thread count.
    }
    work();
  }  // all workers are joined here

  return detail::combine_blocks(blocks);
}

// Estimate E[ payoff(S_T) ] (undiscounted). Payoff must be callable as double(double), noexcept.
template <PathModel Model, class Payoff>
  requires std::is_nothrow_invocable_r_v<double, const Payoff&, double>
[[nodiscard]] Result<McEstimate> run_monte_carlo(const Model& model,
                                                 const Payoff& payoff,
                                                 const McConfig& config) {
  const std::uint64_t seed = config.seed;
  const std::size_t steps = config.steps;
  return run_samples(
      [&](std::uint64_t path) noexcept {
        NormalStream rng(seed, path);
        return payoff(model.simulate_terminal_spot(rng, steps));
      },
      config);
}

}  // namespace fxval::mc
