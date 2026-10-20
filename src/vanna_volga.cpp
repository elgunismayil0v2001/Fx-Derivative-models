#include "fxval/exotics/vanna_volga.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fxval::exotics {
namespace {

using pricing::BarrierDirection;
using pricing::BarrierEffect;
using pricing::FxMarket;
using pricing::OptionType;

[[nodiscard]] Result<FxMarket> with_spot(const FxMarket& m, double spot) {
  const Result<Spot> s = Spot::from_value(spot);
  if (!s) {
    return s.error();
  }
  return FxMarket{s.value(), m.domestic, m.foreign, m.expiry};
}

// Smallest distance (in spot units) from the spot to a barrier of the contract, or infinity.
[[nodiscard]] double barrier_distance(const ExoticContract& contract, double spot) {
  double d = std::numeric_limits<double>::infinity();
  if (const auto* v = std::get_if<pricing::BarrierVanilla>(&contract)) {
    d = std::abs(spot - v->barrier.value());
  } else if (const auto* t = std::get_if<pricing::TouchContract>(&contract)) {
    d = std::abs(spot - t->barrier.value());
  } else if (const auto* dnt = std::get_if<pricing::DoubleNoTouchContract>(&contract)) {
    d = std::min(std::abs(spot - dnt->lower.value()), std::abs(dnt->upper.value() - spot));
  }
  return d;
}

[[nodiscard]] Result<double> survival_probability(const FxMarket& market,
                                                  Volatility vol,
                                                  const ExoticContract& contract) {
  if (const auto* v = std::get_if<pricing::BarrierVanilla>(&contract)) {
    const Result<double> p = pricing::touch_probability(market, vol, v->barrier, v->direction);
    if (!p) {
      return p.error();
    }
    return v->effect == BarrierEffect::knock_out ? 1.0 - p.value() : p.value();
  }
  if (const auto* t = std::get_if<pricing::TouchContract>(&contract)) {
    const Result<double> p = pricing::touch_probability(market, vol, t->barrier, t->direction);
    if (!p) {
      return p.error();
    }
    return t->pays_on_touch ? p.value() : 1.0 - p.value();
  }
  if (const auto* dnt = std::get_if<pricing::DoubleNoTouchContract>(&contract)) {
    const Result<double> price =
        pricing::double_no_touch_price(market, vol, dnt->lower, dnt->upper);
    if (!price) {
      return price.error();
    }
    return price.value() / std::exp(-market.domestic.value() * market.expiry.value());
  }
  return 1.0;  // plain vanilla
}

// Solve the 3 x 3 system G w = g by Gaussian elimination with partial pivoting.
// G is given by columns: column i holds the Greeks of pivot i.
[[nodiscard]] Result<std::array<double, 3>> solve_weights(const std::array<FlatGreeks, 3>& pivots,
                                                          const FlatGreeks& target) {
  double a[3][4];
  for (int col = 0; col < 3; ++col) {
    a[0][col] = pivots[static_cast<std::size_t>(col)].vega;
    a[1][col] = pivots[static_cast<std::size_t>(col)].vanna;
    a[2][col] = pivots[static_cast<std::size_t>(col)].volga;
  }
  a[0][3] = target.vega;
  a[1][3] = target.vanna;
  a[2][3] = target.volga;
  double scale = 0.0;
  for (const auto& row : a) {
    for (int c = 0; c < 3; ++c) {
      scale = std::max(scale, std::abs(row[c]));
    }
  }
  for (int k = 0; k < 3; ++k) {
    int best = k;
    for (int r = k + 1; r < 3; ++r) {
      if (std::abs(a[r][k]) > std::abs(a[best][k])) {
        best = r;
      }
    }
    if (!(std::abs(a[best][k]) > 1e-13 * scale)) {
      return Error{ErrorCode::numerical_failure,
                   "the vega, vanna and volga of the three pivots are linearly dependent"};
    }
    for (int c = 0; c < 4; ++c) {
      std::swap(a[k][c], a[best][c]);
    }
    for (int r = k + 1; r < 3; ++r) {
      const double f = a[r][k] / a[k][k];
      for (int c = k; c < 4; ++c) {
        a[r][c] -= f * a[k][c];
      }
    }
  }
  std::array<double, 3> w{};
  for (int r = 2; r >= 0; --r) {
    double v = a[r][3];
    for (int c = r + 1; c < 3; ++c) {
      v -= a[r][c] * w[static_cast<std::size_t>(c)];
    }
    w[static_cast<std::size_t>(r)] = v / a[r][r];
  }
  return w;
}

}  // namespace

Result<double> flat_volatility_price(const FxMarket& market,
                                     Volatility volatility,
                                     const ExoticContract& contract) {
  if (const auto* plain = std::get_if<PlainVanilla>(&contract)) {
    return pricing::garman_kohlhagen_price(
        pricing::VanillaFx{
            market.spot, plain->strike, market.domestic, market.foreign, market.expiry, volatility},
        plain->type);
  }
  if (const auto* barrier = std::get_if<pricing::BarrierVanilla>(&contract)) {
    return pricing::barrier_contract_price(market, volatility, *barrier);
  }
  if (const auto* touch = std::get_if<pricing::TouchContract>(&contract)) {
    return pricing::barrier_contract_price(market, volatility, *touch);
  }
  return pricing::barrier_contract_price(
      market, volatility, std::get<pricing::DoubleNoTouchContract>(contract));
}

Result<FlatGreeks> flat_volatility_greeks(const FxMarket& market,
                                          Volatility volatility,
                                          const ExoticContract& contract) {
  const double sigma = volatility.value();
  const double spot = market.spot.value();
  const double h = 2e-3 * sigma;
  // The spot step must not carry the spot across a barrier.
  const double hs = std::min(1e-3 * spot, 0.25 * barrier_distance(contract, spot));
  if (!(h > 0.0) || !(hs > 0.0)) {
    return Error{ErrorCode::invalid_argument,
                 "Greeks need a positive volatility and a spot away from the barrier"};
  }
  const auto value = [&](double s, double v) -> Result<double> {
    const Result<FxMarket> m = with_spot(market, s);
    const Result<Volatility> vol = Volatility::from_decimal(v);
    if (!m) {
      return m.error();
    }
    if (!vol) {
      return vol.error();
    }
    return flat_volatility_price(m.value(), vol.value(), contract);
  };
  const Result<double> base = value(spot, sigma);
  const Result<double> up = value(spot, sigma + h);
  const Result<double> down = value(spot, sigma - h);
  const Result<double> uu = value(spot + hs, sigma + h);
  const Result<double> ud = value(spot + hs, sigma - h);
  const Result<double> du = value(spot - hs, sigma + h);
  const Result<double> dd = value(spot - hs, sigma - h);
  for (const Result<double>* r : {&base, &up, &down, &uu, &ud, &du, &dd}) {
    if (!*r) {
      return r->error();
    }
  }
  FlatGreeks g{};
  g.vega = (up.value() - down.value()) / (2.0 * h);
  g.volga = (up.value() - 2.0 * base.value() + down.value()) / (h * h);
  g.vanna = (uu.value() - ud.value() - du.value() + dd.value()) / (4.0 * hs * h);
  if (!std::isfinite(g.vega) || !std::isfinite(g.volga) || !std::isfinite(g.vanna)) {
    return Error{ErrorCode::numerical_failure, "finite-difference Greeks are not finite"};
  }
  return g;
}

Result<VannaVolga> VannaVolga::create(const smile::SmileSlice& slice) {
  const smile::SmileMarket& sm = slice.market();
  const FxMarket market{sm.spot, sm.domestic, sm.foreign, sm.expiry};
  const smile::PivotStrikes& ks = slice.pivot_strikes();
  const smile::PivotVolatilities& vs = slice.pivot_volatilities();
  const Volatility flat = vs.atm;

  const Result<Strike> k_put = Strike::from_value(ks.put);
  const Result<Strike> k_atm = Strike::from_value(ks.atm);
  const Result<Strike> k_call = Strike::from_value(ks.call);
  if (!k_put) {
    return k_put.error();
  }
  if (!k_atm) {
    return k_atm.error();
  }
  if (!k_call) {
    return k_call.error();
  }
  const std::array<PlainVanilla, 3> pivots{PlainVanilla{OptionType::put, k_put.value()},
                                           PlainVanilla{OptionType::call, k_atm.value()},
                                           PlainVanilla{OptionType::call, k_call.value()}};
  const std::array<Volatility, 3> market_vols{vs.put, vs.atm, vs.call};

  std::array<double, 3> costs{};
  std::array<FlatGreeks, 3> greeks{};
  for (std::size_t i = 0; i < 3; ++i) {
    const Result<double> market_price = flat_volatility_price(market, market_vols[i], pivots[i]);
    const Result<double> flat_price = flat_volatility_price(market, flat, pivots[i]);
    const Result<FlatGreeks> g = flat_volatility_greeks(market, flat, pivots[i]);
    if (!market_price) {
      return market_price.error();
    }
    if (!flat_price) {
      return flat_price.error();
    }
    if (!g) {
      return g.error();
    }
    costs[i] = market_price.value() - flat_price.value();
    greeks[i] = g.value();
  }
  return VannaVolga{market, flat, pivots, costs, greeks};
}

Result<VannaVolgaPrice> VannaVolga::price(const ExoticContract& contract,
                                          VannaVolgaVariant variant) const {
  const auto smile_adjustment = [&](const ExoticContract& c) -> Result<VannaVolgaPrice> {
    const Result<double> flat = flat_volatility_price(market_, flat_vol_, c);
    if (!flat) {
      return flat.error();
    }
    const Result<FlatGreeks> g = flat_volatility_greeks(market_, flat_vol_, c);
    if (!g) {
      return g.error();
    }
    const Result<std::array<double, 3>> w = solve_weights(pivot_greeks_, g.value());
    if (!w) {
      return w.error();
    }
    double cost = 0.0;
    for (std::size_t i = 0; i < 3; ++i) {
      cost += w.value()[i] * smile_costs_[i];
    }
    return VannaVolgaPrice{flat.value() + cost, flat.value(), cost, 1.0, w.value()};
  };
  const auto weighted = [&](const ExoticContract& c, bool use_survival) -> Result<VannaVolgaPrice> {
    Result<VannaVolgaPrice> r = smile_adjustment(c);
    if (!r) {
      return r;
    }
    double factor = 1.0;
    if (use_survival) {
      const Result<double> p = survival_probability(market_, flat_vol_, c);
      if (!p) {
        return p.error();
      }
      factor = p.value();
    }
    VannaVolgaPrice out = r.value();
    out.weight_factor = factor;
    out.price = out.flat_vol_price + factor * out.smile_cost;
    if (!std::isfinite(out.price)) {
      return Error{ErrorCode::numerical_failure, "Vanna-Volga price is not finite"};
    }
    return out;
  };

  switch (variant) {
  case VannaVolgaVariant::unweighted:
    return weighted(contract, false);
  case VannaVolgaVariant::survival:
    return weighted(contract, true);
  case VannaVolgaVariant::vanilla_less_knock_in: {
    if (std::holds_alternative<pricing::DoubleNoTouchContract>(contract)) {
      return Error{ErrorCode::invalid_argument,
                   "the vanilla-less-knock-in variant is not defined for a double no-touch"};
    }
    if (const auto* touch = std::get_if<pricing::TouchContract>(&contract)) {
      if (touch->pays_on_touch) {
        return weighted(contract, true);
      }
      // no-touch = discounted cash - one-touch
      const pricing::TouchContract one_touch{touch->barrier, touch->direction, true};
      const Result<VannaVolgaPrice> ot = weighted(one_touch, true);
      if (!ot) {
        return ot.error();
      }
      const double df = std::exp(-market_.domestic.value() * market_.expiry.value());
      VannaVolgaPrice out = ot.value();
      out.price = df - ot.value().price;
      out.flat_vol_price = df - ot.value().flat_vol_price;
      out.smile_cost = -ot.value().smile_cost;
      return out;
    }
    const auto* barrier = std::get_if<pricing::BarrierVanilla>(&contract);
    if (barrier == nullptr) {
      return weighted(contract, true);  // plain vanilla: nothing to decompose
    }
    if (barrier->effect == BarrierEffect::knock_in) {
      return weighted(contract, true);
    }
    pricing::BarrierVanilla knock_in = *barrier;
    knock_in.effect = BarrierEffect::knock_in;
    const Result<VannaVolgaPrice> vanilla =
        weighted(PlainVanilla{barrier->type, barrier->strike}, false);
    const Result<VannaVolgaPrice> ki = weighted(knock_in, true);
    if (!vanilla) {
      return vanilla.error();
    }
    if (!ki) {
      return ki.error();
    }
    VannaVolgaPrice out{};
    out.price = vanilla.value().price - ki.value().price;
    out.flat_vol_price = vanilla.value().flat_vol_price - ki.value().flat_vol_price;
    out.smile_cost = vanilla.value().smile_cost - ki.value().smile_cost;
    out.weight_factor = ki.value().weight_factor;
    out.weights = ki.value().weights;
    return out;
  }
  }
  return Error{ErrorCode::invalid_argument, "unknown Vanna-Volga variant"};
}

}  // namespace fxval::exotics
