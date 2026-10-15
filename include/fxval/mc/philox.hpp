#pragma once

#include <array>
#include <cstdint>

// Philox4x32-10: a counter-based random number generator (Salmon, Moraes, Dror and Shaw,
// "Parallel random numbers: as easy as 1, 2, 3", SC11).
//
// Why counter-based: the output is a pure function of (key, counter). There is no generator state
// to share between threads and no "skip ahead" to get wrong. Path number p simply uses the
// counter stream p, so a path gets the same random numbers no matter which thread runs it, how
// many threads there are, or in which order paths are simulated. That is what makes test V-07
// (identical result for 1 and N threads) hold by construction and not by luck.
//
// The implementation is checked against the published known-answer vectors of the reference
// implementation (tests/unit/test_philox.cpp) and against an independent Python version
// (tests/validation/test_mc_path_reference.cpp).

namespace fxval::mc {

class Philox4x32 {
public:
  using Block = std::array<std::uint32_t, 4>;
  using Key = std::array<std::uint32_t, 2>;

  // One block of 128 random bits for a 128-bit counter and a 64-bit key.
  [[nodiscard]] static constexpr Block generate(Block counter, Key key) noexcept {
    for (int round = 0; round < rounds; ++round) {
      counter = single_round(counter, key);
      key[0] += weyl_0;
      key[1] += weyl_1;
    }
    return counter;
  }

private:
  static constexpr int rounds = 10;
  static constexpr std::uint32_t multiplier_0 = 0xD2511F53U;
  static constexpr std::uint32_t multiplier_1 = 0xCD9E8D57U;
  static constexpr std::uint32_t weyl_0 = 0x9E3779B9U;  // golden ratio
  static constexpr std::uint32_t weyl_1 = 0xBB67AE85U;  // sqrt(3) - 1

  struct HiLo {
    std::uint32_t hi;
    std::uint32_t lo;
  };

  [[nodiscard]] static constexpr HiLo multiply_hi_lo(std::uint32_t a, std::uint32_t b) noexcept {
    const std::uint64_t product = static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b);
    return {static_cast<std::uint32_t>(product >> 32U), static_cast<std::uint32_t>(product)};
  }

  [[nodiscard]] static constexpr Block single_round(const Block& c, const Key& k) noexcept {
    const HiLo p0 = multiply_hi_lo(multiplier_0, c[0]);
    const HiLo p1 = multiply_hi_lo(multiplier_1, c[2]);
    return {p1.hi ^ c[1] ^ k[0], p1.lo, p0.hi ^ c[3] ^ k[1], p0.lo};
  }
};

}  // namespace fxval::mc
