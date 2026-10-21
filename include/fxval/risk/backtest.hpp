#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "fxval/core/error.hpp"

// VaR backtesting (docs/model_documentation.md section 9.6).
//
// An EXCEPTION on day t is a loss larger than the VaR forecast for day t: -pnl_t > VaR_t.
// With confidence level alpha and p = 1 - alpha, a correct and independent forecast gives
// exceptions that are independent Bernoulli(p).
//
// Tests (all fixed here and recomputed independently in python/gen_risk_reference.py):
//   Kupiec (1995) proportion of failures, x exceptions in n days, phat = x / n:
//       LR_pof = -2 [ (n - x) ln(1 - p) + x ln p ] + 2 [ (n - x) ln(1 - phat) + x ln phat ],
//       asymptotically chi-square with 1 degree of freedom (0 ln 0 = 0).
//   Christoffersen (1998) independence, with n_ij the number of days with state i followed by
//       state j (1 = exception), pi01 = n01 / (n00 + n01), pi11 = n11 / (n10 + n11),
//       pi = (n01 + n11) / (n00 + n01 + n10 + n11):
//       LR_ind = -2 [ (n00 + n10) ln(1 - pi) + (n01 + n11) ln pi ]
//                +2 [ n00 ln(1 - pi01) + n01 ln pi01 + n10 ln(1 - pi11) + n11 ln pi11 ],
//       chi-square with 1 degree of freedom.
//   Conditional coverage: LR_cc = LR_pof + LR_ind, chi-square with 2 degrees of freedom.
//   Basel traffic light: with C(x) = P(Binomial(n, p) <= x), green if C(x) < 0.95, yellow if
//       C(x) < 0.9999, red otherwise (for n = 250 and 99%: green 0-4, yellow 5-9, red 10 or more).
// The chi-square approximations are poor with very few exceptions; the numbers are reported with
// the counts so the reader can judge (limitation L-24).

namespace fxval::risk {

enum class TrafficLight : std::uint8_t { green, yellow, red };
[[nodiscard]] const char* to_string(TrafficLight zone) noexcept;

struct BacktestStatistics {
  std::size_t observations = 0;
  std::size_t exceptions = 0;
  double expected_rate = 0.0;  // p = 1 - alpha
  double observed_rate = 0.0;
  double kupiec_lr = 0.0;
  double kupiec_p_value = 1.0;
  double independence_lr = 0.0;  // 0 when the sequence has no transition to measure
  double independence_p_value = 1.0;
  double conditional_lr = 0.0;
  double conditional_p_value = 1.0;
  double binomial_cdf = 0.0;  // C(x) used by the traffic light
  TrafficLight zone = TrafficLight::green;
};

// hits[t] is 1 for an exception, 0 otherwise. Errors: invalid_argument for an empty sequence, a
// value other than 0 or 1, or a level outside (0, 1).
[[nodiscard]] Result<BacktestStatistics> evaluate_exceptions(std::span<const std::uint8_t> hits,
                                                             double level);

// Chi-square survival functions used above (exposed for testing): 1 and 2 degrees of freedom.
[[nodiscard]] double chi_square_p_value_1(double statistic) noexcept;
[[nodiscard]] double chi_square_p_value_2(double statistic) noexcept;

// P(Binomial(n, p) <= x).
[[nodiscard]] double binomial_cdf(std::size_t n, double p, std::size_t x) noexcept;

struct BacktestRun {
  std::vector<std::size_t> days;      // index of each forecast day in the return series
  std::vector<double> var_forecasts;  // VaR for each of those days (positive loss)
  std::vector<double> realised_pnl;   // P&L realised on those days
  std::vector<std::uint8_t> hits;     // exception indicator
};

// A forecaster receives the returns of the `window` days BEFORE the day being forecast, oldest
// first, and nothing else: it has no access to that day or later ones. It returns the VaR (a
// positive loss) for the day. A forecaster may keep state (for example refit a model only every k
// days), but it can only ever have been given past windows.
using VarForecaster = std::function<Result<double>(std::span<const double> past_returns)>;
// Maps the realised log return of a day to its P&L.
using PnlFunction = std::function<Result<double>(double log_return)>;

// Rolling backtest over days window, window + 1, ..., n - 1 of `returns`.
// Errors: invalid_argument if window < 1 or window >= n; whatever the forecaster or P&L function
// return.
[[nodiscard]] Result<BacktestRun> rolling_backtest(std::span<const double> returns,
                                                   std::size_t window,
                                                   const VarForecaster& forecaster,
                                                   const PnlFunction& pnl);

}  // namespace fxval::risk
