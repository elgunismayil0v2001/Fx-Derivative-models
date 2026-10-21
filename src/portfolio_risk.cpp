#include "fxval/risk/portfolio.hpp"

#include <cmath>

#include "fxval/numerics/normal.hpp"

namespace fxval::risk {
namespace {

[[nodiscard]] Result<double> checked_sigma(double sigma) {
  if (!std::isfinite(sigma) || !(sigma >= 0.0)) {
    return Error{ErrorCode::invalid_argument, "the return volatility must be finite and >= 0"};
  }
  return sigma;
}

}  // namespace

Result<double> portfolio_value(const FxPortfolio& portfolio, double spot_value) {
  const Result<Spot> spot = Spot::from_value(spot_value);
  if (!spot) {
    return spot.error();
  }
  double value = portfolio.spot_units * spot_value;
  for (const OptionLeg& leg : portfolio.options) {
    const Result<double> price =
        pricing::garman_kohlhagen_price(pricing::VanillaFx{spot.value(),
                                                           leg.strike,
                                                           portfolio.domestic,
                                                           portfolio.foreign,
                                                           leg.expiry,
                                                           leg.volatility},
                                        leg.type);
    if (!price) {
      return price.error();
    }
    value += leg.quantity * price.value();
  }
  return value;
}

Result<double> pnl_for_simple_return(const FxPortfolio& portfolio, double x) {
  if (!(x > -1.0) || !std::isfinite(x)) {
    return Error{ErrorCode::invalid_argument, "a simple return must be finite and above -1"};
  }
  const double s0 = portfolio.spot.value();
  const Result<double> up = portfolio_value(portfolio, s0 * (1.0 + x));
  const Result<double> base = portfolio_value(portfolio, s0);
  if (!up) {
    return up.error();
  }
  if (!base) {
    return base.error();
  }
  return up.value() - base.value();
}

Result<double> pnl_for_log_return(const FxPortfolio& portfolio, double r) {
  if (!std::isfinite(r)) {
    return Error{ErrorCode::invalid_argument, "a log return must be finite"};
  }
  const double s0 = portfolio.spot.value();
  const Result<double> up = portfolio_value(portfolio, s0 * std::exp(r));
  const Result<double> base = portfolio_value(portfolio, s0);
  if (!up) {
    return up.error();
  }
  if (!base) {
    return base.error();
  }
  return up.value() - base.value();
}

Result<SpotSensitivities> spot_sensitivities(const FxPortfolio& portfolio) {
  SpotSensitivities s{portfolio.spot_units, 0.0};
  for (const OptionLeg& leg : portfolio.options) {
    const Result<pricing::GarmanKohlhagenGreeks> g =
        pricing::garman_kohlhagen_greeks(pricing::VanillaFx{portfolio.spot,
                                                            leg.strike,
                                                            portfolio.domestic,
                                                            portfolio.foreign,
                                                            leg.expiry,
                                                            leg.volatility},
                                         leg.type);
    if (!g) {
      return g.error();
    }
    s.delta += leg.quantity * g.value().spot_delta;
    s.gamma += leg.quantity * g.value().gamma;
  }
  return s;
}

Result<VarEs> delta_normal_var_es(const FxPortfolio& portfolio, double sigma, double level) {
  const Result<double> sg = checked_sigma(sigma);
  if (!sg) {
    return sg.error();
  }
  const Result<SpotSensitivities> s = spot_sensitivities(portfolio);
  if (!s) {
    return s.error();
  }
  return normal_var_es(0.0, std::abs(s.value().delta) * portfolio.spot.value() * sigma, level);
}

Result<VarEs> delta_gamma_var_es(const FxPortfolio& portfolio, double sigma, double level) {
  const Result<double> sg = checked_sigma(sigma);
  if (!sg) {
    return sg.error();
  }
  const Result<SpotSensitivities> sens = spot_sensitivities(portfolio);
  if (!sens) {
    return sens.error();
  }
  const double s0 = portfolio.spot.value();
  const double c1 = sens.value().delta * s0 * sigma;                     // linear coefficient of w
  const double c2 = 0.5 * sens.value().gamma * s0 * s0 * sigma * sigma;  // coefficient of w^2
  // Q = c1 w + c2 w^2 with w standard normal: mean c2; central moments in section 9.5.
  const double var = c1 * c1 + 2.0 * c2 * c2;
  const double m3 = 6.0 * c1 * c1 * c2 + 8.0 * c2 * c2 * c2;
  const double m4 = 3.0 * c1 * c1 * c1 * c1 + 60.0 * c1 * c1 * c2 * c2 + 60.0 * c2 * c2 * c2 * c2;
  if (!(var > 0.0)) {
    return VarEs{-c2, -c2};  // no risk in this approximation
  }
  const double sd = std::sqrt(var);
  const Result<double> v =
      cornish_fisher_var(c2, sd, m3 / (var * sd), m4 / (var * var) - 3.0, level);
  if (!v) {
    return v.error();
  }
  // ES: from the quantile grid of the same quadratic form.
  constexpr std::size_t grid = 100000;
  std::vector<double> pnl(grid);
  for (std::size_t i = 0; i < grid; ++i) {
    const Result<double> z =
        numerics::normal_quantile((static_cast<double>(i) + 0.5) / static_cast<double>(grid));
    if (!z) {
      return z.error();
    }
    pnl[i] = c1 * z.value() + c2 * z.value() * z.value();
  }
  const Result<VarEs> grid_result = historical_var_es(pnl, level);
  if (!grid_result) {
    return grid_result.error();
  }
  return VarEs{v.value(), grid_result.value().es};
}

Result<VarEs> full_revaluation_normal_var_es(const FxPortfolio& portfolio,
                                             double sigma,
                                             double level,
                                             std::size_t grid) {
  const Result<double> sg = checked_sigma(sigma);
  if (!sg) {
    return sg.error();
  }
  if (grid < 1000) {
    return Error{ErrorCode::invalid_argument, "the quantile grid needs at least 1000 points"};
  }
  std::vector<double> pnl(grid);
  for (std::size_t i = 0; i < grid; ++i) {
    const Result<double> z =
        numerics::normal_quantile((static_cast<double>(i) + 0.5) / static_cast<double>(grid));
    if (!z) {
      return z.error();
    }
    const Result<double> p = pnl_for_simple_return(portfolio, sigma * z.value());
    if (!p) {
      return p.error();
    }
    pnl[i] = p.value();
  }
  return historical_var_es(pnl, level);
}

Result<VarEs> historical_revaluation_var_es(const FxPortfolio& portfolio,
                                            std::span<const double> log_returns,
                                            double level) {
  std::vector<double> pnl;
  pnl.reserve(log_returns.size());
  for (double r : log_returns) {
    const Result<double> p = pnl_for_log_return(portfolio, r);
    if (!p) {
      return p.error();
    }
    pnl.push_back(p.value());
  }
  return historical_var_es(pnl, level);
}

Result<VarEs> filtered_historical_var_es(const FxPortfolio& portfolio,
                                         std::span<const double> standardised_residuals,
                                         double sigma_next,
                                         double level) {
  const Result<double> sg = checked_sigma(sigma_next);
  if (!sg) {
    return sg.error();
  }
  std::vector<double> pnl;
  pnl.reserve(standardised_residuals.size());
  for (double z : standardised_residuals) {
    const Result<double> p = pnl_for_log_return(portfolio, sigma_next * z);
    if (!p) {
      return p.error();
    }
    pnl.push_back(p.value());
  }
  return historical_var_es(pnl, level);
}

}  // namespace fxval::risk
