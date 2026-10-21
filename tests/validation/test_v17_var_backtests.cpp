// V-17: VaR backtests for each method (Kupiec, Christoffersen, traffic light).
//
// Setup, fixed before any result was looked at:
//   * Two simulated series of 3750 daily returns from a GARCH(1,1) with omega 4e-7, alpha 0.07,
//   beta
//     0.91 (persistence 0.98, daily volatility 0.45%): A with normal innovations (seed 20261026)
//     and B with Student-t(6) innovations (seed 20261027). The model that generated A is the one
//     fitted by the GARCH methods; B has fatter tails than any normal-based method assumes.
//   * Portfolio: 1,000,000 units of foreign currency (linear, so VaR scales with the volatility).
//   * Rolling window 750 days, 3000 forecast days, levels 99% and 95%.
//   * Methods: historical simulation (HS-750), EWMA(0.94) normal, GARCH(1,1) normal (refit every 50
//     days), GARCH(1,1) filtered historical simulation (FHS, refit every 50 days), and a CONTROL,
//     "static": normal VaR with the volatility of the first window and never updated.
// Reported for every method and level: exceptions, rate, Kupiec, independence and
// conditional-coverage p-values, Basel zone computed for the whole sample (binomial thresholds for
// n = 3000) and for the last 250 days. Asserted (decided before the run):
//   1. CONTROL must fail: "static" has conditional-coverage p below 0.01 on both series at both
//   levels.
//   2. On series A, GARCH-normal (the true model) has Kupiec p above 0.01 at both levels.
//   3. FHS has Kupiec p above 0.01 on both series at both levels.
//   4. Every method runs to the end with 3000 forecasts.
// Not asserted, only reported: everything else, in particular HS-750's independence p-value and
// the tail of GARCH-normal on the t series (the expected shortfalls of normality at 99%).
// TEST OF THE TEST: the Kupiec and independence tests are applied to 2000 simulated Bernoulli(1%)
// sequences of 2500 days. Kupiec must reject at 5% in between 2.5% and 8% of them (size) and,
// applied to Bernoulli(2%) sequences, in at least 80% (power). The independence test must reject in
// less than 8% (it must not be oversized). DISCLOSURE: the independence test was first required to
// reject between 2.5% and 8%, like Kupiec. It rejected in 1.7%: with about 25 exceptions in 2500
// days almost every transition count n11 is 0 or 1, where the chi-square approximation is
// conservative (limitation L-24). Python gives the same statistic (RISK-REF), so this is a property
// of the test and not of the code. The lower bound was dropped for the independence test after
// seeing this, and the consequence (low power of the independence test at 99%) is stated in the
// documentation.

#include <gtest/gtest.h>

#include <iomanip>
#include <iostream>

#include "fxval/risk/backtest.hpp"
#include "support/mc_test_support.hpp"
#include "support/risk_test_support.hpp"

namespace {

using namespace fxval;
using fxval::testing::Innovations;
using fxval::testing::must;

constexpr std::size_t kTotal = 3750;
constexpr std::size_t kWindow = 750;
constexpr std::size_t kRefit = 50;

struct Row {
  std::string method;
  double level;
  risk::BacktestStatistics all;
  risk::BacktestStatistics last250;
};

std::vector<Row> run_series(const std::vector<double>& returns, const char* label) {
  const auto pf = fxval::testing::spot_portfolio(1e6);
  const auto pnl = fxval::testing::pnl_function(pf);
  std::vector<Row> rows;
  for (double level : {0.99, 0.95}) {
    struct M {
      const char* name;
      risk::VarForecaster forecaster;
    };
    std::vector<M> methods;
    methods.push_back({"HS-750", fxval::testing::historical_forecaster(pf, level)});
    methods.push_back({"EWMA-normal", fxval::testing::ewma_forecaster(pf, level, 0.94)});
    methods.push_back({"GARCH-normal", fxval::testing::garch_normal_forecaster(pf, level, kRefit)});
    methods.push_back({"GARCH-FHS", fxval::testing::garch_fhs_forecaster(pf, level, kRefit)});
    methods.push_back({"static (control)", fxval::testing::static_forecaster(pf, level)});
    for (auto& m : methods) {
      const auto run = must(risk::rolling_backtest(returns, kWindow, m.forecaster, pnl));
      EXPECT_EQ(run.hits.size(), kTotal - kWindow) << label << " " << m.name;
      Row row{m.name,
              level,
              must(risk::evaluate_exceptions(run.hits, level)),
              must(risk::evaluate_exceptions(std::span<const std::uint8_t>(run.hits).last(250),
                                             level))};
      rows.push_back(row);
    }
  }
  std::cout << "\nV-17 series " << label << " (" << kTotal - kWindow << " forecast days)\n"
            << std::left << std::setw(18) << "method" << std::right << std::setw(7) << "level"
            << std::setw(6) << "exc" << std::setw(8) << "rate" << std::setw(10) << "Kupiec"
            << std::setw(10) << "indep" << std::setw(10) << "cond.cov" << std::setw(8) << "zone"
            << std::setw(8) << "last250" << std::setw(8) << "zone250" << "\n";
  for (const Row& r : rows) {
    std::cout << std::left << std::setw(18) << r.method << std::right << std::fixed
              << std::setprecision(3) << std::setw(7) << r.level << std::setw(6) << r.all.exceptions
              << std::setw(8) << r.all.observed_rate << std::setw(10) << r.all.kupiec_p_value
              << std::setw(10) << r.all.independence_p_value << std::setw(10)
              << r.all.conditional_p_value << std::setw(8) << risk::to_string(r.all.zone)
              << std::setw(8) << r.last250.exceptions << std::setw(8)
              << risk::to_string(r.last250.zone) << "\n";
  }
  return rows;
}

const Row& find(const std::vector<Row>& rows, const std::string& method, double level) {
  for (const Row& r : rows) {
    if (r.method == method && r.level == level) {
      return r;
    }
  }
  throw std::runtime_error("row not found");
}

TEST(V17_VarBacktests, MethodsOnTheNormalAndTheFatTailedSeries) {
  const timeseries::GarchParams dgp{4e-7, 0.07, 0.0, 0.91};
  const auto a = fxval::testing::simulate_garch_returns(kTotal, dgp, Innovations::normal, 20261026);
  const auto b =
      fxval::testing::simulate_garch_returns(kTotal, dgp, Innovations::student_t6, 20261027);
  const auto rows_a = run_series(a, "A (normal innovations)");
  const auto rows_b = run_series(b, "B (Student-t 6 innovations)");
  for (double level : {0.99, 0.95}) {
    EXPECT_LT(find(rows_a, "static (control)", level).all.conditional_p_value, 0.01) << level;
    EXPECT_LT(find(rows_b, "static (control)", level).all.conditional_p_value, 0.01) << level;
    EXPECT_GT(find(rows_a, "GARCH-normal", level).all.kupiec_p_value, 0.01) << level;
    EXPECT_GT(find(rows_a, "GARCH-FHS", level).all.kupiec_p_value, 0.01) << level;
    EXPECT_GT(find(rows_b, "GARCH-FHS", level).all.kupiec_p_value, 0.01) << level;
  }
}

TEST(V17_VarBacktests, TheTestsHaveTheStatedSizeAndPower) {
  fxval::mc::NormalStream rng(20261028, 0);
  const auto uniform = [&]() {
    // 53 bits from two normals' worth of Philox output is overkill; a normal CDF of one normal is
    // uniform
    double z = 0.0;
    double unused = 0.0;
    rng.next_pair(z, unused);
    return 0.5 * std::erfc(-z / std::sqrt(2.0));
  };
  const auto rejection_rates = [&](double p, std::size_t sequences) {
    std::size_t kupiec = 0;
    std::size_t independence = 0;
    for (std::size_t s = 0; s < sequences; ++s) {
      std::vector<std::uint8_t> hits(2500);
      for (auto& h : hits) {
        h = uniform() < p ? 1 : 0;
      }
      const auto st = must(risk::evaluate_exceptions(hits, 0.99));
      kupiec += st.kupiec_p_value < 0.05 ? 1 : 0;
      independence += st.independence_p_value < 0.05 ? 1 : 0;
    }
    return std::pair{static_cast<double>(kupiec) / static_cast<double>(sequences),
                     static_cast<double>(independence) / static_cast<double>(sequences)};
  };
  const std::size_t sequences = fxval::testing::reduced_effort ? 400 : 2000;
  const auto [size_kupiec, size_independence] = rejection_rates(0.01, sequences);
  const auto [power_kupiec, unused_independence] = rejection_rates(0.02, sequences);
  std::cout << "V-17 size at 5%: Kupiec " << size_kupiec << ", independence " << size_independence
            << "; power of Kupiec against a 2% rate " << power_kupiec << " (" << sequences
            << " sequences)\n";
  const double lo = fxval::testing::reduced_effort ? 0.015 : 0.025;
  const double hi = fxval::testing::reduced_effort ? 0.10 : 0.08;
  EXPECT_GT(size_kupiec, lo);
  EXPECT_LT(size_kupiec, hi);
  EXPECT_LT(size_independence, hi);
  EXPECT_GT(power_kupiec, 0.8);
  static_cast<void>(unused_independence);
}

}  // namespace
