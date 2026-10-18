// SMILE-REF: strikes, pivot volatilities, smile values and the butterfly density factor against an
// independent Python implementation (tests/validation/data/smile_reference.csv, written by
// python/gen_smile_reference.py). The Python strikes come from ROOT FINDING on the spot-form
// Garman-Kohlhagen delta, not from the closed-form inverse used by the C++ code.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "support/smile_test_support.hpp"

namespace {

using fxval::smile::DeltaConvention;
using fxval::testing::make_quotes;
using fxval::testing::make_smile_market;
using fxval::testing::must;

std::string sci(double x) {
  std::ostringstream out;
  out << std::scientific << std::setprecision(2) << x;
  return out.str();
}

struct Row {
  std::string convention;
  std::map<std::string, double> v;
};

std::vector<Row> load() {
  std::ifstream in(FXVAL_SMILE_REFERENCE_PATH);
  if (!in.is_open()) {
    throw std::runtime_error(std::string("cannot open ") + FXVAL_SMILE_REFERENCE_PATH);
  }
  std::string line;
  std::getline(in, line);
  std::vector<std::string> names;
  {
    std::stringstream header(line);
    for (std::string cell; std::getline(header, cell, ',');) {
      names.push_back(cell);
    }
  }
  std::vector<Row> rows;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    std::stringstream ss(line);
    Row row;
    std::string cell;
    std::getline(ss, row.convention, ',');
    for (std::size_t i = 1; i < names.size(); ++i) {
      std::getline(ss, cell, ',');
      row.v[names[i]] = std::stod(cell);
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

TEST(SMILE_REF_IndependentPythonImplementation, StrikesVolatilitiesAndDensityFactorMatch) {
  const auto rows = load();
  ASSERT_GE(rows.size(), 250u);
  double worst_strike = 0.0, worst_pivot_vol = 0.0, worst_vol = 0.0, worst_g = 0.0;
  const double probes[] = {-2.5, -1.0, 0.0, 1.0, 2.5};

  for (const Row& r : rows) {
    const auto convention =
        r.convention == "spot" ? DeltaConvention::spot : DeltaConvention::forward;
    const auto slice = must(fxval::smile::SmileSlice::from_quotes(
        make_smile_market(r.v.at("spot"), r.v.at("rd"), r.v.at("rf"), r.v.at("t")),
        make_quotes(r.v.at("atm"), r.v.at("rr"), r.v.at("bf"), r.v.at("delta")),
        convention));
    const auto& k = slice.pivot_strikes();
    worst_strike = std::max({worst_strike,
                             std::abs(k.put / r.v.at("strike_put") - 1.0),
                             std::abs(k.atm / r.v.at("strike_atm") - 1.0),
                             std::abs(k.call / r.v.at("strike_call") - 1.0)});
    worst_pivot_vol =
        std::max({worst_pivot_vol,
                  std::abs(slice.pivot_volatilities().put.value() - r.v.at("sigma_put")),
                  std::abs(slice.pivot_volatilities().call.value() - r.v.at("sigma_call"))});
    for (std::size_t j = 0; j < 5; ++j) {
      const double log_k = probes[j] * slice.atm_scale();
      const double vol = slice.point(log_k).vol;
      worst_vol = std::max(worst_vol, std::abs(vol - r.v.at("vol_" + std::to_string(j))));
      const double g = must(fxval::smile::density_factor(slice, log_k));
      const double g_ref = r.v.at("g_" + std::to_string(j));
      worst_g = std::max(worst_g, std::abs(g - g_ref) / std::max(1.0, std::abs(g_ref)));
    }
  }
  RecordProperty("rows", static_cast<int>(rows.size()));
  RecordProperty("worst_strike_relative", sci(worst_strike));
  RecordProperty("worst_vol_absolute", sci(std::max(worst_pivot_vol, worst_vol)));
  RecordProperty("worst_density_factor", sci(worst_g));
  EXPECT_LT(worst_strike, 1e-10) << sci(worst_strike);
  EXPECT_LT(worst_pivot_vol, 1e-14) << sci(worst_pivot_vol);
  EXPECT_LT(worst_vol, 1e-11) << sci(worst_vol);
  EXPECT_LT(worst_g, 1e-8) << sci(worst_g);
}

}  // namespace
