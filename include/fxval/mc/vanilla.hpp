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

}  // namespace fxval::mc
