#pragma once

#include "fxval/core/error.hpp"
#include "fxval/core/units.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"

// Garman-Kohlhagen implied volatility: the volatility that makes the GK price equal a given price.
// This is how a model price (Heston, in week 8) is expressed in the unit FX markets quote.

namespace fxval::pricing {

// Market side of a vanilla, without a volatility (the unknown here).
struct VanillaMarket {
  Spot spot;
  Strike strike;
  Rate domestic;
  Rate foreign;
  Years expiry;
};

// Solves GK(sigma) = price for sigma in [1e-6, limits::max_volatility] with Brent's method.
//
// An in-the-money price carries little volatility information (vega is tiny deep in the money and
// rounding in the price dominates), so the price is first converted to the OUT-of-the-money option
// by put-call parity, which is exact, and the OTM option is inverted. A call with strike above the
// forward and a put with strike below it are already OTM.
//
// Errors: invalid_argument if expiry <= 0 or the price violates the no-arbitrage bounds (below
// intrinsic value, or above the forward / strike bound); numerical_failure if the implied
// volatility is outside the search range; not_converged from the root finder.
[[nodiscard]] Result<Volatility> garman_kohlhagen_implied_volatility(const VanillaMarket& market,
                                                                     OptionType type,
                                                                     double price);

}  // namespace fxval::pricing
