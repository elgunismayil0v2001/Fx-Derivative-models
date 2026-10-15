#pragma once

// Helpers shared by the Monte Carlo tests (unit and validation).

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "fxval/mc/engine.hpp"
#include "fxval/mc/vanilla.hpp"
#include "support/heston_test_support.hpp"

#if defined(__has_feature)
#define FXVAL_TEST_HAS_FEATURE(x) __has_feature(x)
#else
#define FXVAL_TEST_HAS_FEATURE(x) 0
#endif

namespace fxval::testing {

// Sanitizer and unoptimised (Debug) builds run Monte Carlo loops 10 to 50 times slower. In those
// builds the tests use fewer paths: the statistical statements stay valid (a standard error is a
// standard error), they only get less sharp. The Release build runs the full sizes.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) ||                               \
    FXVAL_TEST_HAS_FEATURE(address_sanitizer) || FXVAL_TEST_HAS_FEATURE(thread_sanitizer) ||       \
    !defined(NDEBUG)
inline constexpr bool reduced_effort = true;
#else
inline constexpr bool reduced_effort = false;
#endif

[[nodiscard]] constexpr std::size_t scaled_paths(std::size_t full) noexcept {
  return reduced_effort ? std::max<std::size_t>(full / 4, 20'000) : full;
}

[[nodiscard]] inline std::uint64_t bits_of(double x) noexcept {
  return std::bit_cast<std::uint64_t>(x);
}

inline mc::McConfig make_config(std::uint64_t seed,
                                std::size_t paths,
                                std::size_t steps,
                                unsigned threads = 1) {
  mc::McConfig config;
  config.seed = seed;
  config.paths = paths;
  config.steps = steps;
  config.threads = threads;
  return config;
}

// Exact Heston price of a market, for comparison with simulation.
inline double heston_exact(pricing::OptionType type,
                           double spot,
                           double strike,
                           double domestic,
                           double foreign,
                           double years,
                           const models::HestonParams& params) {
  return heston_value(type, spot, strike, domestic, foreign, years, params);
}

}  // namespace fxval::testing
