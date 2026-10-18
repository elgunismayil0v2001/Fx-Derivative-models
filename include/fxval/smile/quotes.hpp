#pragma once

#include "fxval/core/error.hpp"
#include "fxval/core/units.hpp"

// Market quotes of one FX smile (docs/model_documentation.md, section 6.1).
//
// A smile at one expiry is quoted by three numbers:
//   ATM  the at-the-money volatility,
//   RR   the risk reversal  = sigma(call at delta) - sigma(put at delta),
//   BF   the butterfly      = (sigma(call at delta) + sigma(put at delta)) / 2 - sigma(ATM).
// This is the "simple" butterfly. Markets often quote a vega-weighted "market strangle" instead,
// which needs a root find and is not implemented (limitation L-09).

namespace fxval::smile {

// A signed difference of two volatilities, as a decimal (0.005 means half a volatility point).
// Unlike Volatility it may be negative. The same decimals-not-percent rule applies.
class VolSpread final : public fxval::detail::StrongDouble<VolSpread> {
public:
  [[nodiscard]] static Result<VolSpread> from_decimal(double spread);
  [[nodiscard]] static Result<VolSpread> from_percent(double percent_points);

private:
  explicit constexpr VolSpread(double spread) noexcept : StrongDouble(spread) {}
};

struct DeltaQuotes {
  Volatility atm;
  VolSpread risk_reversal;
  VolSpread butterfly;
  double delta;  // absolute delta of the wing options, in (0, 0.5): 0.25 or 0.10 in practice
};

// The three volatilities that pin down the smile.
struct PivotVolatilities {
  Volatility put;  // volatility of the put at `delta`
  Volatility atm;
  Volatility call;  // volatility of the call at `delta`
};

// sigma_call = ATM + BF + RR / 2 and sigma_put = ATM + BF - RR / 2.
// invalid_argument if `delta` is outside (0, 0.5) or a wing volatility is not positive.
[[nodiscard]] Result<PivotVolatilities> pivot_volatilities(const DeltaQuotes& quotes);

// The inverse map: quotes from the three volatilities (used by the round-trip test).
[[nodiscard]] DeltaQuotes quotes_from_pivots(const PivotVolatilities& pivots, double delta);

}  // namespace fxval::smile
