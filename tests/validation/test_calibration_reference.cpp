// CAL-REF: the price -> implied volatility chain and the model -> delta quotes map against an
// independent Python implementation (tests/validation/data/heston_iv_reference.csv and
// calibration_reference.csv, written by python/gen_calibration_reference.py).
//
// The Python side prices Heston with the textbook P1/P2 probabilities and scipy QUADPACK,
// inverts with scipy brentq, and finds delta strikes by nested root finding on the spot-form
// delta. The C++ side uses the Lewis integral, its own Gauss-Kronrod routine, its own Brent and
// the closed-form delta inverse.
//
// Criteria, fixed before the first run: implied volatilities and quotes agree to 1e-8 (decimal
// volatility; the Python quadrature is accurate to about 1e-11 in price).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "support/calibration_test_support.hpp"

#ifndef FXVAL_HESTON_IV_REFERENCE_PATH
#error "FXVAL_HESTON_IV_REFERENCE_PATH must be defined by the build system"
#endif
#ifndef FXVAL_CALIBRATION_REFERENCE_PATH
#error "FXVAL_CALIBRATION_REFERENCE_PATH must be defined by the build system"
#endif

namespace {

using fxval::calibration::heston_delta_quotes;
using fxval::calibration::heston_implied_volatility;
using fxval::smile::DeltaConvention;
using fxval::testing::CsvTable;
using fxval::testing::make_heston_market;
using fxval::testing::make_params;
using fxval::testing::make_smile_market;
using fxval::testing::must;
using fxval::testing::number;

constexpr double kTolerance = 1e-8;

TEST(CAL_REF_IndependentPythonImplementation, HestonImpliedVolatilityMatches) {
  const CsvTable table = CsvTable::load(FXVAL_HESTON_IV_REFERENCE_PATH);
  ASSERT_GE(table.rows.size(), 150U);
  double worst = 0.0;
  for (const auto& row : table.rows) {
    const auto params = make_params(number(row, "v0"),
                                    number(row, "kappa"),
                                    number(row, "theta"),
                                    number(row, "xi"),
                                    number(row, "rho"));
    const auto market = make_heston_market(number(row, "spot"),
                                           number(row, "strike"),
                                           number(row, "rd"),
                                           number(row, "rf"),
                                           number(row, "t"));
    const double implied = must(heston_implied_volatility(market, params)).value();
    const double error = std::abs(implied - number(row, "iv"));
    worst = std::max(worst, error);
    EXPECT_LT(error, kTolerance) << "strike " << number(row, "strike") << " t " << number(row, "t");
  }
  RecordProperty("worst_iv_error", std::to_string(worst));
  std::cout << "CAL-REF implied volatility: " << table.rows.size() << " rows, worst error " << worst
            << "\n";
}

TEST(CAL_REF_IndependentPythonImplementation, DeltaQuotesMatch) {
  const CsvTable table = CsvTable::load(FXVAL_CALIBRATION_REFERENCE_PATH);
  ASSERT_GE(table.rows.size(), 100U);
  double worst = 0.0;
  for (const auto& row : table.rows) {
    const auto params = make_params(number(row, "v0"),
                                    number(row, "kappa"),
                                    number(row, "theta"),
                                    number(row, "xi"),
                                    number(row, "rho"));
    const DeltaConvention convention =
        row.at("convention") == "spot" ? DeltaConvention::spot : DeltaConvention::forward;
    const auto market = make_smile_market(
        number(row, "spot"), number(row, "rd"), number(row, "rf"), number(row, "t"));
    const auto quotes = must(heston_delta_quotes(market, params, 0.25, convention));
    const double e_atm = std::abs(quotes.atm.value() - number(row, "atm"));
    const double e_rr = std::abs(quotes.risk_reversal.value() - number(row, "rr"));
    const double e_bf = std::abs(quotes.butterfly.value() - number(row, "bf"));
    worst = std::max({worst, e_atm, e_rr, e_bf});
    EXPECT_LT(e_atm, kTolerance) << "set " << row.at("set") << " t " << row.at("t");
    EXPECT_LT(e_rr, kTolerance) << "set " << row.at("set") << " t " << row.at("t");
    EXPECT_LT(e_bf, kTolerance) << "set " << row.at("set") << " t " << row.at("t");
  }
  RecordProperty("worst_quote_error", std::to_string(worst));
  std::cout << "CAL-REF delta quotes: " << table.rows.size() << " rows, worst error " << worst
            << "\n";
}

}  // namespace
