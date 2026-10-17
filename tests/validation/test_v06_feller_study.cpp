// V-06: scheme behaviour when the Feller condition 2 kappa theta >= xi^2 fails
// (docs/test_catalogue.md). The question: how large is the bias of Euler with full truncation and
// of QE against the semi-analytic Heston price, and how does it depend on the number of steps and
// on how badly Feller fails?
//
// Fixed before the V-06 run (seed 20261016 everywhere, never changed):
//   Four parameter sets, all violating Feller (ratio 2 kappa theta / xi^2 between 0.02 and 0.1),
//   covering negative and positive rho, T from 1 to 3 years. Calls at strikes 1.00, 1.10, 1.25
//   (spot 1.10, r_d 3%, r_f 1%); 4, 8, 16, 32, 64 steps; 100,000 paths per run (fewer in Debug and
//   sanitizer builds), both schemes on the same random numbers.
//   Assertions:
//     (1) for every set, strike and step count in {4, 8, 16}: |bias(QE)| < |bias(Euler)|;
//     (2) at 32 steps, QE is within 4 standard errors of the semi-analytic price for every set and
//         strike (12 checks), so QE is unbiased to the resolution of the test, with 12 chances at
//         roughly 6e-5 each;
//     (3) power check: Euler is still more than 3 standard errors away at 32 steps for set A at the
//         money, so the test is able to reject a biased scheme;
//     (4) the QE terminal spot is a martingale at 16 steps (within 4 SE of the forward) for every
//         set; Euler's value is only reported;
//     (5) QE is insensitive to the switching level psi_c: for psi_c in {1.0, 1.5, 2.0} (set A, 16
//         steps) every price is within 5 standard errors of the exact one.
//   A Feller-ratio sweep (xi from 0.2 to 1.5 at fixed kappa, theta, rho) is recorded as properties
//   and asserted only in one direction: QE beats Euler whenever Feller fails.
// Before this file was written one exploratory run on set A showed QE roughly 100 times closer
// than Euler at every step count, so assertion (1) was expected for A; the other sets and
// assertions (2) to (5) were not looked at.

#include <gtest/gtest.h>

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

#include "fxval/mc/heston_qe.hpp"
#include "fxval/mc/vanilla.hpp"
#include "support/mc_test_support.hpp"

namespace {

using fxval::pricing::OptionType;
using fxval::testing::heston_exact;
using fxval::testing::make_config;
using fxval::testing::make_heston_market;
using fxval::testing::make_params;
using fxval::testing::must;
using fxval::testing::scaled_paths;

constexpr std::uint64_t kSeed = 20261016;
constexpr double kSpot = 1.10;
constexpr double kRd = 0.03;
constexpr double kRf = 0.01;

struct Scenario {
  const char* name;
  double v0, kappa, theta, xi, rho, years;
};

constexpr Scenario kScenarios[] = {
    {"A", 0.04, 0.5, 0.04, 1.0, -0.9, 2.0},  // ratio 0.04, strong negative skew
    {"B", 0.09, 1.0, 0.09, 1.5, -0.7, 1.0},  // ratio 0.08, high vol
    {"C", 0.02, 0.3, 0.04, 0.9, -0.3, 3.0},  // ratio 0.03, long expiry, mild skew
    {"D", 0.04, 0.8, 0.04, 0.8, 0.5, 1.0},   // ratio 0.10, positive rho
};
constexpr double kStrikes[] = {1.00, 1.10, 1.25};
constexpr std::size_t kSteps[] = {4, 8, 16, 32, 64};

std::string sci(double x) {
  std::ostringstream out;
  out << std::scientific << std::setprecision(3) << x;
  return out.str();
}

struct Bias {
  double value;  // MC minus exact
  double z;      // value / standard error
};

Bias measure(bool qe, const Scenario& s, double strike, std::size_t steps, std::size_t paths) {
  const auto params = make_params(s.v0, s.kappa, s.theta, s.xi, s.rho);
  const auto market = make_heston_market(kSpot, strike, kRd, kRf, s.years);
  const double exact = heston_exact(OptionType::call, kSpot, strike, kRd, kRf, s.years, params);
  const auto config = make_config(kSeed, paths, steps, 2);
  const auto mc =
      qe ? must(fxval::mc::heston_qe_mc_price(market, params, OptionType::call, config))
         : must(fxval::mc::heston_euler_mc_price(market, params, OptionType::call, config));
  return {mc.price - exact, (mc.price - exact) / mc.standard_error};
}

TEST(V06_FellerStudy, QeBeatsEulerAtFewStepsAndIsUnbiasedAtThirtyTwo) {
  const std::size_t paths = scaled_paths(100'000);
  double worst_qe_z_32 = 0.0;
  for (const Scenario& s : kScenarios) {
    ASSERT_FALSE(make_params(s.v0, s.kappa, s.theta, s.xi, s.rho).feller_satisfied()) << s.name;
    for (const double strike : kStrikes) {
      for (const std::size_t steps : kSteps) {
        const Bias qe = measure(true, s, strike, steps, paths);
        const Bias euler = measure(false, s, strike, steps, paths);
        const std::string key = std::string(s.name) + "_K" +
                                std::to_string(static_cast<int>(strike * 100)) + "_n" +
                                std::to_string(steps);
        RecordProperty("qe_bias_" + key, sci(qe.value));
        RecordProperty("euler_bias_" + key, sci(euler.value));
        RecordProperty("qe_z_" + key, sci(qe.z));
        RecordProperty("euler_z_" + key, sci(euler.z));
        if (steps <= 16) {
          EXPECT_LT(std::abs(qe.value), std::abs(euler.value)) << key;  // assertion (1)
        }
        if (steps == 32) {
          worst_qe_z_32 = std::max(worst_qe_z_32, std::abs(qe.z));
          EXPECT_LT(std::abs(qe.z), 4.0) << key;  // assertion (2)
        }
        if (steps == 32 && s.name[0] == 'A' && strike == 1.10) {
          EXPECT_GT(std::abs(euler.z), 3.0)
              << "power check: Euler at 32 steps must be rejectable";  // (3)
        }
      }
    }
  }
  RecordProperty("worst_qe_abs_z_at_32_steps", sci(worst_qe_z_32));
}

TEST(V06_FellerStudy, QeTerminalSpotIsAMartingaleAtSixteenSteps) {
  const std::size_t paths = scaled_paths(100'000);
  const auto identity = [](double x) noexcept {
    return x;
  };
  for (const Scenario& s : kScenarios) {
    const double forward = kSpot * std::exp((kRd - kRf) * s.years);
    const auto params = make_params(s.v0, s.kappa, s.theta, s.xi, s.rho);
    const auto spot = must(fxval::Spot::from_value(kSpot));
    const auto rd = must(fxval::Rate::from_decimal(kRd));
    const auto rf = must(fxval::Rate::from_decimal(kRf));
    const auto years = must(fxval::Years::from_years(s.years));
    const auto qe =
        must(fxval::mc::HestonQuadraticExponential::create(spot, rd, rf, years, params));
    const auto euler =
        must(fxval::mc::HestonEulerFullTruncation::create(spot, rd, rf, years, params));
    const auto config = make_config(kSeed, paths, 16, 2);
    const auto rq = must(fxval::mc::run_monte_carlo(qe, identity, config));
    const auto re = must(fxval::mc::run_monte_carlo(euler, identity, config));
    EXPECT_NEAR(rq.mean, forward, 4.0 * rq.standard_error) << s.name;  // assertion (4)
    RecordProperty(std::string("qe_forward_z_") + s.name,
                   sci((rq.mean - forward) / rq.standard_error));
    RecordProperty(std::string("euler_forward_z_") + s.name,
                   sci((re.mean - forward) / re.standard_error));
  }
}

TEST(V06_FellerStudy, QeIsInsensitiveToTheSwitchingLevel) {
  const Scenario& s = kScenarios[0];
  const auto params = make_params(s.v0, s.kappa, s.theta, s.xi, s.rho);
  const double exact = heston_exact(OptionType::call, kSpot, 1.10, kRd, kRf, s.years, params);
  const auto spot = must(fxval::Spot::from_value(kSpot));
  const auto rd = must(fxval::Rate::from_decimal(kRd));
  const auto rf = must(fxval::Rate::from_decimal(kRf));
  const auto years = must(fxval::Years::from_years(s.years));
  const auto payoff = [](double st) noexcept {
    return std::max(st - 1.10, 0.0);
  };
  const double discount = std::exp(-kRd * s.years);
  for (const double psi_c : {1.0, 1.5, 2.0}) {
    const auto model =
        must(fxval::mc::HestonQuadraticExponential::create(spot, rd, rf, years, params, psi_c));
    const auto r = must(fxval::mc::run_monte_carlo(
        model, payoff, make_config(kSeed, scaled_paths(100'000), 16, 2)));
    const double bias = discount * r.mean - exact;
    RecordProperty("psi_c_" + sci(psi_c) + "_bias_in_se",
                   sci(bias / (discount * r.standard_error)));
    EXPECT_LT(std::abs(bias), 5.0 * discount * r.standard_error) << "psi_c = " << psi_c;  // (5)
  }
}

// Feller-ratio sweep at fixed kappa = 0.5, theta = 0.04, rho = -0.7, T = 2, at the money,
// 16 steps. Recorded; the only assertion is that QE is closer than Euler whenever Feller fails.
TEST(V06_FellerStudy, SweepOverTheFellerRatio) {
  const std::size_t paths = scaled_paths(100'000);
  for (const double xi : {0.2, 0.4, 0.6, 0.8, 1.0, 1.5}) {
    const Scenario s{"sweep", 0.04, 0.5, 0.04, xi, -0.7, 2.0};
    const double ratio = 2.0 * s.kappa * s.theta / (xi * xi);
    const Bias qe = measure(true, s, 1.10, 16, paths);
    const Bias euler = measure(false, s, 1.10, 16, paths);
    const std::string key = "xi_" + sci(xi);
    RecordProperty("feller_ratio_" + key, sci(ratio));
    RecordProperty("qe_bias_" + key, sci(qe.value));
    RecordProperty("euler_bias_" + key, sci(euler.value));
    if (ratio < 1.0) {
      EXPECT_LT(std::abs(qe.value), std::abs(euler.value)) << "xi = " << xi;
    }
  }
}

}  // namespace
