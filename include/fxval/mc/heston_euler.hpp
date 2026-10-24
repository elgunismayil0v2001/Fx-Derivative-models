#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "fxval/core/error.hpp"
#include "fxval/core/units.hpp"
#include "fxval/mc/normal_stream.hpp"
#include "fxval/models/heston.hpp"

namespace fxval::mc {

// State of one Heston path at a time point.
struct HestonState {
  double spot;
  double variance;  // the raw variable v; under full truncation it can be negative
};

// Heston paths by the Euler scheme with FULL TRUNCATION (Lord, Koekkoek and van Dijk, 2010).
//
// One time step of length dt, with v+ = max(v, 0) used everywhere v appears on the right:
//   ln S_{n+1} = ln S_n + (r_d - r_f - v+ / 2) dt + sqrt(v+ dt) Z_S
//   v_{n+1}    = v_n + kappa (theta - v+) dt + xi sqrt(v+ dt) Z_v
//   Z_S = rho Z_v + sqrt(1 - rho^2) Z_perp,   Z_v and Z_perp independent N(0, 1).
// S is advanced in logarithms, so it stays positive and the scheme is exact for xi = 0.
//
// The scheme is biased: the bias shrinks as the step count grows, slowly (it is strong order 1/2
// in general) and it is worst when the Feller condition 2 kappa theta >= xi^2 fails. Test V-05
// measures it; V-06 compares it with the QE scheme. One Philox block per step gives
// both normals.
class HestonEulerFullTruncation {
public:
  // invalid_argument if the expiry is not strictly positive.
  [[nodiscard]] static Result<HestonEulerFullTruncation> create(
      Spot spot, Rate domestic, Rate foreign, Years expiry, models::HestonParams params);

  // Full terminal state; used by tests that compare with an independent implementation.
  [[nodiscard]] HestonState simulate_terminal_state(NormalStream& rng,
                                                    std::size_t steps) const noexcept {
    const double dt = expiry_ / static_cast<double>(steps);
    const double sqrt_dt = std::sqrt(dt);
    const double drift = rate_diff_ * dt;
    const double rho = params_.rho();
    const double rho_complement = std::sqrt(1.0 - rho * rho);
    double log_spot = log_spot0_;
    double variance = params_.v0();
    for (std::size_t i = 0; i < steps; ++i) {
      double z_variance = 0.0;
      double z_independent = 0.0;
      rng.next_pair(z_variance, z_independent);
      const double v_plus = std::max(variance, 0.0);
      const double root = std::sqrt(v_plus) * sqrt_dt;
      const double z_spot = rho * z_variance + rho_complement * z_independent;
      log_spot += drift - 0.5 * v_plus * dt + root * z_spot;
      variance +=
          params_.kappa() * (params_.theta() - v_plus) * dt + params_.xi() * root * z_variance;
    }
    return {std::exp(log_spot), variance};
  }

  [[nodiscard]] double simulate_terminal_spot(NormalStream& rng, std::size_t steps) const noexcept {
    return simulate_terminal_state(rng, steps).spot;
  }

private:
  HestonEulerFullTruncation(double log_spot0,
                            double rate_diff,
                            double expiry,
                            models::HestonParams params) noexcept
      : log_spot0_(log_spot0), rate_diff_(rate_diff), expiry_(expiry), params_(params) {}

  double log_spot0_;
  double rate_diff_;  // r_d - r_f
  double expiry_;
  models::HestonParams params_;
};

}  // namespace fxval::mc
