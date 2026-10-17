// QE-PATH-REF: C++ Heston QE paths against an independent Python implementation of the scheme.
// The table is tests/validation/data/mc_qe_path_reference.csv, written by
// python/gen_qe_reference.py (same paper, separately written code; random numbers from the
// Philox implementation that MC-PATH-REF already ties to the C++ generator).
//
// Like MC-PATH-REF this catches what a statistical test cannot: a wrong sign in one of the K
// constants or in the martingale correction can hide inside sampling noise at moderate path counts.
// The table covers both variance branches, the fallback when the correction moment does not
// exist, antithetic paths, psi_c = 1 and 2, 64-bit seeds and streams, and one-step paths.

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

#include "fxval/mc/heston_qe.hpp"
#include "support/heston_test_support.hpp"

#ifndef FXVAL_MC_QE_PATH_REFERENCE_PATH
#error "FXVAL_MC_QE_PATH_REFERENCE_PATH must be defined by the build system"
#endif

namespace {

using fxval::mc::HestonQuadraticExponential;
using fxval::mc::NormalStream;
using fxval::testing::make_params;
using fxval::testing::must;

std::string sci(double x) {
  std::ostringstream out;
  out << std::scientific << std::setprecision(2) << x;
  return out.str();
}

TEST(QE_PATH_REF_IndependentPythonImplementation, TerminalStatesMatch) {
  std::ifstream in(FXVAL_MC_QE_PATH_REFERENCE_PATH);
  if (!in.is_open()) {
    throw std::runtime_error(std::string("cannot open ") + FXVAL_MC_QE_PATH_REFERENCE_PATH);
  }
  std::string line;
  std::getline(in, line);  // header
  int rows = 0;
  int antithetic_rows = 0;
  int fallback_rows = 0;
  double worst_spot = 0.0;
  double worst_variance = 0.0;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    std::stringstream ss(line);
    std::string cell;
    const auto next = [&]() -> const std::string& {
      std::getline(ss, cell, ',');
      return cell;
    };
    const std::uint64_t seed = std::strtoull(next().c_str(), nullptr, 10);
    const std::uint64_t stream = std::strtoull(next().c_str(), nullptr, 10);
    const bool antithetic = std::strtoull(next().c_str(), nullptr, 10) != 0;
    const std::size_t steps = std::strtoull(next().c_str(), nullptr, 10);
    double v[11];  // spot rd rf t v0 kappa theta xi rho psi_c final_spot (final_variance below)
    for (double& x : v) {
      x = std::stod(next());
    }
    const double final_variance = std::stod(next());
    next();  // quadratic_steps
    next();  // exponential_steps
    const long fallback_steps = std::strtol(next().c_str(), nullptr, 10);

    const auto model =
        must(HestonQuadraticExponential::create(must(fxval::Spot::from_value(v[0])),
                                                must(fxval::Rate::from_decimal(v[1])),
                                                must(fxval::Rate::from_decimal(v[2])),
                                                must(fxval::Years::from_years(v[3])),
                                                make_params(v[4], v[5], v[6], v[7], v[8]),
                                                v[9]));
    NormalStream rng(seed, stream, antithetic);
    const auto state = model.simulate_terminal_state(rng, steps);
    // Same tolerance as MC-PATH-REF: libm differences of a few ulp per step, amplified a little
    // by the truncation of the exponential branch near zero.
    EXPECT_NEAR(state.spot, v[10], 1e-9 * std::max(1.0, std::abs(v[10]))) << "row " << rows;
    EXPECT_NEAR(state.variance, final_variance, 1e-9 * std::max(1.0, std::abs(final_variance)))
        << "row " << rows;
    worst_spot = std::max(worst_spot, std::abs(state.spot - v[10]) / std::max(1.0, v[10]));
    worst_variance = std::max(worst_variance, std::abs(state.variance - final_variance));
    antithetic_rows += antithetic ? 1 : 0;
    fallback_rows += fallback_steps > 0 ? 1 : 0;
    ++rows;
  }
  ASSERT_GE(rows, 150);
  ASSERT_GE(antithetic_rows, 10);
  ASSERT_GE(fallback_rows, 4);  // the corner where the martingale correction is not available
  RecordProperty("rows", rows);
  RecordProperty("antithetic_rows", antithetic_rows);
  RecordProperty("fallback_rows", fallback_rows);
  RecordProperty("worst_spot_rel_error", sci(worst_spot));
  RecordProperty("worst_variance_abs_error", sci(worst_variance));
}

}  // namespace
