#include "price_runner.hpp"

#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "fxval/mc/vanilla.hpp"
#include "fxval/models/heston.hpp"

namespace fxval::app {

namespace {

using Priced = PricedOption;

Error with_prefix(const Error& error, std::string_view what) {
  return Error{error.code, std::string(what) + ": " + error.message};
}

models::HestonVanilla heston_market(const MarketSpec& market, const OptionSpec& option) {
  return models::HestonVanilla{
      market.spot, option.strike, market.domestic, market.foreign, option.expiry};
}

Result<Priced> price_with(const GarmanKohlhagenSpec& model,
                          const MarketSpec& market,
                          const OptionSpec& option) {
  const pricing::VanillaFx vanilla{
      market.spot, option.strike, market.domestic, market.foreign, option.expiry, model.volatility};
  const auto price = pricing::garman_kohlhagen_price(vanilla, option.type);
  if (!price) {
    return with_prefix(price.error(), "Garman-Kohlhagen price");
  }
  Priced out{price.value(), std::nullopt, std::nullopt, {}};
  const auto greeks = pricing::garman_kohlhagen_greeks(vanilla, option.type);
  if (greeks) {
    out.greeks = greeks.value();
  } else {
    out.note = "Greeks not available: " + greeks.error().message;
  }
  return out;
}

Result<Priced> price_with(const HestonSemiAnalyticSpec& model,
                          const MarketSpec& market,
                          const OptionSpec& option) {
  const auto price = models::heston_price(heston_market(market, option), model.params, option.type);
  if (!price) {
    return with_prefix(price.error(), "Heston semi-analytic price");
  }
  return Priced{price.value(), std::nullopt, std::nullopt, {}};
}

Result<Priced> from_mc(const Result<mc::McPrice>& estimate, std::string_view what) {
  if (!estimate) {
    return with_prefix(estimate.error(), what);
  }
  return Priced{estimate.value().price, estimate.value().standard_error, std::nullopt, {}};
}

Result<Priced> price_with(const GbmMonteCarloSpec& model,
                          const MarketSpec& market,
                          const OptionSpec& option) {
  const pricing::VanillaFx vanilla{
      market.spot, option.strike, market.domestic, market.foreign, option.expiry, model.volatility};
  return from_mc(mc::gbm_mc_price(vanilla, option.type, model.settings.config),
                 "GBM Monte Carlo price");
}

Result<Priced> price_with(const HestonMonteCarloSpec& model,
                          const MarketSpec& market,
                          const OptionSpec& option) {
  const models::HestonVanilla vanilla = heston_market(market, option);
  if (model.scheme == McScheme::qe) {
    return from_mc(
        mc::heston_qe_mc_price(
            vanilla, model.params, option.type, model.settings.config, model.settings.reduction),
        "Heston QE Monte Carlo price");
  }
  return from_mc(
      mc::heston_euler_mc_price(
          vanilla, model.params, option.type, model.settings.config, model.settings.reduction),
      "Heston Euler Monte Carlo price");
}

}  // namespace

Result<PricedOption> price_option(const PriceConfig& config, const OptionSpec& option) {
  return std::visit([&](const auto& model) { return price_with(model, config.market, option); },
                    config.model);
}

PriceReport run_price(const PriceConfig& config, const OptionPricer& pricer) {
  PriceReport report;
  report.options.reserve(config.options.size());
  for (const OptionSpec& option : config.options) {
    OptionResult result;
    result.id = option.id;
    result.type = option.type;
    result.strike = option.strike.value();
    result.expiry_years = option.expiry.value();
    result.notional = option.notional.value();

    const Result<PricedOption> priced = pricer(config, option);
    if (priced) {
      const Priced& value = priced.value();
      result.ok = true;
      result.price = value.price;
      result.present_value = value.price * option.notional.value();
      result.standard_error = value.standard_error;
      result.greeks = value.greeks;
      result.note = value.note;
    } else {
      result.error = priced.error().message;
    }
    report.options.push_back(std::move(result));
  }
  return report;
}

}  // namespace fxval::app
