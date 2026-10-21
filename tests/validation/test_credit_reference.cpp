// CREDIT-REF: the CVA arithmetic and the Garman-Kohlhagen price used in the CVA checks, against
// tests/validation/data/credit_reference.csv written by python/gen_credit_reference.py (which
// shares no code with the C++: scipy closed forms and an explicit loop over intervals).
//
// Criteria, fixed before the run: CVA from a given profile within 1e-12 relative of the interval
// formula (14 cases, hazard 0 to 0.3, recovery 0 to 0.8, discount rate -1% to 6%, irregular dates);
// the exact CVA of a constant discounted exposure (the telescoped sum) within 1e-12 relative (6
// option prices); the C++ Garman-Kohlhagen price within 1e-12 relative of the table's price.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iostream>

#include "fxval/pricing/garman_kohlhagen.hpp"
#include "support/credit_test_support.hpp"

#ifndef FXVAL_CREDIT_REFERENCE_PATH
#error "FXVAL_CREDIT_REFERENCE_PATH must be defined by the build system"
#endif

namespace {

using fxval::testing::CsvTable;
using fxval::testing::must;
using fxval::testing::number;
namespace credit = fxval::credit;

double relative(double a, double b) {
  return std::abs(a - b) / std::max(std::abs(b), 1e-300);
}

TEST(CREDIT_REF_Cva, MatchesTheIndependentIntervalFormulaAndTheTelescopedSum) {
  const CsvTable table = CsvTable::load(FXVAL_CREDIT_REFERENCE_PATH);
  int cva_rows = 0;
  int option_rows = 0;
  double worst_cva = 0.0;
  double worst_exact = 0.0;
  double worst_price = 0.0;
  for (const auto& row : table.rows) {
    if (row.at("kind") == "cva") {
      const auto dates = fxval::testing::split_numbers(row.at("a"));
      const auto ee = fxval::testing::split_numbers(row.at("b"));
      const credit::CreditInputs in{number(row, "c"), number(row, "d"), number(row, "e")};
      const double got = must(credit::cva_from_profile(dates, ee, in));
      worst_cva = std::max(worst_cva, relative(got, number(row, "value")));
      ++cva_rows;
    } else if (row.at("kind") == "optq") {
      const bool call = number(row, "a") > 0.0;
      const auto type = call ? fxval::pricing::OptionType::call : fxval::pricing::OptionType::put;
      const auto m = fxval::testing::credit_market();
      const double price = must(fxval::pricing::garman_kohlhagen_price(
          fxval::pricing::VanillaFx{m.spot,
                                    must(fxval::Strike::from_value(number(row, "b"))),
                                    m.domestic,
                                    m.foreign,
                                    must(fxval::Years::from_years(number(row, "c"))),
                                    m.volatility},
          type));
      worst_price = std::max(worst_price, relative(price, number(row, "value")));
      // discounted EE = price at every date: EE(t) = price exp(rd t); dates 0, 0.2, ..., 0.8
      const std::vector<double> dates{0.0, 0.2, 0.4, 0.6, 0.8};
      std::vector<double> ee;
      for (const double t : dates) {
        ee.push_back(price * std::exp(0.03 * t));
      }
      const double got = must(credit::cva_from_profile(dates, ee, {0.03, 0.40, 0.03}));
      worst_exact = std::max(worst_exact, relative(got, number(row, "value2")));
      ++option_rows;
    }
  }
  std::cout << "CREDIT-REF " << cva_rows << " CVA rows worst " << worst_cva << "; " << option_rows
            << " option rows, worst price " << worst_price << ", worst telescoped CVA "
            << worst_exact << "\n";
  EXPECT_EQ(cva_rows, 14);
  EXPECT_EQ(option_rows, 6);
  EXPECT_LT(worst_cva, 1e-12);
  EXPECT_LT(worst_exact, 1e-12);
  EXPECT_LT(worst_price, 1e-12);
}

}  // namespace
