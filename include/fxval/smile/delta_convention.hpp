#pragma once

#include <cstdint>

#include "fxval/core/error.hpp"
#include "fxval/core/units.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"

// Delta quotes and strikes (docs/model_documentation.md, section 6.2).
//
// FX options are quoted by delta, not by strike. Delta conventions differ between currency pairs
// and tenors, so this module implements ONE convention and states it:
//
//   * delta is PREMIUM-EXCLUDED: it is the plain Garman-Kohlhagen delta of the option, as if the
//     premium were paid in the domestic currency;
//   * two flavours of the underlying are supported: SPOT delta, e^{-r_f T} N(d1) for a call, and
//     FORWARD delta, N(d1);
//   * the ATM strike is the DELTA-NEUTRAL STRADDLE strike (call delta + put delta = 0), which is
//     K = F exp(sigma^2 T / 2) in both flavours.
//
// Not implemented, and listed as a limitation: premium-adjusted delta, and the ATM-forward
// definition. A pair whose market uses them will not be reproduced by this module.

namespace fxval::smile {

enum class DeltaConvention : std::uint8_t {
  spot,     // delta of the option with respect to the spot rate
  forward,  // delta of the option with respect to the forward rate (no foreign discount factor)
};

// Everything needed to put one expiry in strike space. Rates are deterministic and flat to the
// expiry, so the forward is F = S exp((r_d - r_f) T).
struct SmileMarket {
  Spot spot;
  Rate domestic;
  Rate foreign;
  Years expiry;

  [[nodiscard]] double forward() const noexcept;
};

// Strike at which a call (or put) has absolute delta `delta` under volatility `vol`.
// `delta` is the absolute value, for example 0.25 for a 25-delta put. It must lie in (0, 1) for the
// forward convention and in (0, e^{-r_f T}) for the spot convention. Requires expiry > 0 and
// vol > 0 (otherwise invalid_argument): a delta is not a strike when there is no randomness.
[[nodiscard]] Result<double> strike_from_delta(const SmileMarket& market,
                                               Volatility vol,
                                               double delta,
                                               pricing::OptionType type,
                                               DeltaConvention convention);

// Strike of the delta-neutral straddle. Requires expiry > 0 and vol > 0.
[[nodiscard]] Result<double> atm_strike(const SmileMarket& market, Volatility vol);

// Signed delta of an option at a strike: positive for a call, negative for a put. Used by the
// arbitrage reports and as the forward map for the round-trip test of strike_from_delta.
// Requires expiry > 0 and vol > 0.
[[nodiscard]] Result<double> delta_at_strike(const SmileMarket& market,
                                             Strike strike,
                                             Volatility vol,
                                             pricing::OptionType type,
                                             DeltaConvention convention);

}  // namespace fxval::smile
