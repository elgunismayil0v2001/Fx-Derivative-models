#pragma once

// Helpers shared by the calibration tests (unit and validation).

#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fxval/calibration/heston_calibration.hpp"
#include "fxval/calibration/heston_smile.hpp"
#include "support/heston_test_support.hpp"
#include "support/smile_test_support.hpp"

namespace fxval::testing {

inline calibration::CalibrationMarket make_calibration_market(
    double spot,
    double domestic,
    double foreign,
    smile::DeltaConvention convention = smile::DeltaConvention::spot) {
  return calibration::CalibrationMarket{must(Spot::from_value(spot)),
                                        must(Rate::from_decimal(domestic)),
                                        must(Rate::from_decimal(foreign)),
                                        convention};
}

// The quotes a Heston model implies, one slice per expiry (25-delta wings).
inline std::vector<calibration::MarketSlice> heston_slices(
    const models::HestonParams& truth,
    const calibration::CalibrationMarket& market,
    const std::vector<double>& expiries,
    double delta = 0.25) {
  std::vector<calibration::MarketSlice> slices;
  for (double t : expiries) {
    const smile::SmileMarket smile_market{
        market.spot, market.domestic, market.foreign, must(Years::from_years(t))};
    slices.push_back(calibration::MarketSlice{
        smile_market.expiry,
        must(calibration::heston_delta_quotes(smile_market, truth, delta, market.convention))});
  }
  return slices;
}

// A numeric CSV with a header line: columns by name, cells as text (the first columns of the
// calibration table are strings). Lines starting with '#' are comments.
struct CsvTable {
  std::vector<std::string> names;
  std::vector<std::map<std::string, std::string>> rows;

  [[nodiscard]] static CsvTable load(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
      throw std::runtime_error("cannot open " + path);
    }
    CsvTable table;
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty() || line.front() == '#') {
        continue;
      }
      std::stringstream ss(line);
      std::string cell;
      if (table.names.empty()) {
        while (std::getline(ss, cell, ',')) {
          table.names.push_back(cell);
        }
        continue;
      }
      std::map<std::string, std::string> row;
      for (const std::string& name : table.names) {
        std::getline(ss, cell, ',');
        row[name] = cell;
      }
      table.rows.push_back(std::move(row));
    }
    return table;
  }
};

inline double number(const std::map<std::string, std::string>& row, const std::string& name) {
  return std::stod(row.at(name));
}

}  // namespace fxval::testing
