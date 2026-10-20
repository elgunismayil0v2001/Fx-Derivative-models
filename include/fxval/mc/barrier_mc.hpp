#pragma once

#include <variant>

#include "fxval/core/error.hpp"
#include "fxval/core/units.hpp"
#include "fxval/mc/vanilla.hpp"
#include "fxval/models/heston.hpp"
#include "fxval/pricing/barrier_closed_form.hpp"

// Monte Carlo prices of barrier and touch options (docs/model_documentation.md, section 8.3).
//
// Two ways to monitor the barrier on a path with `steps` time steps:
//   discrete  the barrier is checked only at the step dates. This prices a contract that is
//             MONITORED DISCRETELY (for example a daily fixing) and is biased high for knock-outs
//             and low for knock-ins when used as a stand-in for continuous monitoring: the path can
//             cross and come back between two dates.
//   bridge    between two simulated points the log spot is treated as a Brownian BRIDGE, which
//             crosses a level b with the known probability
//                 p = exp( -2 (x_i - b)(x_{i+1} - b) / (sigma^2 dt) )
//             when both ends are on the same side. The estimator multiplies the payoff by the
//             path's survival probability prod_i (1 - p_i) (conditional Monte Carlo: no extra
//             random number, lower variance than drawing a Bernoulli). Under GBM the bridge is
//             exact, so the price has NO time-discretisation bias for any number of steps; under
//             Heston it is an approximation, see below.
//
// Heston and the bridge: given the endpoints and the variance path, the log spot is not exactly a
// Brownian bridge. The code uses sigma^2 = (v_i + v_{i+1}) / 2 as the volatility over the step.
// This is an approximation (limitation L-17); its error shrinks with the step size and is measured
// in test V-12 (C) against the Garman-Kohlhagen closed form in the limit of vanishing vol of vol.
//
// Contracts: single-barrier vanillas, one-touch / no-touch (one unit of domestic currency at
// expiry), and double no-touch. A double barrier uses the survival probability
// (1 - p_lower)(1 - p_upper) per step, which ignores the (rare) event of touching both barriers
// inside one step.
//
// All functions are deterministic for a given McConfig (seed, paths, steps), independent of the
// thread count (test V-07 covers the engine; the barrier tests check it again).

namespace fxval::mc {

enum class BarrierMonitoring : std::uint8_t { discrete, bridge };

using pricing::BarrierContract;
using pricing::DoubleNoTouchContract;
using pricing::TouchContract;

// GBM with volatility `volatility`, exact steps. Errors: invalid_argument (configuration, barrier
// already breached, zero expiry or volatility), numerical_failure (non-finite estimate).
[[nodiscard]] Result<McPrice> gbm_barrier_mc_price(const pricing::FxMarket& market,
                                                   Volatility volatility,
                                                   const BarrierContract& contract,
                                                   BarrierMonitoring monitoring,
                                                   const McConfig& config);

// Heston dynamics by the QE scheme. Needs xi > 0.
[[nodiscard]] Result<McPrice> heston_barrier_mc_price(const pricing::FxMarket& market,
                                                      const models::HestonParams& params,
                                                      const BarrierContract& contract,
                                                      BarrierMonitoring monitoring,
                                                      const McConfig& config);

}  // namespace fxval::mc
