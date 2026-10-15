#include "fxval/mc/vanilla.hpp"

#include <algorithm>
#include <cmath>

#include "fxval/mc/gbm.hpp"
#include "fxval/mc/heston_euler.hpp"

namespace fxval::mc {
namespace {

// Vanilla payoff as a function of the terminal spot.
struct VanillaPayoff {
  pricing::OptionType type;
  double strike;

  double operator()(double terminal_spot) const noexcept {
    return type == pricing::OptionType::call ? std::max(terminal_spot - strike, 0.0)
                                             : std::max(strike - terminal_spot, 0.0);
  }
};

// One code path for every model: simulate, then discount the estimate.
template <PathModel Model>
Result<McPrice> simulate_and_discount(const Model& model,
                                      double strike,
                                      pricing::OptionType type,
                                      Rate domestic,
                                      Years expiry,
                                      const McConfig& config) {
  const VanillaPayoff payoff{type, strike};
  const Result<McEstimate> estimate = run_monte_carlo(model, payoff, config);
  if (estimate.has_value()) {
    const double factor = std::exp(-domestic.value() * expiry.value());
    const McEstimate& e = estimate.value();
    return McPrice{factor * e.mean, factor * e.standard_error, e.paths};
  }
  return Error{estimate.error().code, estimate.error().message};
}

}  // namespace

Result<McPrice> gbm_mc_price(const pricing::VanillaFx& market,
                             pricing::OptionType type,
                             const McConfig& config) {
  const auto model = GbmModel::create(
      market.spot, market.domestic, market.foreign, market.volatility, market.expiry);
  if (!model.has_value()) {
    return Error{model.error().code, model.error().message};
  }
  return simulate_and_discount(
      model.value(), market.strike.value(), type, market.domestic, market.expiry, config);
}

Result<McPrice> heston_euler_mc_price(const models::HestonVanilla& market,
                                      const models::HestonParams& params,
                                      pricing::OptionType type,
                                      const McConfig& config) {
  const auto model = HestonEulerFullTruncation::create(
      market.spot, market.domestic, market.foreign, market.expiry, params);
  if (!model.has_value()) {
    return Error{model.error().code, model.error().message};
  }
  return simulate_and_discount(
      model.value(), market.strike.value(), type, market.domestic, market.expiry, config);
}

}  // namespace fxval::mc
