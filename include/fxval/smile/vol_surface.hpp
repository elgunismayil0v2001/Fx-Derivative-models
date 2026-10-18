#pragma once

#include <utility>
#include <vector>

#include "fxval/core/error.hpp"
#include "fxval/core/units.hpp"
#include "fxval/smile/smile_slice.hpp"

// A set of smiles at increasing expiries, interpolated in TOTAL VARIANCE w = sigma^2 T
// (docs/model_documentation.md, section 6.5).
//
// Interpolation is linear in T at fixed forward log-moneyness k = ln(K / F), the same coordinate in
// which the calendar-spread condition is checked, so a surface whose slices pass the calendar
// check stays free of calendar arbitrage between them. There is NO extrapolation: a query outside
// [first expiry, last expiry] is an error, because a number there would be invented.

namespace fxval::smile {

class VolSurface {
public:
  // Slices must be in strictly increasing expiry order and there must be at least one.
  [[nodiscard]] static Result<VolSurface> create(std::vector<SmileSlice> slices);

  [[nodiscard]] const std::vector<SmileSlice>& slices() const noexcept { return slices_; }

  // Total variance at (k, T). numerical_failure if a slice used is not a valid volatility at k;
  // invalid_argument if T is outside the quoted range.
  [[nodiscard]] Result<double> total_variance(double log_moneyness, Years expiry) const;
  [[nodiscard]] Result<Volatility> vol(double log_moneyness, Years expiry) const;

private:
  explicit VolSurface(std::vector<SmileSlice> slices) : slices_(std::move(slices)) {}

  std::vector<SmileSlice> slices_;
};

}  // namespace fxval::smile
