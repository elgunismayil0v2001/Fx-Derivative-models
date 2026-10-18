#pragma once

// Helpers shared by the smile tests (unit and validation): building typed inputs from plain
// numbers and reading the labelled synthetic scenarios in configs/scenarios/smile_scenarios.csv.

#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fxval/smile/arbitrage_checks.hpp"
#include "fxval/smile/smile_slice.hpp"
#include "fxval/smile/vol_surface.hpp"
#include "support/gk_test_support.hpp"

namespace fxval::testing {

inline smile::SmileMarket make_smile_market(double spot,
                                            double domestic,
                                            double foreign,
                                            double years) {
  return smile::SmileMarket{must(Spot::from_value(spot)),
                            must(Rate::from_decimal(domestic)),
                            must(Rate::from_decimal(foreign)),
                            must(Years::from_years(years))};
}

inline smile::DeltaQuotes make_quotes(double atm, double rr, double bf, double delta = 0.25) {
  return smile::DeltaQuotes{must(Volatility::from_decimal(atm)),
                            must(smile::VolSpread::from_decimal(rr)),
                            must(smile::VolSpread::from_decimal(bf)),
                            delta};
}

inline smile::SmileSlice make_slice(
    double spot,
    double domestic,
    double foreign,
    double years,
    double atm,
    double rr,
    double bf,
    double delta = 0.25,
    smile::DeltaConvention convention = smile::DeltaConvention::spot) {
  return must(smile::SmileSlice::from_quotes(make_smile_market(spot, domestic, foreign, years),
                                             make_quotes(atm, rr, bf, delta),
                                             convention));
}

struct ScenarioRow {
  std::string tenor;
  double expiry, spot, rd, rf, atm, rr, bf;
};

// Scenario name -> rows in file order (expiries increasing). Lines starting with '#' are comments.
inline std::map<std::string, std::vector<ScenarioRow>> load_scenarios(const std::string& path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    throw std::runtime_error("cannot open " + path);
  }
  std::map<std::string, std::vector<ScenarioRow>> scenarios;
  std::string line;
  bool header_seen = false;
  while (std::getline(in, line)) {
    if (line.empty() || line.front() == '#') {
      continue;
    }
    if (!header_seen) {
      header_seen = true;
      continue;
    }
    std::stringstream ss(line);
    std::string name, cell;
    ScenarioRow row{};
    std::getline(ss, name, ',');
    std::getline(ss, row.tenor, ',');
    double* fields[] = {&row.expiry, &row.spot, &row.rd, &row.rf, &row.atm, &row.rr, &row.bf};
    for (double* field : fields) {
      std::getline(ss, cell, ',');
      *field = std::stod(cell);
    }
    scenarios[name].push_back(row);
  }
  return scenarios;
}

inline smile::SmileSlice slice_from_row(
    const ScenarioRow& row, smile::DeltaConvention convention = smile::DeltaConvention::spot) {
  return make_slice(
      row.spot, row.rd, row.rf, row.expiry, row.atm, row.rr, row.bf, 0.25, convention);
}

inline smile::VolSurface surface_from_rows(const std::vector<ScenarioRow>& rows) {
  std::vector<smile::SmileSlice> slices;
  for (const ScenarioRow& row : rows) {
    slices.push_back(slice_from_row(row));
  }
  return must(smile::VolSurface::create(std::move(slices)));
}

}  // namespace fxval::testing
