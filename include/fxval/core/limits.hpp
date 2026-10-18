#pragma once

namespace fxval::limits {

// Domain sanity bounds for typed inputs. They exist to catch unit mistakes, for example
// passing 8 (meaning 8%) where 0.08 is expected. They are deliberately generous.
inline constexpr double max_volatility = 5.0;  // 500% as a decimal
inline constexpr double max_abs_rate = 1.0;    // 100% as a decimal
inline constexpr double max_years = 100.0;

}  // namespace fxval::limits

namespace fxval::tol {

// Numerical tolerances live in one place. Each value is a starting point taken from
// docs/test_catalogue.md. Changing one needs a reason recorded in that catalogue.
inline constexpr double put_call_parity = 1e-12;            // V-01
inline constexpr double greeks_relative_fd = 1e-6;          // V-02
inline constexpr double heston_vs_gk_vanishing_vol = 1e-6;  // V-03
inline constexpr double quote_roundtrip = 1e-8;             // V-09
// V-10: a butterfly density factor below -arbitrage_slack, or a total variance that falls by more
// than this fraction between maturities, is reported as a violation. It absorbs rounding only.
inline constexpr double arbitrage_slack = 1e-12;

}  // namespace fxval::tol
