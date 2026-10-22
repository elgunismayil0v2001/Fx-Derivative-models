// Unit tests of VaR/ES, the portfolio methods and the backtest statistics: hand-computed values,
// limits, orderings and error paths.

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "fxval/risk/backtest.hpp"
#include "fxval/risk/portfolio.hpp"
#include "support/risk_test_support.hpp"

namespace {

using fxval::ErrorCode;
using fxval::testing::must;
namespace risk = fxval::risk;

TEST(VarTest, HistoricalByHand) {
  // 10 scenarios, 80%: k = ceil(10 * 0.2) = 2 -> VaR is the 2nd worst, ES the mean of the 2 worst.
  const std::vector<double> pnl{5, -3, 2, -10, 7, -1, 4, -6, 0, 3};
  const auto r = must(risk::historical_var_es(pnl, 0.8));
  EXPECT_DOUBLE_EQ(r.var, 6.0);
  EXPECT_DOUBLE_EQ(r.es, 8.0);
}

TEST(VarTest, ExactMultipleDoesNotRoundUp) {
  std::vector<double> pnl(100);
  for (int i = 0; i < 100; ++i) {
    pnl[static_cast<std::size_t>(i)] = i;  // 0..99
  }
  // 99%: k = ceil(100 * 0.01) = 1 (the product 0.01 * 100 must not be read as 1 + eps).
  EXPECT_DOUBLE_EQ(must(risk::historical_var_es(pnl, 0.99)).var, 0.0);
}

TEST(VarTest, NormalMatchesKnownQuantiles) {
  const auto r = must(risk::normal_var_es(0.0, 1.0, 0.99));
  EXPECT_NEAR(r.var, 2.3263478740408408, 1e-12);
  EXPECT_NEAR(r.es, 2.6652142203689530, 1e-10);
  EXPECT_DOUBLE_EQ(must(risk::normal_var_es(1.5, 0.0, 0.99)).var, -1.5);
}

TEST(VarTest, EsIsAtLeastVarAndCornishFisherReducesToNormal) {
  for (double level : {0.9, 0.95, 0.975, 0.99}) {
    const auto r = must(risk::normal_var_es(0.2, 1.3, level));
    EXPECT_GE(r.es, r.var);
    EXPECT_NEAR(must(risk::cornish_fisher_var(0.2, 1.3, 0.0, 0.0, level)), r.var, 1e-12);
  }
}

TEST(VarTest, ErrorPaths) {
  EXPECT_FALSE(risk::historical_var_es({}, 0.99).has_value());
  const std::vector<double> bad{1.0, std::nan("")};
  EXPECT_FALSE(risk::historical_var_es(bad, 0.99).has_value());
  const std::vector<double> ok{1.0, 2.0};
  EXPECT_FALSE(risk::historical_var_es(ok, 1.0).has_value());
  EXPECT_FALSE(risk::historical_var_es(ok, 0.0).has_value());
  EXPECT_FALSE(risk::normal_var_es(0.0, -1.0, 0.99).has_value());
  EXPECT_FALSE(risk::normal_var_es(std::nan(""), 1.0, 0.99).has_value());
  EXPECT_FALSE(risk::cornish_fisher_var(0.0, 1.0, std::nan(""), 0.0, 0.99).has_value());
}

TEST(PortfolioRiskTest, SpotOnlyPortfolioIsLinear) {
  const auto pf = fxval::testing::spot_portfolio(1000.0);
  EXPECT_NEAR(must(risk::pnl_for_simple_return(pf, 0.01)), 1000.0 * 1.10 * 0.01, 1e-9);
  EXPECT_NEAR(
      must(risk::pnl_for_log_return(pf, 0.01)), 1000.0 * 1.10 * (std::exp(0.01) - 1.0), 1e-9);
  const auto s = must(risk::spot_sensitivities(pf));
  EXPECT_DOUBLE_EQ(s.delta, 1000.0);
  EXPECT_DOUBLE_EQ(s.gamma, 0.0);
}

TEST(PortfolioRiskTest, DeltaAndGammaMatchFiniteDifferencesOfTheValue) {
  auto pf = fxval::testing::spot_portfolio(0.0);
  pf.options.push_back(risk::OptionLeg{fxval::pricing::OptionType::call,
                                       must(fxval::Strike::from_value(1.12)),
                                       must(fxval::Years::from_years(0.5)),
                                       must(fxval::Volatility::from_decimal(0.09)),
                                       2e6});
  const double h = 1e-4;
  const double up = must(risk::portfolio_value(pf, 1.10 + h));
  const double mid = must(risk::portfolio_value(pf, 1.10));
  const double dn = must(risk::portfolio_value(pf, 1.10 - h));
  const auto s = must(risk::spot_sensitivities(pf));
  EXPECT_NEAR(s.delta, (up - dn) / (2 * h), 1e-3 * std::abs(s.delta));
  EXPECT_NEAR(s.gamma, (up - 2 * mid + dn) / (h * h), 1e-3 * std::abs(s.gamma));
}

TEST(PortfolioRiskTest, DeltaGammaWithZeroGammaIsDeltaNormal) {
  const auto pf = fxval::testing::spot_portfolio(1e6);
  const double sigma = 0.01;
  EXPECT_NEAR(must(risk::delta_gamma_var_es(pf, sigma, 0.99)).var,
              must(risk::delta_normal_var_es(pf, sigma, 0.99)).var,
              1e-8);
}

TEST(PortfolioRiskTest, FullRevaluationConvergesWithTheGrid) {
  auto pf = fxval::testing::spot_portfolio(0.0);
  pf.options.push_back(risk::OptionLeg{fxval::pricing::OptionType::call,
                                       must(fxval::Strike::from_value(1.10)),
                                       must(fxval::Years::from_years(0.25)),
                                       must(fxval::Volatility::from_decimal(0.08)),
                                       -1e6});
  const double coarse = must(risk::full_revaluation_normal_var_es(pf, 0.01, 0.99, 10000)).var;
  const double fine = must(risk::full_revaluation_normal_var_es(pf, 0.01, 0.99, 400000)).var;
  const double finer = must(risk::full_revaluation_normal_var_es(pf, 0.01, 0.99, 800000)).var;
  EXPECT_LT(std::abs(fine - finer), std::abs(coarse - finer));
  EXPECT_NEAR(fine, finer, 2e-3 * finer);
}

TEST(PortfolioRiskTest, FilteredHistoricalScalesTheResiduals) {
  const auto pf = fxval::testing::spot_portfolio(1e6);
  const std::vector<double> z{-2.0, -1.0, 0.0, 1.0, 2.0};
  const auto a = must(risk::filtered_historical_var_es(pf, z, 0.01, 0.8));
  const auto b = must(risk::filtered_historical_var_es(pf, z, 0.02, 0.8));
  // The worst residual is -2: the P&L is 1e6 * 1.10 * (exp(-2 sigma) - 1), which is not linear in
  // sigma.
  EXPECT_NEAR(b.var / a.var, (1.0 - std::exp(-0.04)) / (1.0 - std::exp(-0.02)), 1e-9);
}

TEST(PortfolioRiskTest, ErrorPaths) {
  const auto pf = fxval::testing::spot_portfolio(1e6);
  EXPECT_FALSE(risk::delta_normal_var_es(pf, -0.01, 0.99).has_value());
  EXPECT_FALSE(risk::full_revaluation_normal_var_es(pf, 0.01, 0.99, 10).has_value());
  EXPECT_FALSE(risk::pnl_for_simple_return(pf, -1.0).has_value());
  EXPECT_FALSE(risk::pnl_for_log_return(pf, std::nan("")).has_value());
  EXPECT_FALSE(risk::portfolio_value(pf, 0.0).has_value());
  EXPECT_FALSE(risk::filtered_historical_var_es(pf, std::vector<double>{}, 0.01, 0.99).has_value());
}

TEST(BacktestTest, KupiecAndIndependenceByHand) {
  // 250 days, 5 exceptions at 99%: phat = 0.02. LR_pof = -2 ln[(.99)^245 (.01)^5] + 2 ln[(.98)^245
  // (.02)^5].
  std::vector<std::uint8_t> hits(250, 0);
  for (std::size_t i : {10U, 60U, 110U, 160U, 210U}) {
    hits[i] = 1;
  }
  const auto s = must(risk::evaluate_exceptions(hits, 0.99));
  const double lr = -2.0 * (245.0 * std::log(0.99) + 5.0 * std::log(0.01)) +
                    2.0 * (245.0 * std::log(0.98) + 5.0 * std::log(0.02));
  EXPECT_NEAR(s.kupiec_lr, lr, 1e-12);
  EXPECT_NEAR(s.kupiec_p_value, std::erfc(std::sqrt(0.5 * lr)), 1e-12);
  EXPECT_EQ(s.zone, risk::TrafficLight::yellow);  // 5 exceptions in 250 days at 99%
  EXPECT_NEAR(s.independence_lr, 0.0, 0.5);       // isolated exceptions: no clustering evidence
}

TEST(BacktestTest, BaselZonesForTwoHundredFiftyDays) {
  const auto zone = [](std::size_t x) {
    std::vector<std::uint8_t> hits(250, 0);
    for (std::size_t i = 0; i < x; ++i) {
      hits[i * 20] = 1;
    }
    return must(risk::evaluate_exceptions(hits, 0.99)).zone;
  };
  EXPECT_EQ(zone(0), risk::TrafficLight::green);
  EXPECT_EQ(zone(4), risk::TrafficLight::green);
  EXPECT_EQ(zone(5), risk::TrafficLight::yellow);
  EXPECT_EQ(zone(9), risk::TrafficLight::yellow);
  EXPECT_EQ(zone(10), risk::TrafficLight::red);
}

TEST(BacktestTest, ClusteredExceptionsAreFlaggedByTheIndependenceTest) {
  std::vector<std::uint8_t> hits(1000, 0);
  for (std::size_t start : {100U, 400U, 700U}) {
    for (std::size_t i = 0; i < 4; ++i) {
      hits[start + i] = 1;  // 12 exceptions in three runs of four: right count, wrong pattern
    }
  }
  const auto s = must(risk::evaluate_exceptions(hits, 0.99));
  EXPECT_GT(s.kupiec_p_value, 0.5);
  EXPECT_LT(s.independence_p_value, 0.001);
  EXPECT_LT(s.conditional_p_value, 0.001);
}

TEST(BacktestTest, AllZeroAndAllOneSequencesAreFinite) {
  const std::vector<std::uint8_t> none(100, 0);
  const auto a = must(risk::evaluate_exceptions(none, 0.99));
  EXPECT_TRUE(std::isfinite(a.kupiec_lr));
  EXPECT_EQ(a.exceptions, 0U);
  const std::vector<std::uint8_t> all(30, 1);
  const auto b = must(risk::evaluate_exceptions(all, 0.99));
  EXPECT_TRUE(std::isfinite(b.kupiec_lr));
  EXPECT_EQ(b.zone, risk::TrafficLight::red);
}

TEST(BacktestTest, ErrorPaths) {
  EXPECT_FALSE(risk::evaluate_exceptions({}, 0.99).has_value());
  const std::vector<std::uint8_t> bad{0, 2, 1};
  EXPECT_FALSE(risk::evaluate_exceptions(bad, 0.99).has_value());
  const std::vector<std::uint8_t> ok{0, 1, 0};
  EXPECT_FALSE(risk::evaluate_exceptions(ok, 1.0).has_value());
}

TEST(BacktestTest, RollingBacktestPassesOnlyThePastWindowAndCountsExceptions) {
  const std::vector<double> returns{0.01, 0.02, -0.03, 0.01, -0.05, 0.02};
  std::vector<std::size_t> seen_sizes;
  std::vector<double> last_in_window;
  const risk::VarForecaster forecaster =
      [&](std::span<const double> past) -> fxval::Result<double> {
    seen_sizes.push_back(past.size());
    last_in_window.push_back(past.back());
    return 0.04;  // a constant VaR of 0.04 in P&L units
  };
  const risk::PnlFunction pnl = [](double r) -> fxval::Result<double> {
    return r;
  };
  const auto run = must(risk::rolling_backtest(returns, 3, forecaster, pnl));
  EXPECT_EQ(run.days, (std::vector<std::size_t>{3, 4, 5}));
  EXPECT_EQ(seen_sizes, (std::vector<std::size_t>{3, 3, 3}));
  EXPECT_EQ(last_in_window,
            (std::vector<double>{-0.03, 0.01, -0.05}));       // the day BEFORE each forecast
  EXPECT_EQ(run.hits, (std::vector<std::uint8_t>{0, 1, 0}));  // only -0.05 loses more than 0.04
  EXPECT_FALSE(risk::rolling_backtest(returns, 0, forecaster, pnl).has_value());
  EXPECT_FALSE(risk::rolling_backtest(returns, 6, forecaster, pnl).has_value());
}

TEST(PortfolioRiskTest, HistoricalRevaluationUsesTheLossTail) {
  // Ten log returns, spot position of 1000 units: P&L = 1000 * S0 * (e^r - 1). At 80% the VaR is
  // the 2nd worst loss and the ES the mean of the 2 worst; a wrong tail (level 0.2) would give the
  // 8th.
  const auto pf = fxval::testing::spot_portfolio(1000.0);
  const std::vector<double> returns{
      0.01, -0.02, 0.005, -0.05, 0.03, -0.01, 0.02, -0.03, 0.0, 0.015};
  const double s0 = pf.spot.value();
  const double worst = -1000.0 * s0 * (std::exp(-0.05) - 1.0);
  const double second = -1000.0 * s0 * (std::exp(-0.03) - 1.0);
  const auto r = must(risk::historical_revaluation_var_es(pf, returns, 0.8));
  EXPECT_NEAR(r.var, second, 1e-9);
  EXPECT_NEAR(r.es, 0.5 * (worst + second), 1e-9);
  EXPECT_GT(r.var, 0.0);
}

// F-23: a tail sum that overflows used to be reported as a successful infinite expected shortfall.
TEST(VarTest, HistoricalRefusesATailSumThatOverflows) {
  const double big = std::numeric_limits<double>::max();
  const std::vector<double> pnl{-big, -big, 1.0, 2.0};
  const auto r = risk::historical_var_es(pnl, 0.5);  // tail holds the two worst scenarios
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::numerical_failure);
  // Rescaled, the same scenarios are fine.
  const std::vector<double> scaled{-1.0, -1.0, 1e-300, 2e-300};
  EXPECT_TRUE(risk::historical_var_es(scaled, 0.5).has_value());
}

TEST(VarTest, ParametricMethodsRefuseOverflow) {
  const double big = std::numeric_limits<double>::max();
  const auto normal = risk::normal_var_es(-big, big, 0.99);
  ASSERT_FALSE(normal.has_value());
  EXPECT_EQ(normal.error().code, ErrorCode::numerical_failure);
  const auto cf = risk::cornish_fisher_var(0.0, big, 1e10, 1e10, 0.99);
  ASSERT_FALSE(cf.has_value());
  EXPECT_EQ(cf.error().code, ErrorCode::numerical_failure);
  EXPECT_TRUE(risk::normal_var_es(0.0, 1.0, 0.99).has_value());
}

}  // namespace
