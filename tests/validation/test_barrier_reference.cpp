// BARRIER-REF: the barrier closed forms against an independent Python implementation
// (tests/validation/data/barrier_reference.csv, written by python/gen_barrier_reference.py).
//
// The Python prices are numerical integrals (scipy QUADPACK) of the killed transition density of
// Brownian motion with drift (method of images), of the first-passage density, and of a sum of
// images for the double no-touch. None of the Reiner-Rubinstein blocks, the first-passage
// probability formula or the sine series of the C++ code appears there.
//
// Criterion, fixed before the first run: absolute difference below 1e-9 in domestic currency per
// unit notional (the quadrature is accurate to about 1e-12).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>

#include "support/barrier_test_support.hpp"

#ifndef FXVAL_BARRIER_REFERENCE_PATH
#error "FXVAL_BARRIER_REFERENCE_PATH must be defined by the build system"
#endif

namespace {

using fxval::pricing::BarrierDirection;
using fxval::pricing::BarrierEffect;
using fxval::pricing::OptionType;
using fxval::testing::CsvTable;
using fxval::testing::level;
using fxval::testing::make_barrier_vanilla;
using fxval::testing::make_fx_market;
using fxval::testing::must;
using fxval::testing::number;
using fxval::testing::vol;

constexpr double kTolerance = 1e-9;

TEST(BARRIER_REF_IndependentPythonImplementation, PricesMatch) {
  const CsvTable table = CsvTable::load(FXVAL_BARRIER_REFERENCE_PATH);
  ASSERT_GE(table.rows.size(), 600U);
  std::map<std::string, double> worst;
  std::map<std::string, int> count;
  for (const auto& row : table.rows) {
    const auto market =
        make_fx_market(number(row, "spot"), number(row, "rd"), number(row, "rf"), number(row, "t"));
    const double sigma = number(row, "vol");
    const std::string& kind = row.at("kind");
    const BarrierDirection direction =
        row.at("direction") == "up" ? BarrierDirection::up : BarrierDirection::down;
    double ours = 0.0;
    std::string label = kind;
    if (kind == "vanilla") {
      const OptionType type = row.at("type") == "call" ? OptionType::call : OptionType::put;
      const BarrierEffect effect =
          row.at("effect") == "knock_in" ? BarrierEffect::knock_in : BarrierEffect::knock_out;
      ours = must(fxval::pricing::barrier_vanilla_price(
          market,
          vol(sigma),
          make_barrier_vanilla(
              type, number(row, "strike"), number(row, "barrier"), direction, effect)));
      label = row.at("type") + "/" + row.at("direction") + "/" + row.at("effect");
    } else if (kind == "touch") {
      ours = must(fxval::pricing::one_touch_price(
          market, vol(sigma), level(number(row, "barrier")), direction));
    } else {
      ours = must(fxval::pricing::double_no_touch_price(
          market, vol(sigma), level(number(row, "barrier")), level(number(row, "barrier2"))));
    }
    const double error = std::abs(ours - number(row, "price"));
    worst[label] = std::max(worst[label], error);
    ++count[label];
    EXPECT_LT(error, kTolerance) << label << " spot " << number(row, "spot") << " strike "
                                 << number(row, "strike") << " barrier " << number(row, "barrier");
  }
  double overall = 0.0;
  for (const auto& [label, w] : worst) {
    overall = std::max(overall, w);
    std::cout << "BARRIER-REF " << label << ": " << count[label] << " rows, worst " << w << "\n";
  }
  RecordProperty("worst_error", std::to_string(overall));
}

}  // namespace
