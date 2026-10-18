#pragma once

#include <optional>
#include <vector>

#include "fxval/core/error.hpp"
#include "fxval/smile/smile_slice.hpp"
#include "fxval/smile/vol_surface.hpp"

// Static-arbitrage diagnostics for a smile and a surface (test V-10;
// docs/model_documentation.md, section 6.6). They REPORT; they never repair a smile.
//
// Butterfly (within one expiry): the implied density must be non-negative. With total variance
// w(k) = sigma(k)^2 T and k = ln(K / F), Gatheral's density factor
//
//     g(k) = (1 - k w' / (2 w))^2 - (w'^2 / 4)(1 / w + 1 / 4) + w'' / 2
//
// is >= 0 exactly when the call price is convex in strike (the density of the terminal rate is
// g(k) exp(-d2^2 / 2) / (K sqrt(2 pi w))). The derivatives of the quadratic smile are analytic.
//
// Calendar (across expiries): total variance must not decrease with maturity at fixed k.
//
// Both are evaluated on a grid of standardised log-moneyness x = k / (sigma_ATM sqrt(T)), "standard
// deviations from the forward", so the grid means the same thing at every expiry.

namespace fxval::smile {

struct CheckGrid {
  double x_max = 4.0;  // the grid covers x in [-x_max, +x_max]
  int points = 801;    // at least 3; odd so that x = 0 is a grid point
};

// One violation, described in the terms a trader reads.
struct Violation {
  double log_moneyness;  // k = ln(K / F)
  double standardised;   // x = k / (sigma_ATM sqrt(T))
  double strike;         // K
  double abs_delta;      // |spot delta| of the out-of-the-money option there, or 0 if undefined
  double value;          // g(k) for the butterfly, relative fall of total variance for the calendar
};

struct ButterflyReport {
  int grid_points = 0;
  int non_positive_vol_points = 0;  // polynomial <= 0 there: no density exists, g not evaluated
  int violations = 0;               // points with g < -arbitrage_slack
  double min_density_factor = 0.0;  // smallest g over the points where it is defined
  double min_density_log_moneyness = 0.0;
  // Violation (or non-positive volatility) closest to the forward, if any; this is the number
  // that says how far into the wings the smile can be trusted.
  std::optional<Violation> innermost;
  [[nodiscard]] bool clean() const noexcept {
    return violations == 0 && non_positive_vol_points == 0;
  }
};

struct CalendarReport {
  double earlier_expiry = 0.0;
  double later_expiry = 0.0;
  int grid_points = 0;
  int undefined_points = 0;            // one of the two smiles is not a valid volatility there
  int violations = 0;                  // points where w(later) < w(earlier) by more than the slack
  double worst_relative_change = 0.0;  // min over the grid of (w_later - w_earlier) / w_earlier
  std::optional<Violation> innermost;
  [[nodiscard]] bool clean() const noexcept { return violations == 0 && undefined_points == 0; }
};

struct SurfaceReport {
  std::vector<ButterflyReport> butterfly;  // one per slice
  std::vector<CalendarReport> calendar;    // one per consecutive pair
  [[nodiscard]] bool clean() const noexcept;
};

// The density factor g at one point of one slice. Defined where the polynomial is positive;
// numerical_failure otherwise.
[[nodiscard]] Result<double> density_factor(const SmileSlice& slice, double log_moneyness);

[[nodiscard]] Result<ButterflyReport> check_butterfly(const SmileSlice& slice,
                                                      const CheckGrid& grid = {});

// The grid is the standardised grid of the EARLIER slice (the narrower one).
[[nodiscard]] Result<CalendarReport> check_calendar(const SmileSlice& earlier,
                                                    const SmileSlice& later,
                                                    const CheckGrid& grid = {});

[[nodiscard]] Result<SurfaceReport> check_surface(const VolSurface& surface,
                                                  const CheckGrid& grid = {});

}  // namespace fxval::smile
