#pragma once

#include "fxval/core/error.hpp"
#include "fxval/core/units.hpp"
#include "fxval/models/heston.hpp"
#include "fxval/smile/delta_convention.hpp"
#include "fxval/smile/quotes.hpp"

// The Heston smile in market units (docs/model_documentation.md, section 4.6).
//
// A Heston price is not a quote. FX markets quote Garman-Kohlhagen implied volatilities at delta
// strikes, so calibration compares in that unit. This header holds the two maps it needs:
//   price -> implied volatility at a strike, and
//   Heston parameters -> (ATM, risk reversal, butterfly) at a delta, in the SAME convention as
//   smile/delta_convention.hpp.

namespace fxval::calibration {

// Garman-Kohlhagen implied volatility of the Heston price at one strike. The out-of-the-money
// option is priced (a call above the forward, a put below it) and inverted, so the result keeps
// full precision for deep wings. Errors: those of heston_price and of the inversion.
[[nodiscard]] Result<Volatility> heston_implied_volatility(const models::HestonVanilla& market,
                                                           const models::HestonParams& params);

// The delta quotes a Heston model implies at one expiry. Each strike depends on the implied
// volatility AT that strike (the delta uses sigma(K)), so the strikes are found by a root find:
//     sigma = IV_heston( K(sigma) ),     K(sigma) from smile::strike_from_delta / atm_strike.
// The result is the exact inverse of how a SmileSlice reads quotes: building a SmileSlice from
// these quotes puts its pivot strikes at the strikes used here (checked by the unit tests).
// Errors: invalid_argument for delta outside (0, 0.5), numerical_failure if a fixed point is not
// bracketed or a wing volatility is not positive.
[[nodiscard]] Result<smile::DeltaQuotes> heston_delta_quotes(const smile::SmileMarket& market,
                                                             const models::HestonParams& params,
                                                             double delta,
                                                             smile::DeltaConvention convention);

}  // namespace fxval::calibration
