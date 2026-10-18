#include "fxval/smile/vol_surface.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace fxval::smile {
namespace {

Result<double> slice_total_variance(const SmileSlice& slice, double k) {
  const Result<Volatility> vol = slice.vol_at_log_moneyness(k);
  if (!vol) {
    return vol.error();
  }
  return vol.value().value() * vol.value().value() * slice.expiry().value();
}

}  // namespace

Result<VolSurface> VolSurface::create(std::vector<SmileSlice> slices) {
  if (slices.empty()) {
    return Error{ErrorCode::invalid_argument, "a volatility surface needs at least one smile"};
  }
  for (std::size_t i = 1; i < slices.size(); ++i) {
    if (!(slices[i].expiry().value() > slices[i - 1].expiry().value())) {
      return Error{ErrorCode::invalid_argument,
                   "smile expiries must be strictly increasing (slice " + std::to_string(i) +
                       " is not later than the one before)"};
    }
  }
  return VolSurface{std::move(slices)};
}

Result<double> VolSurface::total_variance(double log_moneyness, Years expiry) const {
  const double t = expiry.value();
  const double first = slices_.front().expiry().value();
  const double last = slices_.back().expiry().value();
  if (!std::isfinite(log_moneyness) || !(t >= first) || !(t <= last)) {
    return Error{ErrorCode::invalid_argument,
                 "query is outside the quoted expiries (no extrapolation) or not finite"};
  }
  // First slice whose expiry is >= t; the interval is [upper - 1, upper] unless t is a node.
  const auto upper =
      std::lower_bound(slices_.begin(), slices_.end(), t, [](const SmileSlice& s, double value) {
        return s.expiry().value() < value;
      });
  if (upper->expiry().value() == t) {
    return slice_total_variance(*upper, log_moneyness);
  }
  const SmileSlice& hi = *upper;
  const SmileSlice& lo = *(upper - 1);
  const Result<double> w_lo = slice_total_variance(lo, log_moneyness);
  if (!w_lo) {
    return w_lo.error();
  }
  const Result<double> w_hi = slice_total_variance(hi, log_moneyness);
  if (!w_hi) {
    return w_hi.error();
  }
  const double weight = (t - lo.expiry().value()) / (hi.expiry().value() - lo.expiry().value());
  return (1.0 - weight) * w_lo.value() + weight * w_hi.value();
}

Result<Volatility> VolSurface::vol(double log_moneyness, Years expiry) const {
  const Result<double> w = total_variance(log_moneyness, expiry);
  if (!w) {
    return w.error();
  }
  return Volatility::from_decimal(std::sqrt(w.value() / expiry.value()));
}

}  // namespace fxval::smile
