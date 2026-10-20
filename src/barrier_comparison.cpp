#include "fxval/exotics/barrier_comparison.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fxval::exotics {
namespace {

[[nodiscard]] Result<pricing::BarrierContract> as_barrier_contract(const ExoticContract& c) {
  if (const auto* v = std::get_if<pricing::BarrierVanilla>(&c)) {
    return pricing::BarrierContract{*v};
  }
  if (const auto* t = std::get_if<pricing::TouchContract>(&c)) {
    return pricing::BarrierContract{*t};
  }
  if (const auto* d = std::get_if<pricing::DoubleNoTouchContract>(&c)) {
    return pricing::BarrierContract{*d};
  }
  return Error{ErrorCode::invalid_argument, "a plain vanilla is not a barrier contract"};
}

}  // namespace

Result<ModelComparison> compare_barrier_models(const smile::SmileSlice& slice,
                                               const models::HestonParams& heston,
                                               const ExoticContract& contract,
                                               const mc::McConfig& config) {
  const Result<pricing::BarrierContract> barrier = as_barrier_contract(contract);
  if (!barrier) {
    return barrier.error();
  }
  const Result<VannaVolga> vv = VannaVolga::create(slice);
  if (!vv) {
    return vv.error();
  }
  const pricing::FxMarket& market = vv.value().market();
  const Volatility flat = vv.value().flat_volatility();

  const Result<double> gk = flat_volatility_price(market, flat, contract);
  const Result<VannaVolgaPrice> unweighted =
      vv.value().price(contract, VannaVolgaVariant::unweighted);
  const Result<VannaVolgaPrice> survival = vv.value().price(contract, VannaVolgaVariant::survival);
  const Result<VannaVolgaPrice> decomposed =
      vv.value().price(contract, VannaVolgaVariant::vanilla_less_knock_in);
  const Result<mc::McPrice> hes = mc::heston_barrier_mc_price(
      market, heston, barrier.value(), mc::BarrierMonitoring::bridge, config);
  const Result<FlatGreeks> greeks = flat_volatility_greeks(market, flat, contract);
  if (!gk) {
    return gk.error();
  }
  if (!unweighted) {
    return unweighted.error();
  }
  if (!survival) {
    return survival.error();
  }
  if (!hes) {
    return hes.error();
  }
  if (!greeks) {
    return greeks.error();
  }
  ModelComparison out{};
  out.gk_flat = gk.value();
  out.vv_unweighted = unweighted.value().price;
  out.vv_survival = survival.value().price;
  out.vv_vanilla_less_knock_in =
      decomposed ? decomposed.value().price : std::numeric_limits<double>::quiet_NaN();
  out.heston = hes.value().price;
  out.heston_standard_error = hes.value().standard_error;
  const double lo = std::min({out.gk_flat, out.vv_survival, out.heston});
  const double hi = std::max({out.gk_flat, out.vv_survival, out.heston});
  out.spread = hi - lo;
  out.vega_per_vol_point = greeks.value().vega * 0.01;
  if (std::abs(out.vega_per_vol_point) > 1e-9) {
    out.spread_vol_points = out.spread / std::abs(out.vega_per_vol_point);
  }
  // Notional: one unit of foreign currency (worth `spot` domestic) for vanillas, one unit of
  // domestic currency for the touch products.
  const double notional =
      std::holds_alternative<pricing::BarrierVanilla>(contract) ? market.spot.value() : 1.0;
  out.spread_percent_notional = 100.0 * out.spread / notional;
  return out;
}

}  // namespace fxval::exotics
