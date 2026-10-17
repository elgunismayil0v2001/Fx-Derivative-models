#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>

#include "fxval/mc/philox.hpp"

namespace fxval::mc {

// An independent stream of standard normal numbers, identified by (seed, stream).
//
// Convention used by the whole Monte Carlo module: stream number = path number. Two streams with
// the same seed and different stream numbers are independent; the same (seed, stream) always
// gives the same numbers. A NormalStream is a small value type (no allocation) that one thread
// owns while it simulates one path.
//
// Construction of the numbers:
//   * Each Philox block (128 bits) is split into two 64-bit words; the top 52 bits of each give a
//     uniform number strictly inside (0, 1): u = (k + 1/2) 2^-52. (52 bits, not 53: with 53 the
//     largest value 1 - 2^-54 is not representable and would round to exactly 1.)
//   * Box-Muller turns the two uniforms into two independent normals:
//       z1 = sqrt(-2 ln u1) cos(2 pi u2),   z2 = sqrt(-2 ln u1) sin(2 pi u2).
//   * An ANTITHETIC stream (constructor flag) returns the same numbers with the sign flipped:
//     z -> -z. Because the normals are symmetric, (z, -z) are two equally valid draws that are
//     perfectly negatively correlated, which is what antithetic variates need (week 5).
//   * Box-Muller is used instead of an inverse-CDF routine because it needs no approximation
//     formula that would have to be validated separately. The largest possible |z| is about 8.6
//     (u1 >= 2^-53); real normals exceed that with probability below 1e-17.
class NormalStream {
public:
  constexpr NormalStream(std::uint64_t seed, std::uint64_t stream, bool antithetic = false) noexcept
      : key_{static_cast<std::uint32_t>(seed), static_cast<std::uint32_t>(seed >> 32U)},
        stream_lo_(static_cast<std::uint32_t>(stream)),
        stream_hi_(static_cast<std::uint32_t>(stream >> 32U)), sign_(antithetic ? -1.0 : 1.0) {}

  // Two independent standard normals (one Philox block). Call as often as needed; the stream is
  // 2^64 blocks long.
  void next_pair(double& z1, double& z2) noexcept {
    const Philox4x32::Block counter{static_cast<std::uint32_t>(block_),
                                    static_cast<std::uint32_t>(block_ >> 32U),
                                    stream_lo_,
                                    stream_hi_};
    ++block_;
    const Philox4x32::Block bits = Philox4x32::generate(counter, key_);
    const double u1 = to_open_unit(bits[0], bits[1]);
    const double u2 = to_open_unit(bits[2], bits[3]);
    const double radius = std::sqrt(-2.0 * std::log(u1));
    const double angle = 2.0 * std::numbers::pi * u2;
    z1 = sign_ * radius * std::cos(angle);
    z2 = sign_ * radius * std::sin(angle);
  }

  // Number of blocks consumed so far (for tests).
  [[nodiscard]] constexpr std::uint64_t blocks_used() const noexcept { return block_; }

  // The uniform in (0, 1) built from two 32-bit words. Public so tests can check the mapping.
  [[nodiscard]] static constexpr double to_open_unit(std::uint32_t low,
                                                     std::uint32_t high) noexcept {
    const std::uint64_t word = (static_cast<std::uint64_t>(high) << 32U) | low;
    return (static_cast<double>(word >> 12U) + 0.5) * 0x1.0p-52;
  }

private:
  Philox4x32::Key key_;
  std::uint32_t stream_lo_;
  std::uint32_t stream_hi_;
  double sign_;  // +1, or -1 for an antithetic stream (exact: only the sign bit changes)
  std::uint64_t block_ = 0;
};

}  // namespace fxval::mc
