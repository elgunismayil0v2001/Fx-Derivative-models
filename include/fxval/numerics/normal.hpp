#pragma once

#include "fxval/core/error.hpp"

// Standard normal distribution functions used by the smile code.
//
// The Garman-Kohlhagen code keeps its own private copies of the density and distribution function
// (a deliberate choice: the benchmark model has no dependency on this module). The quantile
// function is new in week 7 because a delta quote is a probability and a strike needs its inverse.

namespace fxval::numerics {

// Standard normal density.
[[nodiscard]] double normal_pdf(double x) noexcept;

// Standard normal distribution function, from erfc so that both tails keep relative accuracy.
[[nodiscard]] double normal_cdf(double x) noexcept;

// Inverse of normal_cdf for p strictly inside (0, 1); anything else is invalid_argument.
// A rational approximation (Acklam, relative error about 1e-9) followed by one Halley step on the
// exact distribution function, which brings the result to rounding level. The lower tail is always
// the one computed, so quantiles near 1 are as accurate as quantiles near 0.
[[nodiscard]] Result<double> normal_quantile(double p);

}  // namespace fxval::numerics
