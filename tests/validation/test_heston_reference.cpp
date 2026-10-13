// HESTON-REF: C++ Heston prices against an independent Python implementation.
// The table is tests/validation/data/heston_reference.csv, written by
// python/gen_heston_reference.py, which prices with the textbook P1/P2 probabilities of Heston
// (1993) and scipy's QUADPACK, not the single Lewis integral and own quadrature used in C++.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fxval/models/heston.hpp"
#include "support/heston_test_support.hpp"

#ifndef FXVAL_HESTON_REFERENCE_PATH
#error "FXVAL_HESTON_REFERENCE_PATH must be defined by the build system"
#endif

namespace {

using fxval::pricing::OptionType;
using fxval::testing::heston_value;
using fxval::testing::make_params;

std::string sci(double x) {
  std::ostringstream out;
  out << std::scientific << std::setprecision(2) << x;
  return out.str();
}

TEST(HESTON_REF_IndependentPythonImplementation, CallAndPutMatch) {
  std::ifstream in(FXVAL_HESTON_REFERENCE_PATH);
  if (!in.is_open()) {
    throw std::runtime_error(std::string("cannot open ") + FXVAL_HESTON_REFERENCE_PATH);
  }
  std::string line;
  std::getline(in, line);  // header
  int rows = 0;
  double worst = 0.0;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    std::stringstream ss(line);
    std::string cell;
    double v[12];  // spot strike rd rf t v0 kappa theta xi rho call put
    for (double& x : v) {
      std::getline(ss, cell, ',');
      x = std::stod(cell);
    }
    const auto params = make_params(v[5], v[6], v[7], v[8], v[9]);
    // Absolute tolerance, scaled by the spot so that it means the same for any quote level. The
    // price of a deep out-of-the-money option can be 1e-8 of the spot, so a relative tolerance
    // would only compare rounding noise.
    const double tol = 2e-11 * std::max(1.0, v[0]);
    const double call = heston_value(OptionType::call, v[0], v[1], v[2], v[3], v[4], params);
    const double put = heston_value(OptionType::put, v[0], v[1], v[2], v[3], v[4], params);
    worst = std::max({worst, std::abs(call - v[10]), std::abs(put - v[11])});
    EXPECT_NEAR(call, v[10], tol) << "row " << rows << ": " << line;
    EXPECT_NEAR(put, v[11], tol) << "row " << rows << ": " << line;
    ++rows;
  }
  ASSERT_GE(rows, 500);
  RecordProperty("rows", rows);
  RecordProperty("worst_abs_error", sci(worst));
}

}  // namespace
