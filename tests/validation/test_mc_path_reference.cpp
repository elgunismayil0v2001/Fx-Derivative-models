// MC-PATH-REF: C++ random numbers and Heston paths against an independent Python implementation.
// The table is tests/validation/data/mc_path_reference.csv, written by
// python/gen_mc_reference.py, which re-implements Philox4x32-10, Box-Muller and the Euler
// full-truncation path in plain Python (no shared code).
//
// This catches what a statistical test cannot: a path that is slightly wrong but still averages
// to the right price at low path counts (a mis-ordered operation, a wrong stream number, the
// wrong half of a 64-bit seed).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fxval/mc/heston_euler.hpp"
#include "fxval/mc/normal_stream.hpp"
#include "support/heston_test_support.hpp"

#ifndef FXVAL_MC_PATH_REFERENCE_PATH
#error "FXVAL_MC_PATH_REFERENCE_PATH must be defined by the build system"
#endif

namespace {

using fxval::mc::HestonEulerFullTruncation;
using fxval::mc::NormalStream;
using fxval::testing::make_params;
using fxval::testing::must;

std::string sci(double x) {
  std::ostringstream out;
  out << std::scientific << std::setprecision(2) << x;
  return out.str();
}

TEST(MC_PATH_REF_IndependentPythonImplementation, NormalsAndHestonPathsMatch) {
  std::ifstream in(FXVAL_MC_PATH_REFERENCE_PATH);
  if (!in.is_open()) {
    throw std::runtime_error(std::string("cannot open ") + FXVAL_MC_PATH_REFERENCE_PATH);
  }
  std::string line;
  std::getline(in, line);  // header
  int rows = 0;
  double worst_normal = 0.0;
  double worst_spot = 0.0;
  double worst_variance = 0.0;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    std::stringstream ss(line);
    std::string cell;
    std::getline(ss, cell, ',');
    const std::uint64_t seed = std::strtoull(cell.c_str(), nullptr, 10);
    std::getline(ss, cell, ',');
    const std::uint64_t stream = std::strtoull(cell.c_str(), nullptr, 10);
    std::getline(ss, cell, ',');
    const std::size_t steps = std::strtoull(cell.c_str(), nullptr, 10);
    double v[13];  // spot rd rf t v0 kappa theta xi rho z1 z2 final_spot final_variance
    for (double& x : v) {
      std::getline(ss, cell, ',');
      x = std::stod(cell);
    }

    NormalStream first(seed, stream);
    double z1 = 0.0;
    double z2 = 0.0;
    first.next_pair(z1, z2);
    EXPECT_NEAR(z1, v[9], 1e-13 * std::max(1.0, std::abs(v[9]))) << "row " << rows;
    EXPECT_NEAR(z2, v[10], 1e-13 * std::max(1.0, std::abs(v[10]))) << "row " << rows;
    worst_normal = std::max({worst_normal, std::abs(z1 - v[9]), std::abs(z2 - v[10])});

    const auto model =
        must(HestonEulerFullTruncation::create(must(fxval::Spot::from_value(v[0])),
                                               must(fxval::Rate::from_decimal(v[1])),
                                               must(fxval::Rate::from_decimal(v[2])),
                                               must(fxval::Years::from_years(v[3])),
                                               make_params(v[4], v[5], v[6], v[7], v[8])));
    NormalStream rng(seed, stream);
    const auto state = model.simulate_terminal_state(rng, steps);
    // libm differences between machines are a few units in the last place per step; the
    // truncation near v = 0 can amplify them slightly, hence 1e-9 and not 1e-14.
    EXPECT_NEAR(state.spot, v[11], 1e-9 * std::max(1.0, std::abs(v[11]))) << "row " << rows;
    EXPECT_NEAR(state.variance, v[12], 1e-9 * std::max(1.0, std::abs(v[12]))) << "row " << rows;
    worst_spot = std::max(worst_spot, std::abs(state.spot - v[11]) / std::max(1.0, v[11]));
    worst_variance = std::max(worst_variance, std::abs(state.variance - v[12]));
    ++rows;
  }
  ASSERT_GE(rows, 100);
  RecordProperty("rows", rows);
  RecordProperty("worst_normal_abs_error", sci(worst_normal));
  RecordProperty("worst_spot_rel_error", sci(worst_spot));
  RecordProperty("worst_variance_abs_error", sci(worst_variance));
}

}  // namespace
