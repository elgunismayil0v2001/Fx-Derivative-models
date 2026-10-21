// V-19: simulated exposure of an FX forward against the closed form (Black-76), and CVA of a long
// option against its exact value.
//
// Reference: tests/validation/data/credit_reference.csv, written by python/gen_credit_reference.py
// with scipy. For a forward V_t = N (a S_t - K e^{-rd tau}), a = e^{-rf tau}, tau = T - t, with S_t
// lognormal of mean S0 e^{mu t}: EE(t) = E[max(V_t, 0)] is a Black formula, PFE_q(t) the quantile
// of max(V_t, 0). The table has 60 forward rows (long and short, strikes 1.00, 1.10, 1.25,
// risk-neutral drift 2% and a real-world drift 5%, five dates), 6 option rows. 200,000 paths per
// case, one seed per case.
//
// Criteria, fixed before the run (the catalogue said "within 3 standard errors"; with 60
// comparisons a 3-sigma rule would fail by chance about once in six runs, so the family-wise bound
// is 4 standard errors):
//   A. EE: |EE - reference| <= 4 standard errors for all 60 rows, and the mean of z^2 between 0.5
//   and 1.6
//      (the standard errors are neither much too small nor too large).
//   B. PFE at 95% and 99%: the C++ PFE equals the k-th smallest exposure computed here by a full
//   sort
//      (exactly); and the share of simulated exposures not above the reference PFE is within 4
//      binomial standard errors of the level (when the reference PFE is zero: not below the level
//      minus 4 standard errors).
//   C. NETTING: a long forward at strike K1 and a short forward at strike K2 with the same maturity
//   have
//      the deterministic value N (K2 - K1) e^{-rd tau}, so EE equals max of it, exactly (1e-6).
//   D. CVA of a long vanilla option (risk-neutral) equals (1 - R) price (1 - exp(-lambda 0.8))
//   within 4
//      standard errors for the 6 option rows (calls and puts, three strikes): the discounted
//      expected exposure of a long option is its price at every date.
//   E. CONTROL, must fail: the same comparison for the 15 long, risk-neutral rows with a simulation
//   volatility
//      of 12% instead of 10% rejects at least 9 of the 15 EE values (4 standard errors).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>

#include "support/credit_test_support.hpp"

#ifndef FXVAL_CREDIT_REFERENCE_PATH
#error "FXVAL_CREDIT_REFERENCE_PATH must be defined by the build system"
#endif

namespace {

using fxval::testing::credit_market;
using fxval::testing::CsvTable;
using fxval::testing::forward_trade;
using fxval::testing::must;
using fxval::testing::number;
namespace credit = fxval::credit;

constexpr std::size_t kPaths = 200'000;
const std::vector<double> kDates{0.1, 0.3, 0.5, 0.7, 0.9};

std::size_t column_of(double t) {
  for (std::size_t i = 0; i < kDates.size(); ++i) {
    if (std::abs(kDates[i] - t) < 1e-12) {
      return i + 1;  // row 0 is t = 0
    }
  }
  throw std::runtime_error("unknown date");
}

struct Case {
  double side;
  double strike;
  double mu;
  bool operator<(const Case& o) const {
    return std::tie(side, strike, mu) < std::tie(o.side, o.strike, o.mu);
  }
};

TEST(V19_ForwardExposure, ExpectedExposureAndPfeMatchBlack76) {
  const CsvTable table = CsvTable::load(FXVAL_CREDIT_REFERENCE_PATH);
  std::map<Case, credit::ValueMatrix> sims;
  std::uint64_t seed = 1000;
  double worst_z = 0.0;
  double sum_z2 = 0.0;
  int rows = 0;
  double worst_pfe_z = 0.0;
  int pfe_checks = 0;
  for (const auto& row : table.rows) {
    if (row.at("kind") != "fwd") {
      continue;
    }
    const Case c{number(row, "a"), number(row, "b"), number(row, "c")};
    if (sims.find(c) == sims.end()) {
      credit::ExposureSettings s;
      s.dates = kDates;
      s.paths = kPaths;
      s.seed = seed++;
      const bool risk_neutral = std::abs(c.mu - 0.02) < 1e-9;
      const auto market = credit_market(risk_neutral ? std::nullopt : std::optional<double>(c.mu));
      sims.emplace(c,
                   must(credit::simulate_forward_values(
                       market, forward_trade(c.side * 1.0e6, c.strike, 1.0), s)));
    }
    const credit::ValueMatrix& v = sims.at(c);
    const std::size_t i = column_of(number(row, "d"));
    const auto profile = must(credit::exposure_profile(v, 0.95));
    const double z = (profile.ee[i] - number(row, "value")) / profile.ee_standard_error[i];
    worst_z = std::max(worst_z, std::abs(z));
    sum_z2 += z * z;
    ++rows;
    EXPECT_LT(std::abs(z), 4.0) << "EE side " << c.side << " K " << c.strike << " mu " << c.mu
                                << " t " << number(row, "d");

    // B: PFE at 95% and 99%
    std::vector<double> exposure(kPaths);
    for (std::size_t p = 0; p < kPaths; ++p) {
      exposure[p] = std::max(v.row(i)[p], 0.0);
    }
    std::vector<double> sorted = exposure;
    std::sort(sorted.begin(), sorted.end());
    const std::vector<std::pair<double, double>> levels{{0.95, number(row, "value2")},
                                                        {0.99, number(row, "e")}};
    for (const auto& [q, ref] : levels) {
      const auto at_level = must(credit::exposure_profile(v, q));
      const auto k = static_cast<std::size_t>(std::ceil(static_cast<double>(kPaths) * q - 1e-9));
      EXPECT_EQ(at_level.pfe[i], sorted[k - 1]);
      const double share = static_cast<double>(std::upper_bound(sorted.begin(), sorted.end(), ref) -
                                               sorted.begin()) /
                           static_cast<double>(kPaths);
      const double se = std::sqrt(q * (1.0 - q) / static_cast<double>(kPaths));
      const double pz = (share - q) / se;
      if (ref > 0.0) {
        worst_pfe_z = std::max(worst_pfe_z, std::abs(pz));
        EXPECT_LT(std::abs(pz), 4.0)
            << "PFE " << q << " side " << c.side << " K " << c.strike << " t " << number(row, "d");
      } else {
        EXPECT_GT(pz, -4.0);
      }
      ++pfe_checks;
    }
  }
  std::cout << "V-19 EE: " << rows << " rows, worst |z| " << worst_z << ", mean z^2 "
            << sum_z2 / rows << "; PFE: " << pfe_checks
            << " checks, worst |z| (reference PFE above 0) " << worst_pfe_z << "\n";
  EXPECT_EQ(rows, 60);
  EXPECT_GT(sum_z2 / rows, 0.5);
  EXPECT_LT(sum_z2 / rows, 1.6);
}

TEST(V19_ForwardExposure, NettingIsAppliedBeforeTheFloor) {
  credit::NettingSet set;
  set.forwards = {forward_trade(1.0e6, 1.05, 1.0), forward_trade(-1.0e6, 1.12, 1.0)};
  credit::ExposureSettings s;
  s.dates = kDates;
  s.paths = 1000;
  s.seed = 5;
  const auto net = must(credit::exposure_profile(
      must(credit::simulate_netting_set_values(credit_market(), set, s)), 0.95));
  for (std::size_t i = 0; i < kDates.size(); ++i) {
    const double tau = 1.0 - kDates[i];
    const double deterministic = 1.0e6 * (1.12 - 1.05) * std::exp(-0.03 * tau);
    EXPECT_NEAR(net.ee[i + 1], std::max(deterministic, 0.0), 1e-6);
    EXPECT_NEAR(net.pfe[i + 1], std::max(deterministic, 0.0), 1e-6);
  }
  // the opposite sign: a short position in the same pair has no exposure at all
  set.forwards = {forward_trade(-1.0e6, 1.05, 1.0), forward_trade(1.0e6, 1.12, 1.0)};
  const auto none = must(credit::exposure_profile(
      must(credit::simulate_netting_set_values(credit_market(), set, s)), 0.95));
  for (const double e : none.ee) {
    EXPECT_DOUBLE_EQ(e, 0.0);
  }
}

TEST(V19_ForwardExposure, CvaOfALongOptionIsItsPriceTimesTheDefaultProbability) {
  const CsvTable table = CsvTable::load(FXVAL_CREDIT_REFERENCE_PATH);
  const credit::CreditInputs in{0.03, 0.40, 0.03};
  double worst = 0.0;
  int count = 0;
  std::uint64_t seed = 5000;
  for (const auto& row : table.rows) {
    if (row.at("kind") != "optq") {
      continue;
    }
    const auto type =
        number(row, "a") > 0.0 ? fxval::pricing::OptionType::call : fxval::pricing::OptionType::put;
    credit::ExposureSettings s;
    s.dates = {0.2, 0.4, 0.6, 0.8};
    s.paths = kPaths;
    s.seed = seed++;
    const auto v = must(credit::simulate_option_values(
        credit_market(), fxval::testing::option_trade(type, number(row, "b"), 1.0, 0.10, 1.0), s));
    const auto est = must(credit::cva_estimate(v, in));
    const double z = (est.cva - number(row, "value2")) / est.standard_error;
    worst = std::max(worst, std::abs(z));
    EXPECT_LT(std::abs(z), 4.0) << "option K " << number(row, "b");
    EXPECT_GT(est.standard_error, 0.0);
    ++count;
  }
  std::cout << "V-19 CVA of a long option: " << count << " cases, worst |z| " << worst << "\n";
  EXPECT_EQ(count, 6);
}

TEST(V19_ForwardExposure, ControlWrongVolatilityIsRejected) {
  const CsvTable table = CsvTable::load(FXVAL_CREDIT_REFERENCE_PATH);
  const auto wrong = fxval::credit::ExposureMarket{fxval::testing::credit_market().spot,
                                                   fxval::testing::credit_market().domestic,
                                                   fxval::testing::credit_market().foreign,
                                                   must(fxval::Volatility::from_decimal(0.12)),
                                                   std::nullopt};
  std::map<double, credit::ValueMatrix> sims;
  int rejected = 0;
  int total = 0;
  for (const auto& row : table.rows) {
    if (row.at("kind") != "fwd" || number(row, "a") < 0.0 ||
        std::abs(number(row, "c") - 0.02) > 1e-9) {
      continue;
    }
    const double strike = number(row, "b");
    if (sims.find(strike) == sims.end()) {
      credit::ExposureSettings s;
      s.dates = kDates;
      s.paths = kPaths;
      s.seed = 9000;
      sims.emplace(
          strike,
          must(credit::simulate_forward_values(wrong, forward_trade(1.0e6, strike, 1.0), s)));
    }
    const auto profile = must(credit::exposure_profile(sims.at(strike), 0.95));
    const std::size_t i = column_of(number(row, "d"));
    const double z = (profile.ee[i] - number(row, "value")) / profile.ee_standard_error[i];
    rejected += std::abs(z) > 4.0 ? 1 : 0;
    ++total;
  }
  std::cout << "V-19 control (volatility 12% instead of 10%): " << rejected << " of " << total
            << " EE values rejected\n";
  EXPECT_EQ(total, 15);
  EXPECT_GE(rejected, 9);
}

}  // namespace
