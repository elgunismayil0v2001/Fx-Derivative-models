#pragma once

#include <cstddef>

#include "fxval/core/error.hpp"
#include "fxval/mc/engine.hpp"
#include "fxval/models/heston.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"

namespace fxval::mc {

// Discounted Monte Carlo price of a European vanilla, with its standard error.
struct McPrice {
  double price;           // domestic currency per one unit of foreign notional
  double standard_error;  // one standard error of `price` (discounting included)
  std::size_t paths;
};

// Variance reduction options for the vanilla wrappers (docs/model_documentation.md 5.7).
//   antithetic       average every path with its mirror image; McConfig.paths then counts PAIRS
//   forward_control  control variate g(S_T) = S_T with the exact mean S_0 e^{(r_d - r_f) T}; the
//                    coefficient is estimated in a separate pilot run of `pilot_paths` paths
struct McReduction {
  bool antithetic = false;
  bool forward_control = false;
  std::size_t pilot_paths = 20'000;
};

// Garman-Kohlhagen dynamics, exact simulation. Errors: invalid_argument (bad configuration or
// zero expiry), numerical_failure (non-finite result).
[[nodiscard]] Result<McPrice> gbm_mc_price(const pricing::VanillaFx& market,
                                           pricing::OptionType type,
                                           const McConfig& config);

// Heston dynamics, Euler scheme with full truncation. The price is biased by the time
// discretisation (config.steps); see test V-05 for its size.
[[nodiscard]] Result<McPrice> heston_euler_mc_price(const models::HestonVanilla& market,
                                                    const models::HestonParams& params,
                                                    pricing::OptionType type,
                                                    const McConfig& config);

// Same, with variance reduction. Errors as above; also numerical_failure if the pilot run cannot
// estimate the control coefficient.
[[nodiscard]] Result<McPrice> heston_euler_mc_price(const models::HestonVanilla& market,
                                                    const models::HestonParams& params,
                                                    pricing::OptionType type,
                                                    const McConfig& config,
                                                    const McReduction& reduction);

// Heston dynamics, Andersen's QE scheme (see heston_qe.hpp). Needs xi > 0 (invalid_argument
// otherwise). Far smaller bias than Euler for the same number of steps, especially when the Feller
// condition fails; test V-06 measures it.
[[nodiscard]] Result<McPrice> heston_qe_mc_price(const models::HestonVanilla& market,
                                                 const models::HestonParams& params,
                                                 pricing::OptionType type,
                                                 const McConfig& config,
                                                 const McReduction& reduction = {});

}  // namespace fxval::mc
