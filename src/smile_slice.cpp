#include "fxval/smile/smile_slice.hpp"

#include <cmath>

#include "fxval/core/limits.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"

namespace fxval::smile {

using pricing::OptionType;

SmileSlice::SmileSlice(const SmileMarket& market,
                       const DeltaQuotes& quotes,
                       DeltaConvention convention,
                       const PivotVolatilities& pivots,
                       const PivotStrikes& strikes)
    : market_(market), quotes_(quotes), convention_(convention), pivots_(pivots), strikes_(strikes),
      forward_(market.forward()), k_put_(std::log(strikes.put / forward_)),
      k_atm_(std::log(strikes.atm / forward_)) {
  const double k_call = std::log(strikes.call / forward_);
  // Divided differences of the three points (k_P, sigma_P), (k_A, sigma_A), (k_C, sigma_C).
  slope_ = (pivots.atm.value() - pivots.put.value()) / (k_atm_ - k_put_);
  const double upper_slope = (pivots.call.value() - pivots.atm.value()) / (k_call - k_atm_);
  curvature_ = (upper_slope - slope_) / (k_call - k_put_);
}

Result<SmileSlice> SmileSlice::from_quotes(const SmileMarket& market,
                                           const DeltaQuotes& quotes,
                                           DeltaConvention convention) {
  const Result<PivotVolatilities> pivots = smile::pivot_volatilities(quotes);
  if (!pivots) {
    return pivots.error();
  }
  const Result<double> put =
      strike_from_delta(market, pivots.value().put, quotes.delta, OptionType::put, convention);
  if (!put) {
    return put.error();
  }
  const Result<double> atm = smile::atm_strike(market, pivots.value().atm);
  if (!atm) {
    return atm.error();
  }
  const Result<double> call =
      strike_from_delta(market, pivots.value().call, quotes.delta, OptionType::call, convention);
  if (!call) {
    return call.error();
  }
  if (put.value() >= atm.value() || atm.value() >= call.value()) {
    return Error{ErrorCode::numerical_failure,
                 "pivot strikes are not increasing (put < ATM < call): the quoted deltas and "
                 "volatilities are not consistent for this expiry and rate"};
  }
  return SmileSlice{market,
                    quotes,
                    convention,
                    pivots.value(),
                    PivotStrikes{put.value(), atm.value(), call.value()}};
}

double SmileSlice::atm_scale() const noexcept {
  return quotes_.atm.value() * std::sqrt(market_.expiry.value());
}

SmilePoint SmileSlice::point(double log_moneyness) const noexcept {
  const double from_put = log_moneyness - k_put_;
  const double from_atm = log_moneyness - k_atm_;
  return SmilePoint{
      pivots_.put.value() + slope_ * from_put + curvature_ * from_put * from_atm,
      slope_ + curvature_ * (from_put + from_atm),
      2.0 * curvature_,
  };
}

Result<Volatility> SmileSlice::vol_at_log_moneyness(double log_moneyness) const {
  if (!std::isfinite(log_moneyness)) {
    return Error{ErrorCode::invalid_argument, "log-moneyness must be finite"};
  }
  const double vol = point(log_moneyness).vol;
  if (!(vol > 0.0) || !(vol <= limits::max_volatility)) {
    return Error{ErrorCode::numerical_failure,
                 "the quadratic smile is not a valid volatility at this strike (zero, negative or "
                 "beyond the volatility limit)"};
  }
  return Volatility::from_decimal(vol);
}

Result<Volatility> SmileSlice::vol_at_strike(Strike strike) const {
  return vol_at_log_moneyness(std::log(strike.value() / forward_));
}

}  // namespace fxval::smile
