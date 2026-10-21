// V-21: look-ahead and leakage check for backtests: the VaR forecast for day t uses data up to t-1
// only.
//
// Method, fixed in advance. A series of 1300 simulated returns, window 500, 800 forecast days. For
// each of the four methods (historical simulation, EWMA, GARCH normal with refits every 50 days,
// GARCH filtered historical simulation) the whole rolling backtest is run on the original series
// and on a PERTURBED copy in which every return from day t0 onwards is replaced by an arbitrary
// other value (x 6 plus 0.01, so very different). For each t0 in {600, 800, 1000, 1250}:
//   (1) the VaR forecasts for all days <= t0 must be BITWISE identical in the two runs (the
//   forecast for
//       day t0 may use the data before t0 and nothing from t0 on);
//   (2) at least one forecast among the days t0 + 1 .. t0 + 49 must DIFFER (sanity: the
//   perturbation is
//       visible to the method once it is legitimately in the past, so the comparison is not
//       vacuous). (First written as "the forecast for day t0 + 1 must differ"; historical
//       simulation failed that because one new observation need not move a 99% order statistic, a
//       mistake in the sanity check and not a leak. Changed to a 49-day look, after seeing this.)
// CONTROL: a deliberately leaky forecaster (it reads the return of the day it forecasts, as a
// window shifted by one day would) must be caught by (1), for every t0. This does not prove the
// absence of leakage in code the harness does not call: it proves that, in the harness, no
// forecaster can be handed anything but the preceding `window` returns (the span given is the only
// input) and that the four methods do not smuggle data in another way (a global, a cache).

#include <gtest/gtest.h>

#include <bit>
#include <cstring>
#include <memory>

#include "fxval/risk/backtest.hpp"
#include "support/risk_test_support.hpp"

namespace {

using namespace fxval;
using fxval::testing::Innovations;
using fxval::testing::must;

constexpr std::size_t kTotal = 1300;
constexpr std::size_t kWindow = 500;

struct Method {
  const char* name;
  std::function<risk::VarForecaster()> make;  // a fresh forecaster (fresh state) per run
};

std::vector<double> perturbed(std::vector<double> r, std::size_t t0) {
  for (std::size_t t = t0; t < r.size(); ++t) {
    r[t] = 6.0 * r[t] + 0.01;
  }
  return r;
}

bool bits_equal(double a, double b) {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

TEST(V21_Lookahead, ForecastsForDayTDoNotDependOnDayTOrLater) {
  const timeseries::GarchParams dgp{4e-7, 0.07, 0.0, 0.91};
  const auto returns =
      fxval::testing::simulate_garch_returns(kTotal, dgp, Innovations::normal, 20261029);
  const auto pf = fxval::testing::spot_portfolio(1e6);
  const auto pnl = fxval::testing::pnl_function(pf);
  const std::vector<Method> methods{{"HS",
                                     [&] {
                                       return fxval::testing::historical_forecaster(pf, 0.99);
                                     }},
                                    {"EWMA",
                                     [&] {
                                       return fxval::testing::ewma_forecaster(pf, 0.99, 0.94);
                                     }},
                                    {"GARCH-normal",
                                     [&] {
                                       return fxval::testing::garch_normal_forecaster(pf, 0.99, 50);
                                     }},
                                    {"GARCH-FHS", [&] {
                                       return fxval::testing::garch_fhs_forecaster(pf, 0.99, 50);
                                     }}};

  for (const Method& m : methods) {
    const auto base = must(risk::rolling_backtest(returns, kWindow, m.make(), pnl));
    for (std::size_t t0 :
         {std::size_t{600}, std::size_t{800}, std::size_t{1000}, std::size_t{1250}}) {
      const auto other = perturbed(returns, t0);
      const auto run = must(risk::rolling_backtest(other, kWindow, m.make(), pnl));
      ASSERT_EQ(base.days.size(), run.days.size());
      for (std::size_t i = 0; i < base.days.size(); ++i) {
        if (base.days[i] <= t0) {
          EXPECT_TRUE(bits_equal(base.var_forecasts[i], run.var_forecasts[i]))
              << m.name << " t0 " << t0 << " day " << base.days[i];
        }
      }
      // Sanity: once the perturbed days are in the past, some later forecast changes.
      bool visible = false;
      for (std::size_t i = 0; i < base.days.size(); ++i) {
        if (base.days[i] > t0 && base.days[i] <= t0 + 49 &&
            !bits_equal(base.var_forecasts[i], run.var_forecasts[i])) {
          visible = true;
        }
      }
      EXPECT_TRUE(visible) << m.name << " t0 " << t0
                           << ": the perturbation is invisible, the check is vacuous";
    }
  }
}

TEST(V21_Lookahead, ControlALeakyForecasterIsCaught) {
  const timeseries::GarchParams dgp{4e-7, 0.07, 0.0, 0.91};
  const auto returns =
      fxval::testing::simulate_garch_returns(kTotal, dgp, Innovations::normal, 20261029);
  const auto pf = fxval::testing::spot_portfolio(1e6);
  const auto pnl = fxval::testing::pnl_function(pf);
  // The leaky forecaster ignores the window it is given and reads the series itself, one day too
  // far: the window ending at the day being forecast, INCLUDING that day.
  const auto leaky = [&pf](const std::vector<double>& series) -> risk::VarForecaster {
    auto day = std::make_shared<std::size_t>(kWindow);
    return [&pf, &series, day](std::span<const double>) -> Result<double> {
      const std::size_t t = (*day)++;
      const std::span<const double> window(series.data() + t - kWindow + 1, kWindow);
      const auto v = timeseries::ewma_variance(window, 0.94);
      if (!v) {
        return v.error();
      }
      const auto r = risk::delta_normal_var_es(pf, std::sqrt(v.value().back()), 0.99);
      if (!r) {
        return r.error();
      }
      return r.value().var;
    };
  };
  for (std::size_t t0 : {std::size_t{600}, std::size_t{800}, std::size_t{1000}}) {
    const auto other = perturbed(returns, t0);
    const auto base = must(risk::rolling_backtest(returns, kWindow, leaky(returns), pnl));
    const auto run = must(risk::rolling_backtest(other, kWindow, leaky(other), pnl));
    bool caught = false;
    for (std::size_t i = 0; i < base.days.size(); ++i) {
      if (base.days[i] == t0 && !bits_equal(base.var_forecasts[i], run.var_forecasts[i])) {
        caught = true;
      }
    }
    EXPECT_TRUE(caught) << "the leaky forecaster was not detected at t0 = " << t0;
  }
}

}  // namespace
