#include "fxval/risk/backtest.hpp"

#include <cmath>
#include <limits>

namespace fxval::risk {
namespace {

// x ln y with the convention 0 ln 0 = 0.
[[nodiscard]] bool valid_level(double level) noexcept {
  return level > 0.0 && level < 1.0;
}

[[nodiscard]] double xlogy(double x, double y) noexcept {
  return x == 0.0 ? 0.0 : x * std::log(y);
}

}  // namespace

const char* to_string(TrafficLight zone) noexcept {
  switch (zone) {
  case TrafficLight::green:
    return "green";
  case TrafficLight::yellow:
    return "yellow";
  case TrafficLight::red:
    return "red";
  }
  return "unknown";
}

double chi_square_p_value_1(double statistic) noexcept {
  return statistic <= 0.0 ? 1.0 : std::erfc(std::sqrt(0.5 * statistic));
}

double chi_square_p_value_2(double statistic) noexcept {
  return statistic <= 0.0 ? 1.0 : std::exp(-0.5 * statistic);
}

double binomial_cdf(std::size_t n, double p, std::size_t x) noexcept {
  if (x >= n) {
    return 1.0;
  }
  const auto dn = static_cast<double>(n);
  const double log_p = std::log(p);
  const double log_q = std::log1p(-p);
  double sum = 0.0;
  for (std::size_t k = 0; k <= x; ++k) {
    const auto dk = static_cast<double>(k);
    sum += std::exp(std::lgamma(dn + 1.0) - std::lgamma(dk + 1.0) - std::lgamma(dn - dk + 1.0) +
                    dk * log_p + (dn - dk) * log_q);
  }
  return std::min(sum, 1.0);
}

Result<BacktestStatistics> evaluate_exceptions(std::span<const std::uint8_t> hits, double level) {
  if (hits.empty()) {
    return Error{ErrorCode::invalid_argument, "no backtest observations"};
  }
  if (!valid_level(level)) {
    return Error{ErrorCode::invalid_argument,
                 "the confidence level must be strictly inside (0, 1)"};
  }
  std::size_t x = 0;
  double n00 = 0.0;
  double n01 = 0.0;
  double n10 = 0.0;
  double n11 = 0.0;
  for (std::size_t t = 0; t < hits.size(); ++t) {
    if (hits[t] > 1) {
      return Error{ErrorCode::invalid_argument, "exception indicators must be 0 or 1"};
    }
    x += hits[t];
    if (t > 0) {
      if (hits[t - 1] == 0) {
        (hits[t] == 0 ? n00 : n01) += 1.0;
      } else {
        (hits[t] == 0 ? n10 : n11) += 1.0;
      }
    }
  }
  const std::size_t n = hits.size();
  const auto dn = static_cast<double>(n);
  const auto dx = static_cast<double>(x);
  const double p = 1.0 - level;
  const double phat = dx / dn;

  BacktestStatistics s;
  s.observations = n;
  s.exceptions = x;
  s.expected_rate = p;
  s.observed_rate = phat;
  s.kupiec_lr = -2.0 * (xlogy(dn - dx, 1.0 - p) + xlogy(dx, p)) +
                2.0 * (xlogy(dn - dx, 1.0 - phat) + xlogy(dx, phat));
  s.kupiec_lr = std::max(s.kupiec_lr, 0.0);
  s.kupiec_p_value = chi_square_p_value_1(s.kupiec_lr);

  const double from0 = n00 + n01;
  const double from1 = n10 + n11;
  const double transitions = from0 + from1;
  if (transitions > 0.0) {
    const double pi = (n01 + n11) / transitions;
    const double pi01 = from0 > 0.0 ? n01 / from0 : 0.0;
    const double pi11 = from1 > 0.0 ? n11 / from1 : 0.0;
    s.independence_lr = -2.0 * (xlogy(n00 + n10, 1.0 - pi) + xlogy(n01 + n11, pi)) +
                        2.0 * (xlogy(n00, 1.0 - pi01) + xlogy(n01, pi01) + xlogy(n10, 1.0 - pi11) +
                               xlogy(n11, pi11));
    s.independence_lr = std::max(s.independence_lr, 0.0);
  }
  s.independence_p_value = chi_square_p_value_1(s.independence_lr);
  s.conditional_lr = s.kupiec_lr + s.independence_lr;
  s.conditional_p_value = chi_square_p_value_2(s.conditional_lr);

  s.binomial_cdf = binomial_cdf(n, p, x);
  s.zone = TrafficLight::red;
  if (s.binomial_cdf < 0.9999) {
    s.zone = TrafficLight::yellow;
  }
  if (s.binomial_cdf < 0.95) {
    s.zone = TrafficLight::green;
  }
  return s;
}

Result<BacktestRun> rolling_backtest(std::span<const double> returns,
                                     std::size_t window,
                                     const VarForecaster& forecaster,
                                     const PnlFunction& pnl) {
  if (window < 1 || window >= returns.size()) {
    return Error{ErrorCode::invalid_argument,
                 "the backtest window must be at least 1 and below the number of returns"};
  }
  BacktestRun run;
  const std::size_t days = returns.size() - window;
  run.days.reserve(days);
  run.var_forecasts.reserve(days);
  run.realised_pnl.reserve(days);
  run.hits.reserve(days);
  for (std::size_t t = window; t < returns.size(); ++t) {
    // Only returns[t - window, t) is passed: the forecaster cannot see day t or later.
    const Result<double> var = forecaster(returns.subspan(t - window, window));
    if (!var) {
      return var.error();
    }
    const Result<double> realised = pnl(returns[t]);
    if (!realised) {
      return realised.error();
    }
    run.days.push_back(t);
    run.var_forecasts.push_back(var.value());
    run.realised_pnl.push_back(realised.value());
    run.hits.push_back(-realised.value() > var.value() ? 1 : 0);
  }
  return run;
}

}  // namespace fxval::risk
