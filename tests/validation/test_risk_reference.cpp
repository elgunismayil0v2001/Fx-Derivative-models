// RISK-REF: VaR methods and backtest statistics against independent Python
// (tests/validation/data/risk_hits_reference.csv and risk_var_reference.csv, written by
// python/gen_risk_reference.py with numpy, scipy and a separate Garman-Kohlhagen pricer).
//
// Criteria, fixed before the first run:
//   backtest statistics (40 sequences): LR statistics within 1e-9 absolute; p-values within 1e-9
//   absolute; binomial cdf within 1e-12; traffic-light zone equal; exception count equal.
//   VaR / ES: historical, normal and Cornish-Fisher within 1e-12 relative; delta-gamma and full
//   revaluation within 1e-9 relative or 1e-7 absolute (currency units, on notionals of 1e6),
//   because they go through the pricer and the sensitivities.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <vector>

#include "fxval/risk/backtest.hpp"
#include "fxval/risk/portfolio.hpp"
#include "support/risk_test_support.hpp"

#ifndef FXVAL_RISK_HITS_REFERENCE_PATH
#error "FXVAL_RISK_HITS_REFERENCE_PATH must be defined by the build system"
#endif
#ifndef FXVAL_RISK_VAR_REFERENCE_PATH
#error "FXVAL_RISK_VAR_REFERENCE_PATH must be defined by the build system"
#endif

namespace {

using fxval::testing::CsvTable;
using fxval::testing::must;
using fxval::testing::number;

std::vector<std::string> split(const std::string& text, char sep) {
  std::vector<std::string> parts;
  std::stringstream ss(text);
  std::string cell;
  while (std::getline(ss, cell, sep)) {
    parts.push_back(cell);
  }
  return parts;
}

fxval::risk::FxPortfolio parse_portfolio(const std::string& text) {
  const auto head = split(text, '|');
  fxval::risk::FxPortfolio pf{must(fxval::Spot::from_value(std::stod(head.at(0)))),
                              must(fxval::Rate::from_decimal(std::stod(head.at(1)))),
                              must(fxval::Rate::from_decimal(std::stod(head.at(2)))),
                              std::stod(head.at(3)),
                              {}};
  for (const std::string& leg : split(head.at(4), ';')) {
    const auto f = split(leg, ':');
    pf.options.push_back(fxval::risk::OptionLeg{
        f.at(0) == "call" ? fxval::pricing::OptionType::call : fxval::pricing::OptionType::put,
        must(fxval::Strike::from_value(std::stod(f.at(1)))),
        must(fxval::Years::from_years(std::stod(f.at(2)))),
        must(fxval::Volatility::from_decimal(std::stod(f.at(3)))),
        std::stod(f.at(4))});
  }
  return pf;
}

bool close(double a, double b, double rel, double abs_tol) {
  return std::abs(a - b) <= std::max(rel * std::abs(b), abs_tol);
}

TEST(RISK_REF_BacktestStatistics, MatchIndependentPython) {
  const CsvTable table = CsvTable::load(FXVAL_RISK_HITS_REFERENCE_PATH);
  ASSERT_EQ(table.rows.size(), 40U);
  double worst_lr = 0.0;
  double worst_p = 0.0;
  for (const auto& row : table.rows) {
    std::vector<std::uint8_t> hits;
    for (char c : row.at("hits")) {
      hits.push_back(c == '1' ? 1 : 0);
    }
    const auto s = must(fxval::risk::evaluate_exceptions(hits, number(row, "level")));
    EXPECT_EQ(s.exceptions, static_cast<std::size_t>(number(row, "exceptions"))) << row.at("id");
    const auto diff = [&](double ours, const char* key) {
      return std::abs(ours - number(row, key));
    };
    worst_lr = std::max({worst_lr,
                         diff(s.kupiec_lr, "kupiec_lr"),
                         diff(s.independence_lr, "ind_lr"),
                         diff(s.conditional_lr, "cc_lr")});
    worst_p = std::max({worst_p,
                        diff(s.kupiec_p_value, "kupiec_p"),
                        diff(s.independence_p_value, "ind_p"),
                        diff(s.conditional_p_value, "cc_p")});
    EXPECT_LT(diff(s.kupiec_lr, "kupiec_lr"), 1e-9) << row.at("id");
    EXPECT_LT(diff(s.independence_lr, "ind_lr"), 1e-9) << row.at("id");
    EXPECT_LT(diff(s.conditional_lr, "cc_lr"), 1e-9) << row.at("id");
    EXPECT_LT(diff(s.kupiec_p_value, "kupiec_p"), 1e-9) << row.at("id");
    EXPECT_LT(diff(s.independence_p_value, "ind_p"), 1e-9) << row.at("id");
    EXPECT_LT(diff(s.conditional_p_value, "cc_p"), 1e-9) << row.at("id");
    EXPECT_LT(diff(s.binomial_cdf, "cdf"), 1e-12) << row.at("id");
    EXPECT_STREQ(fxval::risk::to_string(s.zone), row.at("zone").c_str()) << row.at("id");
  }
  std::cout << "RISK-REF backtest: " << table.rows.size() << " sequences, worst LR error "
            << worst_lr << ", worst p-value error " << worst_p << "\n";
}

TEST(RISK_REF_VarMethods, MatchIndependentPython) {
  const CsvTable table = CsvTable::load(FXVAL_RISK_VAR_REFERENCE_PATH);
  std::map<std::string, std::pair<int, double>> summary;  // kind -> count, worst relative error
  for (const auto& row : table.rows) {
    const std::string kind = row.at("kind");
    const double level = number(row, "level");
    double var = 0.0;
    double es = std::nan("");
    double rel = 1e-12;
    double abs_tol = 1e-13;
    if (kind == "hist") {
      std::vector<double> pnl;
      for (const std::string& x : split(row.at("a"), ';')) {
        pnl.push_back(std::stod(x));
      }
      const auto r = must(fxval::risk::historical_var_es(pnl, level));
      var = r.var;
      es = r.es;
    } else if (kind == "norm") {
      const auto r = must(fxval::risk::normal_var_es(number(row, "a"), number(row, "b"), level));
      var = r.var;
      es = r.es;
    } else if (kind == "cf") {
      var = must(fxval::risk::cornish_fisher_var(
          number(row, "a"), number(row, "b"), number(row, "c"), number(row, "d"), level));
    } else {
      rel = 1e-9;
      abs_tol = 1e-7;
      const auto pf = parse_portfolio(row.at("a"));
      const double sigma = number(row, "b");
      const auto r = kind == "dg"
                         ? must(fxval::risk::delta_gamma_var_es(pf, sigma, level))
                         : must(fxval::risk::full_revaluation_normal_var_es(pf, sigma, level));
      var = r.var;
      es = r.es;
    }
    const double ref_var = number(row, "var");
    EXPECT_TRUE(close(var, ref_var, rel, abs_tol)) << kind << " var " << var << " ref " << ref_var;
    double worst = std::abs(var - ref_var) / std::max(std::abs(ref_var), 1e-300);
    if (!row.at("es").empty()) {
      const double ref_es = number(row, "es");
      EXPECT_TRUE(close(es, ref_es, rel, abs_tol)) << kind << " es " << es << " ref " << ref_es;
      worst = std::max(worst, std::abs(es - ref_es) / std::max(std::abs(ref_es), 1e-300));
    }
    summary[kind].first += 1;
    summary[kind].second = std::max(summary[kind].second, worst);
  }
  for (const auto& [kind, v] : summary) {
    std::cout << "RISK-REF var " << kind << ": " << v.first << " rows, worst relative error "
              << v.second << "\n";
  }
}

}  // namespace
