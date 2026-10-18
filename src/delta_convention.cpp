#include "fxval/smile/delta_convention.hpp"

#include <cmath>

#include "fxval/numerics/normal.hpp"

namespace fxval::smile {
namespace {

using pricing::OptionType;

// Shared validation: the conversions are defined only when there is randomness over the horizon.
[[nodiscard]] Result<double> total_stddev(const SmileMarket& market, Volatility vol) {
  const double sigma_sqrt_t = vol.value() * std::sqrt(market.expiry.value());
  if (!(sigma_sqrt_t > 0.0)) {
    return Error{ErrorCode::invalid_argument,
                 "delta conversion needs a positive volatility and a positive expiry"};
  }
  return sigma_sqrt_t;
}

[[nodiscard]] double foreign_discount(const SmileMarket& market) noexcept {
  return std::exp(-market.foreign.value() * market.expiry.value());
}

}  // namespace

double SmileMarket::forward() const noexcept {
  return spot.value() * std::exp((domestic.value() - foreign.value()) * expiry.value());
}

Result<double> strike_from_delta(const SmileMarket& market,
                                 Volatility vol,
                                 double delta,
                                 OptionType type,
                                 DeltaConvention convention) {
  const Result<double> stddev = total_stddev(market, vol);
  if (!stddev) {
    return stddev.error();
  }
  // N(d1) = p for a call and N(-d1) = p for a put, where p is the absolute delta expressed as a
  // probability: spot delta carries the foreign discount factor, forward delta does not.
  const double discount = convention == DeltaConvention::spot ? foreign_discount(market) : 1.0;
  const double p = delta / discount;
  if (!(delta > 0.0) || !(p < 1.0)) {
    return Error{ErrorCode::invalid_argument,
                 "absolute delta must be in (0, 1) for forward delta and in (0, e^{-r_f T}) for "
                 "spot delta"};
  }
  const Result<double> q = numerics::normal_quantile(p);
  if (!q) {
    return q.error();
  }
  // ln(F/K) = sigma sqrt(T) d1 - sigma^2 T / 2, with d1 = q (call) or d1 = -q (put).
  const double d1 = type == OptionType::call ? q.value() : -q.value();
  const double strike =
      market.forward() * std::exp(-stddev.value() * d1 + 0.5 * stddev.value() * stddev.value());
  if (!std::isfinite(strike) || !(strike > 0.0)) {
    return Error{ErrorCode::numerical_failure, "strike from delta is not a finite positive number"};
  }
  return strike;
}

Result<double> atm_strike(const SmileMarket& market, Volatility vol) {
  const Result<double> stddev = total_stddev(market, vol);
  if (!stddev) {
    return stddev.error();
  }
  const double strike = market.forward() * std::exp(0.5 * stddev.value() * stddev.value());
  if (!std::isfinite(strike) || !(strike > 0.0)) {
    return Error{ErrorCode::numerical_failure, "ATM strike is not a finite positive number"};
  }
  return strike;
}

Result<double> delta_at_strike(const SmileMarket& market,
                               Strike strike,
                               Volatility vol,
                               OptionType type,
                               DeltaConvention convention) {
  const Result<double> stddev = total_stddev(market, vol);
  if (!stddev) {
    return stddev.error();
  }
  const double d1 =
      (std::log(market.forward() / strike.value()) + 0.5 * stddev.value() * stddev.value()) /
      stddev.value();
  const double discount = convention == DeltaConvention::spot ? foreign_discount(market) : 1.0;
  const double delta = type == OptionType::call ? discount * numerics::normal_cdf(d1)
                                                : -discount * numerics::normal_cdf(-d1);
  if (!std::isfinite(delta)) {
    return Error{ErrorCode::numerical_failure, "delta is not finite"};
  }
  return delta;
}

}  // namespace fxval::smile
