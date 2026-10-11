#include "fxval/pricing/garman_kohlhagen.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fxval::pricing {
namespace {

// Below this, sigma * sqrt(T) is treated as the Black-Scholes degenerate (deterministic) limit.
constexpr double kTinyVolTime = 1e-16;

struct Kernel {
  double spot;
  double strike;
  double domestic;
  double foreign;
  double expiry;
  double vol;
  OptionType type;
};

Kernel to_kernel(const VanillaFx& market, OptionType type) {
  return Kernel{
      market.spot.value(),
      market.strike.value(),
      market.domestic.value(),
      market.foreign.value(),
      market.expiry.value(),
      market.volatility.value(),
      type,
  };
}

[[nodiscard]] bool is_call(OptionType type) noexcept { return type == OptionType::call; }

[[nodiscard]] double norm_pdf(double x) {
  return std::exp(-0.5 * x * x) / std::sqrt(2.0 * std::numbers::pi);
}

[[nodiscard]] double norm_cdf(double x) { return 0.5 * std::erfc(-x / std::numbers::sqrt2); }

[[nodiscard]] double forward(const Kernel& k) {
  return k.spot * std::exp((k.domestic - k.foreign) * k.expiry);
}

[[nodiscard]] bool is_deterministic(const Kernel& k) {
  return k.expiry <= 0.0 || k.vol * std::sqrt(k.expiry) < kTinyVolTime;
}

struct DTerms {
  double d1;
  double d2;
  double density_d1;
  double n_d1;
  double n_d2;
  double n_minus_d1;
  double n_minus_d2;
};

[[nodiscard]] DTerms d_terms(const Kernel& k) {
  const double sqrt_t = std::sqrt(k.expiry);
  const double vol_sqrt_t = k.vol * sqrt_t;
  const double d1 =
      (std::log(k.spot / k.strike) + (k.domestic - k.foreign + 0.5 * k.vol * k.vol) * k.expiry) /
      vol_sqrt_t;
  const double d2 = d1 - vol_sqrt_t;
  return DTerms{
      d1,
      d2,
      norm_pdf(d1),
      norm_cdf(d1),
      norm_cdf(d2),
      norm_cdf(-d1),
      norm_cdf(-d2),
  };
}

[[nodiscard]] double digital_call(double fwd, double strike) {
  if (fwd > strike) {
    return 1.0;
  }
  if (fwd < strike) {
    return 0.0;
  }
  return 0.5;
}

[[nodiscard]] Result<double> price_deterministic(const Kernel& k) {
  const double df_d = std::exp(-k.domestic * k.expiry);
  const double fwd = (k.expiry <= 0.0) ? k.spot : forward(k);
  const double intrinsic = is_call(k.type) ? std::max(fwd - k.strike, 0.0)
                                           : std::max(k.strike - fwd, 0.0);
  const double value = df_d * intrinsic;
  if (!std::isfinite(value)) {
    return Error{ErrorCode::numerical_failure, "Garman-Kohlhagen price is not finite"};
  }
  return value;
}

[[nodiscard]] Result<double> price_smooth(const Kernel& k) {
  const DTerms d = d_terms(k);
  const double df_f = std::exp(-k.foreign * k.expiry);
  const double df_d = std::exp(-k.domestic * k.expiry);
  const double value = is_call(k.type) ? k.spot * df_f * d.n_d1 - k.strike * df_d * d.n_d2
                                       : k.strike * df_d * d.n_minus_d2 - k.spot * df_f * d.n_minus_d1;
  if (!std::isfinite(value)) {
    return Error{ErrorCode::numerical_failure, "Garman-Kohlhagen price is not finite"};
  }
  return value;
}

[[nodiscard]] Result<GarmanKohlhagenGreeks> greeks_deterministic(const Kernel& k) {
  const double df_f = std::exp(-k.foreign * k.expiry);
  const double df_d = std::exp(-k.domestic * k.expiry);
  const double fwd = (k.expiry <= 0.0) ? k.spot : forward(k);
  const double in_the_money = digital_call(fwd, k.strike);
  const double put_in_the_money = 1.0 - in_the_money;

  GarmanKohlhagenGreeks g{};
  if (is_call(k.type)) {
    g.spot_delta = df_f * in_the_money;
    g.forward_delta = in_the_money;
    if (fwd > k.strike) {
      g.theta = k.foreign * k.spot * df_f - k.domestic * k.strike * df_d;
      g.rho_domestic = k.strike * k.expiry * df_d;
      g.rho_foreign = -k.spot * k.expiry * df_f;
    }
  } else {
    g.spot_delta = -df_f * put_in_the_money;
    g.forward_delta = -put_in_the_money;
    if (fwd < k.strike) {
      g.theta = -k.domestic * k.strike * df_d + k.foreign * k.spot * df_f;
      g.rho_domestic = -k.strike * k.expiry * df_d;
      g.rho_foreign = k.spot * k.expiry * df_f;
    }
  }

  if (!std::isfinite(g.spot_delta) || !std::isfinite(g.theta) || !std::isfinite(g.rho_domestic) ||
      !std::isfinite(g.rho_foreign)) {
    return Error{ErrorCode::numerical_failure, "Garman-Kohlhagen Greeks are not finite"};
  }
  return g;
}

[[nodiscard]] Result<GarmanKohlhagenGreeks> greeks_smooth(const Kernel& k) {
  const DTerms d = d_terms(k);
  const double sqrt_t = std::sqrt(k.expiry);
  const double df_f = std::exp(-k.foreign * k.expiry);
  const double df_d = std::exp(-k.domestic * k.expiry);
  const double vega = k.spot * df_f * d.density_d1 * sqrt_t;

  GarmanKohlhagenGreeks g{};
  if (is_call(k.type)) {
    g.spot_delta = df_f * d.n_d1;
    g.forward_delta = d.n_d1;
    g.theta = -k.spot * df_f * d.density_d1 * k.vol / (2.0 * sqrt_t) +
              k.foreign * k.spot * df_f * d.n_d1 - k.domestic * k.strike * df_d * d.n_d2;
    g.rho_domestic = k.strike * k.expiry * df_d * d.n_d2;
    g.rho_foreign = -k.spot * k.expiry * df_f * d.n_d1;
  } else {
    g.spot_delta = -df_f * d.n_minus_d1;
    g.forward_delta = -d.n_minus_d1;
    g.theta = -k.spot * df_f * d.density_d1 * k.vol / (2.0 * sqrt_t) -
              k.foreign * k.spot * df_f * d.n_minus_d1 + k.domestic * k.strike * df_d * d.n_minus_d2;
    g.rho_domestic = -k.strike * k.expiry * df_d * d.n_minus_d2;
    g.rho_foreign = k.spot * k.expiry * df_f * d.n_minus_d1;
  }
  g.gamma = df_f * d.density_d1 / (k.spot * k.vol * sqrt_t);
  g.vega = vega;
  g.vanna = -df_f * d.density_d1 * d.d2 / k.vol;
  g.volga = vega * d.d1 * d.d2 / k.vol;

  if (!std::isfinite(g.spot_delta) || !std::isfinite(g.gamma) || !std::isfinite(g.vega) ||
      !std::isfinite(g.vanna) || !std::isfinite(g.volga) || !std::isfinite(g.theta) ||
      !std::isfinite(g.rho_domestic) || !std::isfinite(g.rho_foreign)) {
    return Error{ErrorCode::numerical_failure, "Garman-Kohlhagen Greeks are not finite"};
  }
  return g;
}

}  // namespace

Result<double> garman_kohlhagen_price(const VanillaFx& market, OptionType type) {
  const Kernel k = to_kernel(market, type);
  return is_deterministic(k) ? price_deterministic(k) : price_smooth(k);
}

Result<GarmanKohlhagenGreeks> garman_kohlhagen_greeks(const VanillaFx& market, OptionType type) {
  const Kernel k = to_kernel(market, type);
  return is_deterministic(k) ? greeks_deterministic(k) : greeks_smooth(k);
}

}  // namespace fxval::pricing
