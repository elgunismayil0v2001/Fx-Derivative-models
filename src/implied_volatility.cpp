#include "fxval/pricing/implied_volatility.hpp"

#include <algorithm>
#include <cmath>

#include "fxval/core/limits.hpp"
#include "fxval/numerics/brent.hpp"

namespace fxval::pricing {
namespace {

constexpr double min_search_vol = 1e-6;

}  // namespace

Result<Volatility> garman_kohlhagen_implied_volatility(const VanillaMarket& market,
                                                       OptionType type,
                                                       double price) {
  const double t = market.expiry.value();
  if (!(t > 0.0)) {
    return Error{ErrorCode::invalid_argument, "implied volatility needs a positive expiry"};
  }
  if (!std::isfinite(price)) {
    return Error{ErrorCode::invalid_argument, "option price is not finite"};
  }
  const double df_d = std::exp(-market.domestic.value() * t);
  const double df_f = std::exp(-market.foreign.value() * t);
  const double s = market.spot.value();
  const double k = market.strike.value();
  const double forward_value = s * df_f;  // discounted forward
  const double strike_value = k * df_d;   // discounted strike

  // No-arbitrage bounds of the option itself.
  const bool call = type == OptionType::call;
  const double intrinsic = call ? std::max(forward_value - strike_value, 0.0)
                                : std::max(strike_value - forward_value, 0.0);
  const double upper = call ? forward_value : strike_value;
  if (price < intrinsic || price > upper) {
    return Error{ErrorCode::invalid_argument,
                 "option price violates the no-arbitrage bounds, no implied volatility exists"};
  }

  // Work with the out-of-the-money option: it has the same implied volatility (parity) and the
  // price carries the volatility information with full relative precision.
  const bool forward_above_strike = forward_value > strike_value;
  const OptionType otm = forward_above_strike ? OptionType::put : OptionType::call;
  double otm_price = price;
  if (otm != type) {
    // call - put = forward_value - strike_value
    otm_price =
        call ? price - (forward_value - strike_value) : price + (forward_value - strike_value);
  }
  if (otm_price <= 0.0) {
    return Error{
        ErrorCode::numerical_failure,
        "out-of-the-money price is not positive: the volatility is below the search range"};
  }

  const auto residual = [&](double sigma) -> Result<double> {
    const Result<Volatility> vol = Volatility::from_decimal(sigma);
    if (!vol) {
      return vol.error();
    }
    const Result<double> model = garman_kohlhagen_price(VanillaFx{market.spot,
                                                                  market.strike,
                                                                  market.domestic,
                                                                  market.foreign,
                                                                  market.expiry,
                                                                  vol.value()},
                                                        otm);
    if (!model) {
      return model.error();
    }
    return model.value() - otm_price;
  };

  const Result<double> root =
      numerics::brent_root(residual, min_search_vol, limits::max_volatility, 1e-15, 200);
  if (!root) {
    if (root.error().code == ErrorCode::invalid_argument) {
      return Error{ErrorCode::numerical_failure, "implied volatility lies outside [1e-6, 500%]"};
    }
    return root.error();
  }
  return Volatility::from_decimal(root.value());
}

}  // namespace fxval::pricing
