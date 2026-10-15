// V-05: Monte Carlo against the semi-analytic prices (docs/test_catalogue.md).
// Pass criterion: within 3 standard errors; bias against step size reported.
//
// Fixed in advance, before the results were looked at (seed 20261014 everywhere, never changed):
//
//  A. ENGINE CHECK. GBM is simulated exactly, so any disagreement with Garman-Kohlhagen is an
//     engine, random-number or discounting defect. 8 markets x call and put, each within 3 SE.
//  B. HESTON EULER, WELL-BEHAVED PARAMETERS (Feller holds). 64 time steps, 3 strikes, calls,
//     each within 3 SE of the Lewis price from week 3.
//  C. BIAS VS STEP SIZE. Parameters that violate Feller badly (2 kappa theta / xi^2 = 0.04, the
//     kind of set where Euler is known to struggle; Lord, Koekkoek and van Dijk 2010). The bias
//     (MC minus exact, in price units) is measured for 8..128 steps and written to the test log
//     as properties. Asserted: (i) at 128 steps the bias is still above 3 SE, i.e. this test
//     really can detect a biased scheme (a power check: the pass in B means something);
//     (ii) |bias| falls every time the steps double; (iii) |bias(128)| < |bias(8)| / 4
//     (a first-order scheme would give 1/16).
//  A single 3-SE check per case would fail by chance about 0.3% of the time; with the fixed seed
//  the outcome is deterministic, and a failure is investigated, never "fixed" by changing the seed.

#include <gtest/gtest.h>

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

#include "fxval/mc/vanilla.hpp"
#include "support/mc_test_support.hpp"

namespace {

using fxval::pricing::OptionType;
using fxval::testing::gk_value;
using fxval::testing::heston_exact;
using fxval::testing::make_config;
using fxval::testing::make_heston_market;
using fxval::testing::make_market;
using fxval::testing::make_params;
using fxval::testing::must;
using fxval::testing::scaled_paths;

constexpr std::uint64_t kSeed = 20261014;

std::string sci(double x) {
  std::ostringstream out;
  out << std::scientific << std::setprecision(3) << x;
  return out.str();
}

TEST(V05_McVsAnalytic, GbmEngineAgreesWithGarmanKohlhagenWithinThreeStandardErrors) {
  struct Row {
    double spot, strike, rd, rf, vol, years;
  };
  const Row rows[] = {{1.10, 1.10, 0.03, 0.01, 0.10, 1.0},
                      {1.10, 1.00, 0.03, 0.01, 0.10, 1.0},
                      {1.10, 1.25, 0.03, 0.01, 0.10, 1.0},
                      {0.90, 0.95, -0.005, 0.02, 0.20, 0.25},
                      {150.0, 140.0, 0.05, 0.01, 0.12, 2.0},
                      {1.30, 1.30, 0.00, 0.00, 0.30, 0.5},
                      {1.05, 1.00, 0.08, 0.00, 0.25, 5.0},
                      {1.00, 1.02, 0.01, 0.01, 0.05, 0.1}};
  const std::size_t paths = scaled_paths(200'000);
  double worst_z = 0.0;
  int n = 0;
  for (const Row& r : rows) {
    for (const OptionType type : {OptionType::call, OptionType::put}) {
      const auto market = make_market(r.spot, r.strike, r.rd, r.rf, r.years, r.vol);
      const auto mc = must(fxval::mc::gbm_mc_price(market, type, make_config(kSeed, paths, 1, 2)));
      const double exact = gk_value(type, r.spot, r.strike, r.rd, r.rf, r.vol, r.years);
      const double z = (mc.price - exact) / mc.standard_error;
      worst_z = std::max(worst_z, std::abs(z));
      EXPECT_LT(std::abs(z), 3.0) << "row " << n << " mc=" << mc.price << " exact=" << exact;
      ++n;
    }
  }
  RecordProperty("comparisons", n);
  RecordProperty("worst_abs_z", sci(worst_z));
}

TEST(V05_McVsAnalytic, HestonEulerAgreesWithTheSemiAnalyticPriceWhenFellerHolds) {
  const auto params = make_params(0.04, 2.0, 0.04, 0.3, -0.5);  // 2 kappa theta = 0.16 >= xi^2
  ASSERT_TRUE(params.feller_satisfied());
  const std::size_t paths = scaled_paths(200'000);
  double worst_z = 0.0;
  for (const double strike : {0.95, 1.10, 1.25}) {
    const auto market = make_heston_market(1.10, strike, 0.03, 0.01, 1.0);
    const auto mc = must(fxval::mc::heston_euler_mc_price(
        market, params, OptionType::call, make_config(kSeed, paths, 64, 2)));
    const double exact = heston_exact(OptionType::call, 1.10, strike, 0.03, 0.01, 1.0, params);
    const double z = (mc.price - exact) / mc.standard_error;
    worst_z = std::max(worst_z, std::abs(z));
    EXPECT_LT(std::abs(z), 3.0) << "strike " << strike << " mc=" << mc.price << " exact=" << exact;
  }
  RecordProperty("worst_abs_z", sci(worst_z));
}

TEST(V05_McVsAnalytic, BiasOfEulerFullTruncationFallsWithStepsAndTheTestCanDetectIt) {
  const auto params = make_params(0.04, 0.5, 0.04, 1.0, -0.9);  // Feller ratio 0.04
  ASSERT_FALSE(params.feller_satisfied());
  const auto market = make_heston_market(1.10, 1.10, 0.03, 0.01, 2.0);
  const double exact = heston_exact(OptionType::call, 1.10, 1.10, 0.03, 0.01, 2.0, params);
  const std::size_t paths = scaled_paths(100'000);

  double previous_abs_bias = 0.0;
  double abs_bias_8 = 0.0;
  double last_z = 0.0;
  double abs_bias_last = 0.0;
  for (const std::size_t steps : {8U, 16U, 32U, 64U, 128U}) {
    const auto mc = must(fxval::mc::heston_euler_mc_price(
        market, params, OptionType::call, make_config(kSeed, paths, steps, 2)));
    const double bias = mc.price - exact;
    RecordProperty("bias_steps_" + std::to_string(steps), sci(bias));
    RecordProperty("bias_in_se_steps_" + std::to_string(steps), sci(bias / mc.standard_error));
    if (steps == 8) {
      abs_bias_8 = std::abs(bias);
    } else {
      EXPECT_LT(std::abs(bias), previous_abs_bias) << "bias did not fall at " << steps << " steps";
    }
    previous_abs_bias = std::abs(bias);
    last_z = bias / mc.standard_error;
    abs_bias_last = std::abs(bias);
  }
  EXPECT_GT(std::abs(last_z), 3.0) << "power check: the 3-SE test must be able to reject";
  EXPECT_LT(abs_bias_last, abs_bias_8 / 4.0);
}

}  // namespace
