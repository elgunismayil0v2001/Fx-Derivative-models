#pragma once

#include <cmath>
#include <cstddef>

#include "fxval/core/error.hpp"
#include "fxval/core/units.hpp"
#include "fxval/mc/normal_stream.hpp"

namespace fxval::mc {

// Geometric Brownian motion, the Garman-Kohlhagen dynamics, simulated EXACTLY:
//   S_T = S_0 exp( (r_d - r_f - sigma^2 / 2) T + sigma sqrt(T) Z ),   Z ~ N(0, 1).
// There is no time-discretisation error, whatever the number of steps (the step count is
// ignored). It exists to validate the engine itself: a Monte Carlo price of a vanilla option must
// agree with the closed form within sampling error (tests/validation/test_v05_*).
class GbmModel {
public:
  // invalid_argument if the expiry is not strictly positive.
  [[nodiscard]] static Result<GbmModel> create(
      Spot spot, Rate domestic, Rate foreign, Volatility volatility, Years expiry);

  [[nodiscard]] double simulate_terminal_spot(NormalStream& rng,
                                              std::size_t /*steps*/) const noexcept {
    double z = 0.0;
    double unused = 0.0;
    rng.next_pair(z, unused);  // one block gives two normals; the second is not needed
    return spot_ * std::exp(log_drift_ + log_vol_ * z);
  }

private:
  GbmModel(double spot, double log_drift, double log_vol) noexcept
      : spot_(spot), log_drift_(log_drift), log_vol_(log_vol) {}

  double spot_;
  double log_drift_;  // (r_d - r_f - sigma^2 / 2) T
  double log_vol_;    // sigma sqrt(T)
};

}  // namespace fxval::mc
