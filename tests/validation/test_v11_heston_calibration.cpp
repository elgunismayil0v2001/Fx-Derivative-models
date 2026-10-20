// V-11: Heston calibration parameter recovery.
//
// Data: tests/validation/data/calibration_reference.csv, 24 random "true" Heston parameter sets
// with the market quotes (ATM, 25-delta RR and BF) at 1M, 3M, 6M, 1Y and 2Y that each set
// implies, computed by independent Python code (python/gen_calibration_reference.py). 16 of the
// 24 sets satisfy the Feller condition and 8 violate it; 12 use spot delta and 12 forward delta.
// The seed and the sampling rule were fixed before any calibration was run on the table.
//
// Criteria, fixed before the table was calibrated:
//   (A) EXACT QUOTES, FIVE EXPIRIES. On every one of the 24 sets (6 in unoptimised or sanitizer
//       builds, every 4th) the calibration (data-driven start plus 2 random starts) ends with a
//       maximum volatility error below 1e-6 and recovers every parameter: relative error below
//       1e-4 for v0, kappa, theta and xi, absolute error below 1e-4 for rho, and reports
//       `converged`.
//   (B) IDENTIFICATION CONTROL. The same calibration with only the 1Y expiry has 3 quotes for 5
//       unknowns. It must fit the quotes (max volatility error below 1e-6) and must NOT be
//       trusted: in at least 80% of the sets the parameters are off by more than 1% (relative; 0.01
//       absolute for rho), and in 100% of the sets every reported standard error is infinite. A
//       test that could not fail would prove nothing, so this is the control that must fail.
//   (C) NOISE STUDY. On sets 0, 8 and 16, 40 noisy copies of the quotes (independent normal noise,
//       standard deviation s = 5e-4 = 5 basis points of volatility, added to each of the 15 pivot
//       volatilities, seeded Philox streams) are calibrated from the data-driven start. With N = 15
//       quotes and p = 5 parameters, linear theory gives E[rms^2] = s^2 (N - p) / N and a parameter
//       spread equal to the reported standard error. Required: the mean rms^2 over the copies is
//       within 25% of that value; for each parameter the ratio (sample standard deviation over the
//       copies) / (mean reported standard error) is in [0.6, 1.6]; and the mean estimate is within
//       4 standard errors of the truth (standard error of the mean = sd / sqrt(40)). All copies
//       must converge.
//
// Disclosures:
//  * Before the table was generated, one hand-picked set (v0 = 0.0225, kappa = 1.8, theta = 0.0324,
//    xi = 0.55, rho = -0.35; expiries 1M to 2Y) was calibrated in an exploratory program, to check
//    that the code worked at all. It was recovered to about 1e-12. That set is not in the table.
//  * The first draw of the table used unrestricted sampling and only 1 of 24 sets satisfied Feller.
//    The sampling was changed to force Feller on half of the sets BEFORE the C++ calibration was
//    run on any table; this is recorded in the generator docstring.
//  * This file is the week-8 rebuild: the first copy of the code and tests was lost with a
//    container reset before its results were recorded, so every number reported for V-11 comes from
//    the run of this version.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <vector>

#include "fxval/mc/normal_stream.hpp"
#include "support/calibration_test_support.hpp"
#include "support/mc_test_support.hpp"

#ifndef FXVAL_CALIBRATION_REFERENCE_PATH
#error "FXVAL_CALIBRATION_REFERENCE_PATH must be defined by the build system"
#endif

namespace {

using fxval::calibration::calibrate_heston;
using fxval::calibration::CalibrationMarket;
using fxval::calibration::CalibrationSettings;
using fxval::calibration::MarketSlice;
using fxval::smile::DeltaConvention;
using fxval::testing::CsvTable;
using fxval::testing::make_calibration_market;
using fxval::testing::must;
using fxval::testing::number;
using fxval::testing::reduced_effort;

constexpr std::size_t kParams = 5;
const char* const kNames[kParams] = {"v0", "kappa", "theta", "xi", "rho"};

struct TrueSet {
  int id;
  CalibrationMarket market;
  std::array<double, kParams> truth;
  std::vector<double> expiries;
  std::vector<std::array<double, 3>> quotes;  // atm, rr, bf per expiry

  [[nodiscard]] bool feller() const { return 2.0 * truth[1] * truth[2] >= truth[3] * truth[3]; }
};

std::vector<TrueSet> load_sets() {
  const CsvTable table = CsvTable::load(FXVAL_CALIBRATION_REFERENCE_PATH);
  std::map<int, TrueSet> by_id;
  for (const auto& row : table.rows) {
    const int id = std::stoi(row.at("set"));
    auto it = by_id.find(id);
    if (it == by_id.end()) {
      const DeltaConvention convention =
          row.at("convention") == "spot" ? DeltaConvention::spot : DeltaConvention::forward;
      TrueSet set{id,
                  make_calibration_market(
                      number(row, "spot"), number(row, "rd"), number(row, "rf"), convention),
                  {number(row, "v0"),
                   number(row, "kappa"),
                   number(row, "theta"),
                   number(row, "xi"),
                   number(row, "rho")},
                  {},
                  {}};
      it = by_id.emplace(id, std::move(set)).first;
    }
    it->second.expiries.push_back(number(row, "t"));
    it->second.quotes.push_back({number(row, "atm"), number(row, "rr"), number(row, "bf")});
  }
  std::vector<TrueSet> sets;
  for (auto& [id, set] : by_id) {
    sets.push_back(std::move(set));
  }
  return sets;
}

MarketSlice slice_of(double expiry, const std::array<double, 3>& q) {
  return MarketSlice{must(fxval::Years::from_years(expiry)),
                     fxval::testing::make_quotes(q[0], q[1], q[2])};
}

std::vector<MarketSlice> slices_of(const TrueSet& set, const std::vector<std::size_t>& which) {
  std::vector<MarketSlice> slices;
  for (std::size_t i : which) {
    slices.push_back(slice_of(set.expiries[i], set.quotes[i]));
  }
  return slices;
}

std::array<double, kParams> estimate_of(const fxval::calibration::CalibrationResult& r) {
  return {r.params.v0(), r.params.kappa(), r.params.theta(), r.params.xi(), r.params.rho()};
}

// Recovery error: relative for the positive parameters, absolute for rho.
double recovery_error(std::size_t j, double estimate, double truth) {
  return j == 4 ? std::abs(estimate - truth) : std::abs(estimate - truth) / std::abs(truth);
}

std::string sci(double x) {
  std::ostringstream out;
  out << std::scientific << std::setprecision(1) << x;
  return out.str();
}

TEST(V11_HestonCalibration, A_RecoversKnownParametersFromExactQuotes) {
  const std::vector<TrueSet> sets = load_sets();
  ASSERT_EQ(sets.size(), 24U);
  CalibrationSettings settings;
  settings.random_starts = 2;
  settings.compute_standard_errors = false;
  const std::size_t stride = reduced_effort ? 4 : 1;
  double worst_vol = 0.0;
  double worst_param = 0.0;
  std::size_t run = 0;
  std::size_t feller_run = 0;
  for (std::size_t s = 0; s < sets.size(); s += stride) {
    const TrueSet& set = sets[s];
    const auto result = calibrate_heston(set.market, slices_of(set, {0, 1, 2, 3, 4}), settings);
    ASSERT_TRUE(result.has_value()) << "set " << set.id << ": " << result.error().message;
    const auto& r = result.value();
    ++run;
    feller_run += set.feller() ? 1U : 0U;
    worst_vol = std::max(worst_vol, r.max_abs_error);
    EXPECT_LT(r.max_abs_error, 1e-6) << "set " << set.id;
    EXPECT_TRUE(r.converged) << "set " << set.id;
    const auto est = estimate_of(r);
    for (std::size_t j = 0; j < kParams; ++j) {
      const double e = recovery_error(j, est[j], set.truth[j]);
      worst_param = std::max(worst_param, e);
      EXPECT_LT(e, 1e-4) << "set " << set.id << " parameter " << kNames[j] << " truth "
                         << set.truth[j] << " estimate " << est[j];
    }
  }
  RecordProperty("sets_run", std::to_string(run));
  RecordProperty("worst_vol_error", sci(worst_vol));
  RecordProperty("worst_param_error", sci(worst_param));
  std::cout << "V-11 (A): " << run << " sets (" << feller_run
            << " satisfy Feller), worst vol error " << sci(worst_vol) << ", worst parameter error "
            << sci(worst_param) << "\n";
}

TEST(V11_HestonCalibration, B_OneExpiryFitsTheQuotesButDoesNotIdentifyTheParameters) {
  const std::vector<TrueSet> sets = load_sets();
  CalibrationSettings settings;
  settings.random_starts = 2;
  const std::size_t stride = reduced_effort ? 4 : 1;
  std::size_t run = 0;
  std::size_t not_identified = 0;
  std::size_t infinite_se = 0;
  double worst_fit = 0.0;
  for (std::size_t s = 0; s < sets.size(); s += stride) {
    const TrueSet& set = sets[s];
    const auto result = calibrate_heston(set.market, slices_of(set, {3}), settings);  // 1Y only
    ASSERT_TRUE(result.has_value()) << "set " << set.id << ": " << result.error().message;
    const auto& r = result.value();
    ++run;
    worst_fit = std::max(worst_fit, r.max_abs_error);
    EXPECT_LT(r.max_abs_error, 1e-6) << "set " << set.id;
    const auto est = estimate_of(r);
    bool off = false;
    for (std::size_t j = 0; j < kParams; ++j) {
      const double e = recovery_error(j, est[j], set.truth[j]);
      off = off || e > (j == 4 ? 1e-2 : 1e-2);
    }
    not_identified += off ? 1U : 0U;
    const bool all_inf =
        std::all_of(r.standard_errors.begin(), r.standard_errors.end(), [](double se) {
          return se == std::numeric_limits<double>::infinity();
        });
    infinite_se += all_inf ? 1U : 0U;
    EXPECT_TRUE(all_inf) << "set " << set.id << " reports a finite standard error from 3 quotes";
  }
  RecordProperty("sets_run", std::to_string(run));
  RecordProperty("sets_not_identified", std::to_string(not_identified));
  std::cout << "V-11 (B): " << run << " sets, worst fit error " << sci(worst_fit) << ", "
            << not_identified << " of " << run << " not identified (parameters off by > 1%), "
            << infinite_se << " of " << run << " report infinite standard errors\n";
  EXPECT_GE(static_cast<double>(not_identified), 0.8 * static_cast<double>(run))
      << "the control must fail: one expiry cannot identify five parameters";
  EXPECT_EQ(infinite_se, run);
}

TEST(V11_HestonCalibration, C_NoisyQuotesGiveTheErrorsTheLinearTheoryPredicts) {
  if (reduced_effort) {
    GTEST_SKIP() << "noise study runs in optimised builds only (40 calibrations x 3 sets)";
  }
  const std::vector<TrueSet> sets = load_sets();
  constexpr double kSigma = 5e-4;
  constexpr std::size_t kCopies = 40;
  constexpr std::uint64_t kSeed = 20261020;
  CalibrationSettings settings;
  settings.random_starts = 0;

  for (std::size_t chosen : {std::size_t{0}, std::size_t{8}, std::size_t{16}}) {
    const TrueSet& set = sets[chosen];
    std::vector<std::array<double, kParams>> estimates;
    std::vector<std::array<double, kParams>> reported_se;
    double mean_rms2 = 0.0;
    std::size_t converged = 0;
    for (std::size_t copy = 0; copy < kCopies; ++copy) {
      // Noise on the 15 pivot volatilities (put, ATM, call of each expiry), then back to quotes.
      fxval::mc::NormalStream noise(kSeed, chosen * 1000 + copy);
      std::vector<MarketSlice> slices;
      for (std::size_t e = 0; e < set.expiries.size(); ++e) {
        double z[4];
        noise.next_pair(z[0], z[1]);
        noise.next_pair(z[2], z[3]);
        const double atm = set.quotes[e][0];
        const double call = atm + set.quotes[e][2] + 0.5 * set.quotes[e][1];
        const double put = atm + set.quotes[e][2] - 0.5 * set.quotes[e][1];
        const double atm_n = atm + kSigma * z[0];
        const double call_n = call + kSigma * z[1];
        const double put_n = put + kSigma * z[2];
        slices.push_back(
            slice_of(set.expiries[e], {atm_n, call_n - put_n, 0.5 * (call_n + put_n) - atm_n}));
      }
      const auto result = calibrate_heston(set.market, slices, settings);
      ASSERT_TRUE(result.has_value()) << result.error().message;
      const auto& r = result.value();
      converged += r.converged ? 1U : 0U;
      mean_rms2 += r.rms_error * r.rms_error / static_cast<double>(kCopies);
      estimates.push_back(estimate_of(r));
      reported_se.push_back(r.standard_errors);
    }
    EXPECT_EQ(converged, kCopies) << "set " << set.id;
    constexpr double n = 15.0;
    constexpr double p = 5.0;
    const double expected_rms2 = kSigma * kSigma * (n - p) / n;
    EXPECT_NEAR(mean_rms2 / expected_rms2, 1.0, 0.25) << "set " << set.id;

    std::cout << "V-11 (C) set " << set.id << " (Feller " << (set.feller() ? "yes" : "no")
              << "): mean rms^2 / expected = " << mean_rms2 / expected_rms2 << "\n";
    for (std::size_t j = 0; j < kParams; ++j) {
      double mean = 0.0;
      double mean_se = 0.0;
      for (std::size_t c = 0; c < kCopies; ++c) {
        mean += estimates[c][j] / static_cast<double>(kCopies);
        mean_se += reported_se[c][j] / static_cast<double>(kCopies);
      }
      double var = 0.0;
      for (std::size_t c = 0; c < kCopies; ++c) {
        var +=
            (estimates[c][j] - mean) * (estimates[c][j] - mean) / static_cast<double>(kCopies - 1);
      }
      const double sd = std::sqrt(var);
      const double ratio = sd / mean_se;
      const double bias_in_se = std::abs(mean - set.truth[j]) / (sd / std::sqrt(40.0));
      std::cout << "    " << kNames[j] << ": truth " << set.truth[j] << " mean " << mean << " sd "
                << sd << " reported se " << mean_se << " ratio " << ratio << " bias/se(mean) "
                << bias_in_se << "\n";
      EXPECT_GT(ratio, 0.6) << "set " << set.id << " " << kNames[j];
      EXPECT_LT(ratio, 1.6) << "set " << set.id << " " << kNames[j];
      EXPECT_LT(bias_in_se, 4.0) << "set " << set.id << " " << kNames[j];
    }
  }
}

}  // namespace
