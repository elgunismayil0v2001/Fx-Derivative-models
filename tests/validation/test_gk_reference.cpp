// GK-REF: C++ prices and Greeks against an independent Python (SciPy) implementation.
// The table is tests/validation/data/gk_reference.csv, written by python/gen_gk_reference.py.

#include <gtest/gtest.h>

#include "fxval/core/limits.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"
#include "support/gk_test_support.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using fxval::pricing::GarmanKohlhagenGreeks;
using fxval::pricing::OptionType;
using fxval::testing::gk_all;
using fxval::testing::gk_value;

// Scientific notation for the recorded worst errors (std::to_string would print 0.000000).
std::string sci(double x) {
  std::ostringstream out;
  out << std::scientific << std::setprecision(2) << x;
  return out.str();
}

// ---------------------------------------------------------------------------------------------
// REF: comparison with the independent Python implementation (scipy).
// The two implementations differ in language, normal-distribution routine and algebraic form, so
// agreement to ~1e-12 relative shows they are computing the same mathematical object.
// ---------------------------------------------------------------------------------------------
struct RefRow {
  std::string type;
  double v[16];  // spot strike rd rf sigma t price delta_spot delta_forward gamma vega vanna volga
                 // theta rho_d rho_f
};

std::vector<RefRow> load_reference() {
  std::ifstream in(FXVAL_GK_REFERENCE_PATH);
  if (!in.is_open()) {
    throw std::runtime_error(std::string("cannot open ") + FXVAL_GK_REFERENCE_PATH);
  }
  std::vector<RefRow> rows;
  std::string line;
  std::getline(in, line);  // header
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    std::stringstream ss(line);
    std::string cell;
    RefRow row{};
    std::getline(ss, row.type, ',');
    for (double& x : row.v) {
      std::getline(ss, cell, ',');
      x = std::stod(cell);
    }
    rows.push_back(row);
  }
  return rows;
}

TEST(REF_IndependentPythonImplementation, PricesAndGreeksMatch) {
  const auto rows = load_reference();
  ASSERT_GE(rows.size(), 600u);
  constexpr double tol = 1e-11;  // relative, with a floor of 1e-12 of the row's price scale
  double worst = 0.0;
  for (const auto& r : rows) {
    const auto type = (r.type == "call") ? OptionType::call : OptionType::put;
    const double s = r.v[0], k = r.v[1], rd = r.v[2], rf = r.v[3], sigma = r.v[4], t = r.v[5];
    const GarmanKohlhagenGreeks g = gk_all(type, s, k, rd, rf, sigma, t);
    const double mine[] = {gk_value(type, s, k, rd, rf, sigma, t),
                           g.spot_delta,
                           g.forward_delta,
                           g.gamma,
                           g.vega,
                           g.vanna,
                           g.volga,
                           g.theta,
                           g.rho_domestic,
                           g.rho_foreign};
    const char* names[] = {"price",
                           "delta_spot",
                           "delta_forward",
                           "gamma",
                           "vega",
                           "vanna",
                           "volga",
                           "theta",
                           "rho_d",
                           "rho_f"};
    // Floor for each column: 1e-12 of the largest magnitude in the row's family, so tiny
    // out-of-the-money values are compared in absolute terms, not as ratios of rounding noise.
    const double floors[] = {1e-12 * s,
                             1e-12,
                             1e-12,
                             1e-12 * 1e2,
                             1e-12 * s,
                             1e-12 * 1e2,
                             1e-12 * s * 1e2,
                             1e-12 * s,
                             1e-12 * k * t,
                             1e-12 * s * t};
    for (int i = 0; i < 10; ++i) {
      const double ref = r.v[6 + i];
      const double err = std::abs(mine[i] - ref) / (std::abs(ref) + floors[i]);
      worst = std::max(worst, err);
      EXPECT_LT(err, tol) << names[i] << " " << r.type << " s=" << s << " k=" << k << " rd=" << rd
                          << " rf=" << rf << " sigma=" << sigma << " t=" << t << " mine=" << mine[i]
                          << " ref=" << ref;
    }
  }
  RecordProperty("rows", static_cast<int>(rows.size()));
  RecordProperty("worst_relative_error", sci(worst));
}

}  // namespace
