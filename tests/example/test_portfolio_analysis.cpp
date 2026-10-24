// Tests of the example application (examples/portfolio). They check the application's own claims
// against independent computations: finite differences of the book revaluation, the risk layer fed
// by hand, closed-form VaR of a linear book, and identities of the credit layer.

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>

#include <gtest/gtest.h>

#ifdef FXVAL_PORTFOLIO_TEST_HAVE_JSON
#include <nlohmann/json.hpp>
#endif

#include "fxval/core/units.hpp"
#include "fxval/risk/portfolio.hpp"
#include "fxval/smile/smile_slice.hpp"
#include "fxval/timeseries/volatility_models.hpp"
#include "portfolio_analysis.hpp"
#include "report_writers.hpp"

namespace {

using namespace fxval;
using namespace fxval::example;

template <class T>
T must(Result<T> result) {
  EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
  return std::move(result).value();
}

// A scenario that runs in a fraction of a second: no Heston fit, few paths, short history.
Scenario quick() {
  Scenario s = default_scenario();
  s.run_heston = false;
  s.exposure_paths = 3000;
  s.exposure_dates = 12;
  s.history_days = 400;
  return s;
}

Scenario forward_only() {
  Scenario s = quick();
  s.options.clear();
  return s;
}

// Run only the sections a test looks at: every section is exercised by some test, but the Greek and
// finite-difference tests do not need a GARCH fit or a credit simulation on each of their many
// runs.
Scenario valuation_only(Scenario s) {
  s.run_market_risk = false;
  s.run_credit = false;
  return s;
}

Scenario without_credit(Scenario s) {
  s.run_credit = false;
  return s;
}

Scenario without_market_risk(Scenario s) {
  s.run_market_risk = false;
  return s;
}

TEST(PortfolioExample, DefaultScenarioRunsWithHestonAndEverythingIsFinite) {
  Scenario s = default_scenario();
  s.exposure_paths = 4000;
  s.history_days = 600;
  const Report r = must(analyse(s));
  ASSERT_EQ(r.positions.size(), s.forwards.size() + s.options.size());
  EXPECT_TRUE(std::isfinite(r.total_value));
  for (const PositionReport& p : r.positions) {
    EXPECT_TRUE(std::isfinite(p.value)) << p.label;
  }
  ASSERT_EQ(r.market_risk.methods.size(), 5U);
  for (const VarRow& v : r.market_risk.methods) {
    EXPECT_GT(v.var, 0.0) << v.method;
    EXPECT_GE(v.es, v.var) << v.method;
  }
  ASSERT_TRUE(r.heston.has_value());
  // Heston is calibrated to the same surface: about 0.1 vol point of error at most, so each option
  // price differs from the smile price by far less than the premium (one vol point is ~20k here).
  EXPECT_LT(r.heston->rms_vol_error, 0.002);
  for (const PositionReport& p : r.positions) {
    if (p.kind == "forward") {
      EXPECT_FALSE(p.value_heston.has_value());
      continue;
    }
    ASSERT_TRUE(p.value_heston.has_value()) << p.label;
    EXPECT_LT(std::abs(*p.value_heston - p.value), 6000.0) << p.label;
  }
}

TEST(PortfolioExample, TotalsAreTheSumOfThePositions) {
  const Report r = must(analyse(valuation_only(quick())));
  double value = 0.0;
  GreekSet sum;
  for (const PositionReport& p : r.positions) {
    value += p.value;
    sum.delta += p.greeks.delta;
    sum.gamma += p.greeks.gamma;
    sum.vega += p.greeks.vega;
    sum.theta += p.greeks.theta;
    sum.rho_domestic += p.greeks.rho_domestic;
    sum.rho_foreign += p.greeks.rho_foreign;
  }
  EXPECT_NEAR(r.total_value, value, 1e-9 * std::abs(value) + 1e-9);
  EXPECT_NEAR(r.total_greeks.delta, sum.delta, 1e-9 * std::abs(sum.delta));
  EXPECT_NEAR(r.total_greeks.gamma, sum.gamma, 1e-9 * std::abs(sum.gamma));
  EXPECT_NEAR(r.total_greeks.vega, sum.vega, 1e-9 * std::abs(sum.vega));
  EXPECT_NEAR(r.total_greeks.theta, sum.theta, 1e-9 * std::abs(sum.theta));
  EXPECT_NEAR(r.total_greeks.rho_domestic, sum.rho_domestic, 1e-9 * std::abs(sum.rho_domestic));
  EXPECT_NEAR(r.total_greeks.rho_foreign, sum.rho_foreign, 1e-9 * std::abs(sum.rho_foreign));
}

TEST(PortfolioExample, ShortAndLongOptionsAtTheSameStrikeGiveTheForward) {
  // Put-call parity at book level: long call, short put, same strike and expiry (so the same
  // smile volatility) is worth N (S e^{-r_f T} - K e^{-r_d T}), whatever the smile.
  Scenario s = valuation_only(quick());
  s.forwards.clear();
  s.options = {{"call", pricing::OptionType::call, 1.09, 0.4, 1'000'000.0},
               {"put", pricing::OptionType::put, 1.09, 0.4, -1'000'000.0}};
  const Report r = must(analyse(s));
  const double expected = 1'000'000.0 * (s.spot * std::exp(-s.foreign_rate * 0.4) -
                                         1.09 * std::exp(-s.domestic_rate * 0.4));
  EXPECT_NEAR(r.total_value, expected, 1e-9 * 1'000'000.0);
  // and the forward has no gamma and no vega
  EXPECT_NEAR(r.total_greeks.gamma, 0.0, 1e-9);
  EXPECT_NEAR(r.total_greeks.vega, 0.0, 1e-3);
}

TEST(PortfolioExample, DeltaAndGammaMatchFiniteDifferencesOfTheRevaluation) {
  const Scenario s = valuation_only(quick());
  const Report r = must(analyse(s));
  const double h = 1e-4 * s.spot;
  const double up = must(revalue(s, s.spot + h));
  const double down = must(revalue(s, s.spot - h));
  const double mid = must(revalue(s, s.spot));
  EXPECT_NEAR(mid, r.total_value, 1e-9 * std::abs(mid) + 1e-6);
  const double delta = (up - down) / (2.0 * h);
  const double gamma = (up - 2.0 * mid + down) / (h * h);
  EXPECT_NEAR(r.total_greeks.delta, delta, 1e-6 * std::abs(delta));
  double gross_gamma = 0.0;
  for (const PositionReport& p : r.positions) {
    gross_gamma += std::abs(p.greeks.gamma);
  }
  EXPECT_NEAR(r.total_greeks.gamma, gamma, 1e-3 * gross_gamma);
}

TEST(PortfolioExample, ForwardGreeksMatchFiniteDifferences) {
  // The forward's Greeks are written by hand in the application, so check each one by bumping the
  // input of a forward-only book (no smile dependence).
  const Scenario s = valuation_only(forward_only());
  const Report r = must(analyse(s));
  const double v0 = r.total_value;

  Scenario rd_up = s;
  rd_up.domestic_rate += 1e-6;
  Scenario rd_down = s;
  rd_down.domestic_rate -= 1e-6;
  EXPECT_NEAR(r.total_greeks.rho_domestic,
              (must(analyse(rd_up)).total_value - must(analyse(rd_down)).total_value) / 2e-6,
              1e-5 * std::abs(r.total_greeks.rho_domestic));

  Scenario rf_up = s;
  rf_up.foreign_rate += 1e-6;
  Scenario rf_down = s;
  rf_down.foreign_rate -= 1e-6;
  EXPECT_NEAR(r.total_greeks.rho_foreign,
              (must(analyse(rf_up)).total_value - must(analyse(rf_down)).total_value) / 2e-6,
              1e-5 * std::abs(r.total_greeks.rho_foreign));

  // theta = dV/dt = -dV/dT
  Scenario t_up = s;
  t_up.forwards[0].maturity += 1e-6;
  Scenario t_down = s;
  t_down.forwards[0].maturity -= 1e-6;
  const double theta =
      -(must(analyse(t_up)).total_value - must(analyse(t_down)).total_value) / 2e-6;
  EXPECT_NEAR(r.total_greeks.theta, theta, 1e-5 * std::abs(theta));
  EXPECT_NEAR(v0, must(revalue(s, s.spot)), 1e-9 * std::abs(v0));
}

TEST(PortfolioExample, RiskLayerSeesTheSamePnlAsTheLadder) {
  // Build the risk layer's portfolio by hand (forward = N e^{-r_f T} units of spot, options as
  // legs with the smile volatilities of the report) and compare its P&L with the ladder's.
  const Scenario s = without_credit(quick());
  const Report r = must(analyse(s));
  risk::FxPortfolio pf{must(Spot::from_value(s.spot)),
                       must(Rate::from_decimal(s.domestic_rate)),
                       must(Rate::from_decimal(s.foreign_rate)),
                       0.0,
                       {}};
  for (const ForwardInput& f : s.forwards) {
    pf.spot_units += f.notional * std::exp(-s.foreign_rate * f.maturity);
  }
  for (const PositionReport& p : r.positions) {
    if (p.kind == "forward") {
      continue;
    }
    pf.options.push_back({p.kind == "call" ? pricing::OptionType::call : pricing::OptionType::put,
                          must(Strike::from_value(p.strike)),
                          must(Years::from_years(p.expiry)),
                          must(Volatility::from_decimal(p.volatility)),
                          p.quantity});
  }
  for (const LadderPoint& l : r.ladder) {
    const double pnl = must(risk::pnl_for_simple_return(pf, l.shock));
    EXPECT_NEAR(pnl, l.pnl, 1e-7 * (std::abs(l.pnl) + 1000.0)) << "shock " << l.shock;
  }
  EXPECT_NEAR(must(risk::spot_sensitivities(pf)).delta,
              r.total_greeks.delta,
              1e-9 * std::abs(r.total_greeks.delta));
  EXPECT_NEAR(r.market_risk.portfolio_delta_units,
              r.total_greeks.delta,
              1e-9 * std::abs(r.total_greeks.delta));
}

TEST(PortfolioExample, LadderIsFullRevaluationAndTheTaylorExpansionIsLocallyAccurate) {
  Scenario s = valuation_only(quick());
  s.ladder_range = 0.10;
  s.ladder_points = 21;  // shocks in steps of 1%, the middle one is zero
  const Report r = must(analyse(s));
  ASSERT_EQ(r.ladder.size(), 21U);
  const LadderPoint& middle = r.ladder[10];
  EXPECT_NEAR(middle.shock, 0.0, 1e-15);
  EXPECT_NEAR(middle.pnl, 0.0, 1e-6);
  EXPECT_NEAR(middle.pnl_taylor, 0.0, 1e-9);
  for (const LadderPoint& l : r.ladder) {
    EXPECT_NEAR(l.spot, s.spot * (1.0 + l.shock), 1e-12);
    EXPECT_NEAR(l.value - r.total_value, l.pnl, 1e-9 * (1.0 + std::abs(l.pnl)));
    EXPECT_NEAR(l.value, must(revalue(s, l.spot)), 1e-9 * (1.0 + std::abs(l.value)));
  }
  // The error of a second-order expansion is third order in the move: at 1% it is far smaller than
  // at 10% (a factor 1000 in theory; require 100).
  const double err_small = std::abs(r.ladder[11].pnl - r.ladder[11].pnl_taylor);
  const double err_large = std::abs(r.ladder[20].pnl - r.ladder[20].pnl_taylor);
  EXPECT_GT(err_large, 100.0 * err_small);
  EXPECT_LT(err_small, 0.01 * std::abs(r.ladder[11].pnl));
}

TEST(PortfolioExample, VarOfALinearBookIsTheNormalQuantileTimesItsVolatility) {
  const Scenario s = without_credit(forward_only());
  const Report r = must(analyse(s));
  const double z99 = 2.3263478740408408;
  const double expected =
      z99 * r.market_risk.garch.sigma_next * std::abs(r.total_greeks.delta) * s.spot;
  ASSERT_EQ(r.market_risk.methods.front().method, "Delta-normal");
  EXPECT_NEAR(r.market_risk.methods.front().var, expected, 1e-9 * expected);
  // For a linear book the full revaluation on the normal grid agrees up to the grid error.
  EXPECT_NEAR(r.market_risk.methods[2].var, expected, 2e-3 * expected);
  // The delta-gamma method has no gamma to add.
  EXPECT_NEAR(r.market_risk.methods[1].var, expected, 1e-6 * expected);
}

TEST(PortfolioExample, HistogramCountsEveryScenario) {
  const Scenario s = without_credit(quick());
  const Report r = must(analyse(s));
  const auto& h = r.market_risk.pnl_histogram;
  ASSERT_EQ(h.counts.size(), s.histogram_bins);
  EXPECT_EQ(std::accumulate(h.counts.begin(), h.counts.end(), std::size_t{0}),
            r.market_risk.garch.observations);
  // the smallest P&L is the lower edge and the largest the upper edge, so both end bins are used
  EXPECT_GE(h.counts.front(), 1U);
  EXPECT_GE(h.counts.back(), 1U);
  EXPECT_LT(h.lower, 0.0);
  EXPECT_GT(h.upper, 0.0);
  EXPECT_LE(-r.market_risk.historical_var, 0.0);
  EXPECT_GE(-r.market_risk.historical_var, h.lower);
}

TEST(PortfolioExample, NettedCvaIsAtMostTheSumOfTheStandaloneCvas) {
  const Report r = must(analyse(without_market_risk(quick())));
  const CreditReport& c = r.credit;
  EXPECT_GE(c.cva, 0.0);
  EXPECT_LE(c.cva, c.standalone_sum + 1e-9);
  EXPECT_GT(c.netting_benefit, 0.0);  // the straddle and the risk reversal offset each other
  EXPECT_NEAR(c.netting_benefit, c.standalone_sum - c.cva, 1e-9 * c.standalone_sum);
  // Estimate from paths and from the expected-exposure profile are the same number up to rounding
  // (same weights applied to the same mean).
  EXPECT_NEAR(c.cva, c.cva_from_profile, 1e-9 * (1.0 + c.cva));
  ASSERT_EQ(c.dates.size(), c.ee.size());
  ASSERT_EQ(c.dates.size(), c.pfe.size());
  for (std::size_t i = 0; i < c.ee.size(); ++i) {
    EXPECT_GE(c.ee[i], 0.0);
    EXPECT_GE(c.pfe[i] + 1e-9, c.ee[i]) << "date " << c.dates[i];
  }
}

TEST(PortfolioExample, TheVolatilityForecastIsTheNextDayVarianceOfAnIndependentGarchFit) {
  Scenario s = without_credit(quick());
  s.returns = synthetic_returns(500, 3);
  const Report r = must(analyse(s));
  const auto fit = must(timeseries::fit_garch(s.returns, timeseries::GarchModel::garch));
  EXPECT_EQ(fit.variance_path.size(), s.returns.size() + 1);
  EXPECT_NEAR(r.market_risk.garch.sigma_next, std::sqrt(fit.variance_path.back()), 1e-12);
  EXPECT_NEAR(r.market_risk.garch.alpha, fit.params.alpha, 1e-12);
  EXPECT_NEAR(r.market_risk.garch.beta, fit.params.beta, 1e-12);
  EXPECT_NEAR(r.market_risk.garch.sigma_unconditional,
              std::sqrt(fit.params.unconditional_variance()),
              1e-12);
}

TEST(PortfolioExample, CvaEqualsTheTrapezoidFormulaOnTheReportedExposureProfile) {
  // CVA = (1 - R) sum_i [DF(t_{i-1}) EE(t_{i-1}) + DF(t_i) EE(t_i)] / 2 (Q(t_{i-1}) - Q(t_i)),
  // DF = exp(-r_d t), Q = exp(-lambda t): written again here from docs/portfolio_mathematics.md.
  const Scenario s = without_market_risk(quick());
  const Report r = must(analyse(s));
  const CreditReport& c = r.credit;
  ASSERT_GE(c.dates.size(), 2U);
  double sum = 0.0;
  for (std::size_t i = 1; i < c.dates.size(); ++i) {
    const double a = std::exp(-s.domestic_rate * c.dates[i - 1]) * c.ee[i - 1];
    const double b = std::exp(-s.domestic_rate * c.dates[i]) * c.ee[i];
    const double dq =
        std::exp(-s.hazard_rate * c.dates[i - 1]) - std::exp(-s.hazard_rate * c.dates[i]);
    sum += 0.5 * (a + b) * dq;
  }
  const double expected = (1.0 - s.recovery) * sum;
  EXPECT_NEAR(c.cva_from_profile, expected, 1e-12 * (1.0 + expected));
  EXPECT_NEAR(c.cva, expected, 1e-9 * (1.0 + expected));
}

TEST(PortfolioExample, ASingleTradeHasNoNettingBenefit) {
  const Report r = must(analyse(without_market_risk(forward_only())));
  ASSERT_EQ(r.credit.standalone.size(), 1U);
  EXPECT_NEAR(r.credit.cva, r.credit.standalone[0].cva, 1e-12 * (1.0 + r.credit.cva));
  EXPECT_NEAR(r.credit.netting_benefit, 0.0, 1e-9);
}

TEST(PortfolioExample, CvaScalesWithLossGivenDefaultAndVanishesWithoutHazard) {
  Scenario s = without_market_risk(quick());
  s.recovery = 0.0;
  const double full = must(analyse(s)).credit.cva;
  s.recovery = 0.4;
  const double partial = must(analyse(s)).credit.cva;
  EXPECT_NEAR(partial, 0.6 * full, 1e-9 * full);
  s.hazard_rate = 0.0;
  EXPECT_EQ(must(analyse(s)).credit.cva, 0.0);
}

TEST(PortfolioExample, ABookOfShortOptionsOnlyHasNoCva) {
  Scenario s = without_market_risk(quick());
  s.forwards.clear();
  s.options = {{"short call", pricing::OptionType::call, 1.10, 0.5, -1'000'000.0},
               {"short put", pricing::OptionType::put, 1.05, 0.5, -1'000'000.0}};
  const Report r = must(analyse(s));
  EXPECT_EQ(r.credit.cva, 0.0);
  // the opposite book (all long) does have credit exposure
  for (OptionInput& o : s.options) {
    o.quantity = -o.quantity;
  }
  EXPECT_GT(must(analyse(s)).credit.cva, 0.0);
}

TEST(PortfolioExample, AnOptionAtAPivotStrikeGetsThePivotVolatilityOfItsSlice) {
  // Strike at the 25-delta call of the 3M slice: the application must read back exactly the quoted
  // 25-delta call volatility (ATM + RR/2 + BF), and at the 25-delta put the quoted put volatility.
  Scenario s = valuation_only(quick());
  const SmileQuote& q = s.smile[1];  // 3M
  ASSERT_DOUBLE_EQ(q.expiry, 0.25);
  const smile::SmileMarket market{must(Spot::from_value(s.spot)),
                                  must(Rate::from_decimal(s.domestic_rate)),
                                  must(Rate::from_decimal(s.foreign_rate)),
                                  must(Years::from_years(q.expiry))};
  const smile::DeltaQuotes quotes{must(Volatility::from_decimal(q.atm)),
                                  must(smile::VolSpread::from_decimal(q.risk_reversal)),
                                  must(smile::VolSpread::from_decimal(q.butterfly)),
                                  0.25};
  const auto slice =
      must(smile::SmileSlice::from_quotes(market, quotes, smile::DeltaConvention::spot));
  s.forwards.clear();
  s.options = {{"call", pricing::OptionType::call, slice.pivot_strikes().call, 0.25, 1.0},
               {"put", pricing::OptionType::put, slice.pivot_strikes().put, 0.25, 1.0}};
  const Report r = must(analyse(s));
  EXPECT_NEAR(r.positions[0].volatility, slice.pivot_volatilities().call.value(), 1e-9);
  EXPECT_NEAR(r.positions[1].volatility, slice.pivot_volatilities().put.value(), 1e-9);
  EXPECT_NEAR(
      slice.pivot_volatilities().call.value(), q.atm + 0.5 * q.risk_reversal + q.butterfly, 1e-12);
}

TEST(PortfolioExample, FlatAtmValueDiffersFromSmileValueOnlyThroughTheSmile) {
  Scenario s = valuation_only(quick());
  for (SmileQuote& q : s.smile) {
    q.risk_reversal = 0.0;
    q.butterfly = 0.0;
  }
  s.options = {{"atm call", pricing::OptionType::call, 1.0850, 0.25, 1'000'000.0}};
  s.forwards.clear();
  const Report r = must(analyse(s));
  // With a flat smile the volatility at any strike is the ATM one, so both prices agree to the
  // accuracy of the delta-neutral ATM strike versus the forward strike (well below a vol point).
  ASSERT_TRUE(r.positions[0].value_flat_atm.has_value());
  EXPECT_NEAR(
      r.positions[0].value, *r.positions[0].value_flat_atm, 0.01 * std::abs(r.positions[0].value));
}

TEST(PortfolioExample, TheAnalysisIsDeterministic) {
  const Scenario s = quick();
  EXPECT_EQ(to_json(must(analyse(s))), to_json(must(analyse(s))));
  Scenario other = s;
  other.seed += 1;
  EXPECT_NE(to_json(must(analyse(s))), to_json(must(analyse(other))));
}

TEST(PortfolioExample, ReportsAreWellFormed) {
  const Report r = must(analyse(quick()));
  const std::string json = to_json(r);
  EXPECT_EQ(json.find("nan"), std::string::npos);
  EXPECT_EQ(json.find("inf"), std::string::npos);
#ifdef FXVAL_PORTFOLIO_TEST_HAVE_JSON
  const auto parsed = nlohmann::json::parse(json);
  EXPECT_EQ(parsed.at("pair"), "EURUSD");
  EXPECT_EQ(parsed.at("positions").size(), r.positions.size());
  EXPECT_EQ(parsed.at("ladder").size(), r.ladder.size());
  EXPECT_EQ(parsed.at("market_risk").at("methods").size(), 5U);
  EXPECT_TRUE(parsed.at("heston").is_null());
  EXPECT_NEAR(
      parsed.at("total_value").get<double>(), r.total_value, 1e-6 * std::abs(r.total_value));
#endif
  const std::string html = to_html(r);
  EXPECT_EQ(html.find("/*REPORT_JSON*/null"), std::string::npos);
  EXPECT_NE(html.find("\"total_value\""), std::string::npos);
  EXPECT_EQ(html.find("http://"), std::string::npos);  // nothing is fetched from the network
  EXPECT_EQ(html.find("https://"), std::string::npos);
  const std::string text = to_text(r);
  EXPECT_NE(text.find("Total value"), std::string::npos);
  EXPECT_NE(text.find("Filtered historical simulation"), std::string::npos);
}

TEST(PortfolioExample, NonFiniteNumbersAndMarkupAreEscapedInTheJson) {
  Report r;
  r.pair = "EUR\"</script>USD";
  r.total_value = std::nan("");
  const std::string json = to_json(r);
  EXPECT_NE(json.find("\"total_value\":null"), std::string::npos);
  EXPECT_EQ(json.find("</script>"), std::string::npos);
  EXPECT_NE(json.find("\\\""), std::string::npos);
}

TEST(PortfolioExample, OptionsOutsideTheQuotedExpiriesAreRefusedButShortBooksStillRun) {
  Scenario s = without_market_risk(quick());
  s.options = {
      {"too long", pricing::OptionType::call, 1.10, 2.0, 1'000'000.0}};  // surface ends at 1Y
  const auto refused = analyse(s);
  ASSERT_FALSE(refused.has_value());
  EXPECT_NE(refused.error().message.find("too long"), std::string::npos);

  // A book that matures before the first quoted expiry (1M) has no smile to read, but a forward
  // does not need one; the exposure volatility is then read at the first quoted expiry.
  Scenario shorter = without_market_risk(forward_only());
  shorter.forwards[0].maturity = 0.03;
  const Report r = must(analyse(shorter));
  EXPECT_NEAR(r.credit.exposure_volatility, shorter.smile.front().atm, 0.002);
  EXPECT_TRUE(std::isfinite(r.credit.cva));
}

TEST(PortfolioExample, InvalidScenariosAreRefusedWithAnError) {
  Scenario none = quick();
  none.forwards.clear();
  none.options.clear();
  EXPECT_FALSE(analyse(none).has_value());

  Scenario no_smile = quick();
  no_smile.smile.clear();
  EXPECT_FALSE(analyse(no_smile).has_value());

  Scenario bad_strike = quick();
  bad_strike.options[0].strike = -1.0;
  const auto r = analyse(bad_strike);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::invalid_argument);

  Scenario bad_spot = quick();
  bad_spot.spot = 0.0;
  EXPECT_FALSE(analyse(bad_spot).has_value());

  Scenario bad_level = quick();
  bad_level.var_level = 1.5;
  EXPECT_FALSE(analyse(bad_level).has_value());

  Scenario bad_recovery = quick();
  bad_recovery.recovery = 1.0;
  EXPECT_FALSE(analyse(bad_recovery).has_value());

  Scenario one_point = quick();
  one_point.ladder_points = 1;
  EXPECT_FALSE(analyse(one_point).has_value());
}

TEST(PortfolioExample, ReturnsAreParsedFromTextAndBadTextIsRefused) {
  std::string text = "# daily log returns\ndate,return\n";
  for (int i = 0; i < 300; ++i) {
    text += "2024-01-01," + std::to_string(0.001 * ((i % 7) - 3)) + "\n";
  }
  const auto ok = parse_returns(text);
  ASSERT_TRUE(ok.has_value());
  EXPECT_EQ(ok.value().size(), 300U);

  EXPECT_EQ(parse_returns("0.01\n0.02\n").error().code, ErrorCode::invalid_argument);  // too short
  std::string garbage = "x\n";
  for (int i = 0; i < 300; ++i) {
    garbage += "0.001\n";
  }
  garbage += "abc\n";
  EXPECT_EQ(parse_returns(garbage).error().code, ErrorCode::parse_error);
  std::string huge = "0.001\n";
  for (int i = 0; i < 300; ++i) {
    huge += "0.002\n";
  }
  huge += "5.0\n";  // not a daily log return
  EXPECT_EQ(parse_returns(huge).error().code, ErrorCode::parse_error);
  EXPECT_EQ(parse_returns("nan\n").error().code, ErrorCode::parse_error);
}

TEST(PortfolioExample, SuppliedReturnsReplaceTheSyntheticHistory) {
  Scenario s = without_credit(quick());
  s.returns = synthetic_returns(500, 7);
  const Report r = must(analyse(s));
  EXPECT_FALSE(r.market_risk.garch.synthetic);
  EXPECT_EQ(r.market_risk.garch.observations, 500U);
}

TEST(PortfolioExample, SyntheticReturnsAreDeterministicWithTheExpectedScale) {
  const auto a = synthetic_returns(20000, 11);
  EXPECT_EQ(a, synthetic_returns(20000, 11));
  EXPECT_NE(a, synthetic_returns(20000, 12));
  double sum = 0.0;
  double sq = 0.0;
  for (const double x : a) {
    sum += x;
    sq += x * x;
  }
  const double n = static_cast<double>(a.size());
  EXPECT_NEAR(sum / n, 0.0, 3e-4);
  // unconditional standard deviation sqrt(omega / (1 - alpha - beta)) = 0.00548
  EXPECT_NEAR(std::sqrt(sq / n), 0.00548, 0.0006);
}

}  // namespace
