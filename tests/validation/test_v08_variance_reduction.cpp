// V-08: variance reduction (docs/test_catalogue.md). Antithetic variates and a control variate are
// only worth having if (a) they leave the estimate unbiased, (b) their reported standard error is
// honest, and (c) they really reduce the variance at equal cost. This file checks all three.
//
// Fixed before the V-08 run (seed 20261016, never changed). Model: Heston with QE, spot 1.10,
// r_d 3%, r_f 1%, T = 1, v0 = theta = 0.04, kappa = 2, xi = 0.3, rho = -0.5 (Feller holds), 16
// steps, calls at strikes 0.95, 1.10, 1.25, 200,000 plain paths. Estimators:
//   plain       200,000 paths
//   antithetic  100,000 pairs               (the same 200,000 path simulations)
//   control     200,000 paths, control g(S_T) = S_T with exact mean = forward, coefficient from a
//               pilot of 20,000 paths on other random streams
//   both        100,000 pairs and the control
// Assertions:
//   (a) every estimate is within 3 standard errors of the semi-analytic price (4 estimators x
//       3 strikes); and a control with a deliberately wrong mean (forward + 1%) is rejected at more
//       than 5 standard errors, so the check is able to fail;
//   (b) at the money, 8 steps, 80 independent seeds: the mean reported standard error of each of
//       the four estimators is within 30% of the empirical standard deviation of its 80 estimates
//       (the relative error of a standard deviation from 80 values is about 8%);
//   (c) cost-adjusted variance ratio above 1 for antithetic, control and both, at every strike:
//       plain variance / reduced variance for the control (same cost), and
//       plain variance / (2 x pair variance) for the antithetic ones (one pair costs two paths).
//   (d) machinery check under exact GBM (sigma 20%, strike 1.10) with a Garman-Kohlhagen price as
//       the control: the call with strike 1.05, whose mean is known exactly. Within 3 SE of the
//       GK price and a variance ratio above 5.
// The ratios themselves are recorded as properties and reported in docs/model_documentation.md;
// they are measurements, not targets.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "fxval/mc/gbm.hpp"
#include "fxval/mc/heston_qe.hpp"
#include "fxval/mc/variance_reduction.hpp"
#include "support/mc_test_support.hpp"

namespace {

using fxval::mc::McConfig;
using fxval::mc::McEstimate;
using fxval::mc::NoControl;
using fxval::pricing::OptionType;
using fxval::testing::gk_value;
using fxval::testing::heston_exact;
using fxval::testing::make_config;
using fxval::testing::make_params;
using fxval::testing::must;
using fxval::testing::scaled_paths;

constexpr std::uint64_t kSeed = 20261016;
constexpr double kSpot = 1.10;
constexpr double kRd = 0.03;
constexpr double kRf = 0.01;
constexpr double kYears = 1.0;

std::string sci(double x) {
  std::ostringstream out;
  out << std::scientific << std::setprecision(3) << x;
  return out.str();
}

constexpr auto forward_control = [](double s) noexcept {
  return s;
};

fxval::mc::HestonQuadraticExponential make_model() {
  return must(
      fxval::mc::HestonQuadraticExponential::create(must(fxval::Spot::from_value(kSpot)),
                                                    must(fxval::Rate::from_decimal(kRd)),
                                                    must(fxval::Rate::from_decimal(kRf)),
                                                    must(fxval::Years::from_years(kYears)),
                                                    make_params(0.04, 2.0, 0.04, 0.3, -0.5)));
}

struct Four {
  McEstimate plain;
  McEstimate anti;
  McEstimate control;
  McEstimate both;
  double coefficient;
};

Four run_four(double strike,
              std::size_t paths,
              std::size_t steps,
              std::uint64_t seed,
              double mean_shift = 0.0) {
  const auto model = make_model();
  const auto call = [strike](double s) noexcept {
    return std::max(s - strike, 0.0);
  };
  const double forward = kSpot * std::exp((kRd - kRf) * kYears) + mean_shift;
  McConfig full = make_config(seed, paths, steps, 2);
  McConfig half = make_config(seed, paths / 2, steps, 2);
  Four out{};
  out.plain = must(fxval::mc::run_monte_carlo(model, call, full));
  out.anti = must(fxval::mc::run_antithetic(model, call, half));
  out.coefficient = must(fxval::mc::estimate_control_coefficient(
      model, call, forward_control, false, seed, steps, 20'000));
  out.control = must(fxval::mc::run_variance_reduced(
      model, call, forward_control, forward, out.coefficient, false, full));
  const double c_pair = must(fxval::mc::estimate_control_coefficient(
      model, call, forward_control, true, seed, steps, 20'000));
  out.both = must(
      fxval::mc::run_variance_reduced(model, call, forward_control, forward, c_pair, true, half));
  return out;
}

TEST(V08_VarianceReduction, EstimatorsAreUnbiasedAndTheWrongControlMeanIsDetected) {
  const std::size_t paths = scaled_paths(200'000);
  const auto params = make_params(0.04, 2.0, 0.04, 0.3, -0.5);
  for (const double strike : {0.95, 1.10, 1.25}) {
    const double exact = heston_exact(OptionType::call, kSpot, strike, kRd, kRf, kYears, params) *
                         std::exp(kRd * kYears);
    const Four r = run_four(strike, paths, 16, kSeed);
    const struct {
      const char* name;
      const McEstimate& e;
    } rows[] = {
        {"plain", r.plain}, {"antithetic", r.anti}, {"control", r.control}, {"both", r.both}};
    for (const auto& row : rows) {
      const double z = (row.e.mean - exact) / row.e.standard_error;
      RecordProperty(std::string("z_") + row.name + "_K" +
                         std::to_string(static_cast<int>(strike * 100)),
                     sci(z));
      EXPECT_LT(std::abs(z), 3.0) << row.name << " strike " << strike;  // (a)
    }
    // (c) cost-adjusted variance ratios.
    const double ratio_anti = r.plain.variance / (2.0 * r.anti.variance);
    const double ratio_control = r.plain.variance / r.control.variance;
    const double ratio_both = r.plain.variance / (2.0 * r.both.variance);
    const std::string k = "_K" + std::to_string(static_cast<int>(strike * 100));
    RecordProperty("variance_ratio_antithetic" + k, sci(ratio_anti));
    RecordProperty("variance_ratio_control" + k, sci(ratio_control));
    RecordProperty("variance_ratio_both" + k, sci(ratio_both));
    RecordProperty("control_coefficient" + k, sci(r.coefficient));
    EXPECT_GT(ratio_anti, 1.0) << strike;
    EXPECT_GT(ratio_control, 1.0) << strike;
    EXPECT_GT(ratio_both, 1.0) << strike;
  }
  // A wrong control mean is not visible in the standard error but must show up against the exact
  // price: this is the power check for (a).
  const double exact_atm = heston_exact(OptionType::call, kSpot, 1.10, kRd, kRf, kYears, params) *
                           std::exp(kRd * kYears);
  const double forward = kSpot * std::exp((kRd - kRf) * kYears);
  const Four wrong = run_four(1.10, paths, 16, kSeed, 0.01 * forward);
  const double z_wrong = (wrong.control.mean - exact_atm) / wrong.control.standard_error;
  RecordProperty("z_control_with_wrong_mean", sci(z_wrong));
  EXPECT_GT(std::abs(z_wrong), 5.0);
}

TEST(V08_VarianceReduction, ReportedStandardErrorMatchesTheSpreadAcrossSeeds) {
  const std::size_t paths = scaled_paths(50'000);
  constexpr int kSeeds = 80;
  std::vector<double> mean[4];
  std::vector<double> se[4];
  for (int i = 0; i < kSeeds; ++i) {
    const Four r = run_four(1.10, paths, 8, kSeed + 1000 + static_cast<std::uint64_t>(i));
    const McEstimate* all[4] = {&r.plain, &r.anti, &r.control, &r.both};
    for (int k = 0; k < 4; ++k) {
      mean[k].push_back(all[k]->mean);
      se[k].push_back(all[k]->standard_error);
    }
  }
  const char* names[4] = {"plain", "antithetic", "control", "both"};
  for (int k = 0; k < 4; ++k) {
    double m = 0.0;
    double avg_se = 0.0;
    for (int i = 0; i < kSeeds; ++i) {
      m += mean[k][static_cast<std::size_t>(i)];
      avg_se += se[k][static_cast<std::size_t>(i)];
    }
    m /= kSeeds;
    avg_se /= kSeeds;
    double ss = 0.0;
    for (int i = 0; i < kSeeds; ++i) {
      const double d = mean[k][static_cast<std::size_t>(i)] - m;
      ss += d * d;
    }
    const double sd = std::sqrt(ss / (kSeeds - 1));
    RecordProperty(std::string("se_over_empirical_sd_") + names[k], sci(avg_se / sd));
    EXPECT_NEAR(avg_se / sd, 1.0, 0.30) << names[k];  // (b)
  }
}

TEST(V08_VarianceReduction, GarmanKohlhagenPriceWorksAsAControlUnderExactGbm) {
  const double sigma = 0.20;
  const auto model = must(fxval::mc::GbmModel::create(must(fxval::Spot::from_value(kSpot)),
                                                      must(fxval::Rate::from_decimal(kRd)),
                                                      must(fxval::Rate::from_decimal(kRf)),
                                                      must(fxval::Volatility::from_decimal(sigma)),
                                                      must(fxval::Years::from_years(kYears))));
  const auto target = [](double s) noexcept {
    return std::max(s - 1.10, 0.0);
  };
  const auto control = [](double s) noexcept {
    return std::max(s - 1.05, 0.0);
  };
  const double control_mean =
      gk_value(OptionType::call, kSpot, 1.05, kRd, kRf, sigma, kYears) * std::exp(kRd * kYears);
  const double exact =
      gk_value(OptionType::call, kSpot, 1.10, kRd, kRf, sigma, kYears) * std::exp(kRd * kYears);
  const std::size_t paths = scaled_paths(200'000);
  const McConfig config = make_config(kSeed, paths, 1, 2);
  const auto plain = must(fxval::mc::run_monte_carlo(model, target, config));
  const double c = must(
      fxval::mc::estimate_control_coefficient(model, target, control, false, kSeed, 1, 20'000));
  const auto cv =
      must(fxval::mc::run_variance_reduced(model, target, control, control_mean, c, false, config));
  const double z = (cv.mean - exact) / cv.standard_error;
  const double ratio = plain.variance / cv.variance;
  RecordProperty("gbm_control_z", sci(z));
  RecordProperty("gbm_control_variance_ratio", sci(ratio));
  RecordProperty("gbm_control_coefficient", sci(c));
  EXPECT_LT(std::abs(z), 3.0);  // (d)
  EXPECT_GT(ratio, 5.0);
}

}  // namespace
