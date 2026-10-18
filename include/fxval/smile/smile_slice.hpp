#pragma once

#include "fxval/core/error.hpp"
#include "fxval/core/units.hpp"
#include "fxval/smile/delta_convention.hpp"
#include "fxval/smile/quotes.hpp"

// One smile: implied volatility as a function of strike at one expiry
// (docs/model_documentation.md, sections 6.3 and 6.4).
//
// The smile is the unique QUADRATIC in the forward log-moneyness k = ln(K / F) that passes through
// the three market points (25-delta put, ATM, 25-delta call):
//
//     sigma(k) = sigma_P + b (k - k_P) + c (k - k_P)(k - k_A).
//
// A quadratic is the simplest smooth form that reproduces the three quotes exactly. Its price is
// that it is not guaranteed to be free of arbitrage and that it can go negative; both are checked
// and reported (arbitrage_checks.hpp), not hidden.
//
// Immutable after construction. Const member functions are safe to call from several threads.

namespace fxval::smile {

// The smile polynomial and its first two derivatives at one k. `vol` may be zero or negative far
// from the pivots: it is the raw polynomial, not yet a valid Volatility.
struct SmilePoint {
  double vol;
  double dvol_dk;
  double d2vol_dk2;
};

struct PivotStrikes {
  double put;
  double atm;
  double call;
};

class SmileSlice {
public:
  // Quotes and market to a smile. Errors: invalid quotes (non-positive wing volatility, wrong
  // delta), a non-positive expiry, or pivot strikes that are not in increasing order (which can
  // happen only for very long expiries with large rates).
  [[nodiscard]] static Result<SmileSlice> from_quotes(const SmileMarket& market,
                                                      const DeltaQuotes& quotes,
                                                      DeltaConvention convention);

  [[nodiscard]] const SmileMarket& market() const noexcept { return market_; }
  [[nodiscard]] const DeltaQuotes& quotes() const noexcept { return quotes_; }
  [[nodiscard]] DeltaConvention convention() const noexcept { return convention_; }
  [[nodiscard]] double forward() const noexcept { return forward_; }
  [[nodiscard]] Years expiry() const noexcept { return market_.expiry; }
  [[nodiscard]] const PivotVolatilities& pivot_volatilities() const noexcept { return pivots_; }
  [[nodiscard]] const PivotStrikes& pivot_strikes() const noexcept { return strikes_; }

  // sigma_ATM * sqrt(T): the natural unit of log-moneyness, so k / atm_scale() is "standard
  // deviations from the forward".
  [[nodiscard]] double atm_scale() const noexcept;

  // The polynomial and its derivatives. Cannot fail.
  [[nodiscard]] SmilePoint point(double log_moneyness) const noexcept;

  // The volatility as a validated unit; numerical_failure where the polynomial is not positive or
  // exceeds limits::max_volatility.
  [[nodiscard]] Result<Volatility> vol_at_log_moneyness(double log_moneyness) const;
  [[nodiscard]] Result<Volatility> vol_at_strike(Strike strike) const;

private:
  SmileSlice(const SmileMarket& market,
             const DeltaQuotes& quotes,
             DeltaConvention convention,
             const PivotVolatilities& pivots,
             const PivotStrikes& strikes);

  SmileMarket market_;
  DeltaQuotes quotes_;
  DeltaConvention convention_;
  PivotVolatilities pivots_;
  PivotStrikes strikes_;
  double forward_;
  // Newton form of the interpolating quadratic, in k = ln(K / F).
  double k_put_;
  double k_atm_;
  double slope_ = 0.0;
  double curvature_ = 0.0;
};

}  // namespace fxval::smile
