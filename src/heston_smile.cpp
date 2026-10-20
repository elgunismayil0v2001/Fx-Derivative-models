#include "fxval/calibration/heston_smile.hpp"

#include <algorithm>
#include <cmath>

#include "fxval/core/limits.hpp"
#include "fxval/numerics/brent.hpp"
#include "fxval/pricing/implied_volatility.hpp"

namespace fxval::calibration {
namespace {

using pricing::OptionType;

// Implied volatility of the Heston model at a strike given as a plain double.
[[nodiscard]] Result<double> iv_at(const smile::SmileMarket& market,
                                   const models::HestonParams& params,
                                   double strike) {
  const Result<Strike> typed = Strike::from_value(strike);
  if (!typed) {
    return typed.error();
  }
  const Result<Volatility> vol = heston_implied_volatility(
      models::HestonVanilla{
          market.spot, typed.value(), market.domestic, market.foreign, market.expiry},
      params);
  if (!vol) {
    return vol.error();
  }
  return vol.value().value();
}

// Solves sigma = IV(K(sigma)) where `strike_of` maps a volatility to its delta strike.
template <class StrikeOf>
[[nodiscard]] Result<double> fixed_point_vol(const smile::SmileMarket& market,
                                             const models::HestonParams& params,
                                             double level,
                                             const StrikeOf& strike_of) {
  const auto residual = [&](double sigma) -> Result<double> {
    const Result<Volatility> vol = Volatility::from_decimal(sigma);
    if (!vol) {
      return vol.error();
    }
    const Result<double> strike = strike_of(vol.value());
    if (!strike) {
      return strike.error();
    }
    const Result<double> implied = iv_at(market, params, strike.value());
    if (!implied) {
      return implied.error();
    }
    return implied.value() - sigma;
  };
  // Bracket: the residual is positive for volatilities well below the model level (the implied
  // volatility at a nearby strike is larger) and negative above it. The upper end is found by
  // scanning upwards and stopping at the first sign change, NOT by starting far out: at five times
  // the level a wing strike can be so far from the forward that the option price underflows (a
  // 25-delta call at sigma = 1.08, T = 2 sits at 8 forwards and is worth 1e-12), and the
  // evaluation there fails although the root is nearby. Found by the CAL-REF test on set 4.
  // The lower end is scanned downwards in the same way: with strong volatility of volatility the
  // implied volatility at a delta strike can be well below the level (an ATM volatility of 0.34
  // times the level for kappa = 0.3, xi = 1, T = 2), and a fixed lower end of 0.3 times the level
  // then fails to bracket.
  double lo = 0.0;
  for (const double multiple : {0.3, 0.1, 0.03, 0.01}) {
    const double candidate = std::max(multiple * level, 1e-4);
    const Result<double> value = residual(candidate);
    if (value && value.value() > 0.0) {
      lo = candidate;
      break;
    }
  }
  double hi = 0.0;
  for (const double multiple : {1.25, 1.6, 2.2, 3.0}) {
    const double candidate = std::min(multiple * level, limits::max_volatility);
    const Result<double> value = residual(candidate);
    if (!value) {
      break;  // too far out for the pricer: the scan has gone past any sign change
    }
    if (value.value() <= 0.0) {
      hi = candidate;
      break;
    }
  }
  if (!(lo > 0.0) || !(hi > lo)) {
    return Error{ErrorCode::numerical_failure,
                 "delta strike fixed point is not bracketed around the model volatility level"};
  }
  Result<double> root = numerics::brent_root(residual, lo, hi, 1e-14, 100);
  if (!root && root.error().code == ErrorCode::invalid_argument) {
    return Error{ErrorCode::numerical_failure,
                 "delta strike fixed point is not bracketed around the model volatility level"};
  }
  return root;
}

}  // namespace

Result<Volatility> heston_implied_volatility(const models::HestonVanilla& market,
                                             const models::HestonParams& params) {
  const double forward =
      market.spot.value() *
      std::exp((market.domestic.value() - market.foreign.value()) * market.expiry.value());
  const OptionType type = market.strike.value() >= forward ? OptionType::call : OptionType::put;
  const Result<double> price = models::heston_price(market, params, type);
  if (!price) {
    return price.error();
  }
  return pricing::garman_kohlhagen_implied_volatility(
      pricing::VanillaMarket{
          market.spot, market.strike, market.domestic, market.foreign, market.expiry},
      type,
      price.value());
}

Result<smile::DeltaQuotes> heston_delta_quotes(const smile::SmileMarket& market,
                                               const models::HestonParams& params,
                                               double delta,
                                               smile::DeltaConvention convention) {
  if (!(delta > 0.0) || !(delta < 0.5)) {
    return Error{ErrorCode::invalid_argument, "wing delta must be in (0, 0.5)"};
  }
  if (!(market.expiry.value() > 0.0)) {
    return Error{ErrorCode::invalid_argument, "model quotes need a positive expiry"};
  }
  const double tau = market.expiry.value();
  const double level = std::sqrt(models::expected_integrated_variance(params, tau) / tau);
  if (!(level > 0.0) || !std::isfinite(level)) {
    return Error{ErrorCode::numerical_failure, "model volatility level is not positive"};
  }

  const Result<double> atm = fixed_point_vol(
      market, params, level, [&](Volatility v) { return smile::atm_strike(market, v); });
  if (!atm) {
    return atm.error();
  }
  const auto wing = [&](OptionType type) {
    return fixed_point_vol(market, params, level, [&](Volatility v) {
      return smile::strike_from_delta(market, v, delta, type, convention);
    });
  };
  const Result<double> call = wing(OptionType::call);
  if (!call) {
    return call.error();
  }
  const Result<double> put = wing(OptionType::put);
  if (!put) {
    return put.error();
  }

  const Result<Volatility> atm_vol = Volatility::from_decimal(atm.value());
  if (!atm_vol) {
    return atm_vol.error();
  }
  const Result<smile::VolSpread> rr = smile::VolSpread::from_decimal(call.value() - put.value());
  const Result<smile::VolSpread> bf =
      smile::VolSpread::from_decimal(0.5 * (call.value() + put.value()) - atm.value());
  if (!rr) {
    return rr.error();
  }
  if (!bf) {
    return bf.error();
  }
  return smile::DeltaQuotes{atm_vol.value(), rr.value(), bf.value(), delta};
}

}  // namespace fxval::calibration
