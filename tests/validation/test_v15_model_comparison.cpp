// V-15: model comparison, GK flat ATM vs Vanna-Volga vs Heston, for barrier contracts.
//
// REPORTED, NOT PASS/FAIL: the spread between models is the quantified model risk, and there is no
// right answer to test it against. What IS asserted (fixed in advance):
//   * every calibration and every price succeeds and is finite;
//   * CONTROL, flat smile: with no smile the three prices must agree (Vanna-Volga with GK to 1e-11,
//     Heston with GK within 5 standard errors plus 2e-4 of price, the calibration cannot reach
//     zero vol-of-vol), so the machinery does not manufacture a spread;
//   * on the smiled scenarios the model-risk spread is positive for at least one contract per
//     scenario (otherwise the table would say nothing).
// Contracts, at the LONGEST expiry row of each scenario (1Y for flat/emerging/extreme_skew, 2Y for
// calm_g10 and stressed), barriers as fractions of spot S:
//   down-and-out call (K = S, H = 0.92 S), up-and-out call (K = S, H = 1.08 S),
//   one-touch down (0.92 S), one-touch up (1.08 S), double no-touch (0.92 S, 1.08 S).
// Heston is calibrated to all expiries of the scenario from 3M outwards (multi-expiry, seed
// 20261019, 2 random starts), bridge Monte Carlo 300000 paths, 100 steps, seed 20261023. The table
// goes to stdout and to the test log. Synthetic quotes (configs/scenarios/smile_scenarios.csv): not
// market data.

#include <gtest/gtest.h>

#include <cmath>
#include <iomanip>
#include <iostream>

#include "fxval/exotics/barrier_comparison.hpp"
#include "support/barrier_test_support.hpp"
#include "support/mc_test_support.hpp"

#ifndef FXVAL_SMILE_SCENARIOS_PATH
#error "FXVAL_SMILE_SCENARIOS_PATH must be defined by the build system"
#endif

namespace {

using namespace fxval;
using fxval::testing::must;
using pricing::BarrierDirection;
using pricing::BarrierEffect;
using pricing::OptionType;

struct Named {
  const char* name;
  exotics::ExoticContract contract;
};

std::vector<Named> contracts(double s) {
  return {{"DOC call H=.92",
           fxval::testing::make_barrier_vanilla(
               OptionType::call, s, 0.92 * s, BarrierDirection::down, BarrierEffect::knock_out)},
          {"UOC call H=1.08",
           fxval::testing::make_barrier_vanilla(
               OptionType::call, s, 1.08 * s, BarrierDirection::up, BarrierEffect::knock_out)},
          {"OT down .92",
           pricing::TouchContract{fxval::testing::level(0.92 * s), BarrierDirection::down, true}},
          {"OT up 1.08",
           pricing::TouchContract{fxval::testing::level(1.08 * s), BarrierDirection::up, true}},
          {"DNT .92/1.08",
           pricing::DoubleNoTouchContract{fxval::testing::level(0.92 * s),
                                          fxval::testing::level(1.08 * s)}}};
}

TEST(V15_ModelComparison, TableOfModelRisk) {
  const auto scenarios = fxval::testing::load_scenarios(FXVAL_SMILE_SCENARIOS_PATH);
  mc::McConfig config =
      fxval::testing::make_config(20261023, fxval::testing::scaled_paths(300'000), 100);
  std::cout << std::fixed << std::setprecision(5);
  for (const char* name : {"flat", "calm_g10", "stressed", "emerging", "extreme_skew"}) {
    const auto& rows = scenarios.at(name);
    const auto& row = rows.back();  // the longest-expiry row
    const double spot = row.spot;
    const auto market = fxval::testing::make_calibration_market(row.spot, row.rd, row.rf);
    std::vector<calibration::MarketSlice> slices;
    for (const auto& r : rows) {
      if (r.expiry < 0.2) {
        continue;  // calibrate from 3M outwards: the 1W and 1M quotes are not used for 1Y contracts
      }
      slices.push_back(calibration::MarketSlice{must(Years::from_years(r.expiry)),
                                                fxval::testing::make_quotes(r.atm, r.rr, r.bf)});
    }
    calibration::CalibrationSettings settings;
    settings.random_starts = 2;
    settings.compute_standard_errors = false;
    const auto fit = calibration::calibrate_heston(market, slices, settings);
    ASSERT_TRUE(fit.has_value()) << name << ": " << fit.error().message;
    const auto slice = fxval::testing::slice_from_row(row);
    std::cout << "\n== " << name << "  (expiry " << row.expiry << "Y, Heston fit rms "
              << 1e4 * fit.value().rms_error << " bp vol, max " << 1e4 * fit.value().max_abs_error
              << " bp, " << slices.size() << " expiries)\n"
              << std::left << std::setw(16) << "contract" << std::right << std::setw(10)
              << "GK flat" << std::setw(10) << "VV unw" << std::setw(10) << "VV surv"
              << std::setw(10) << "VV dec" << std::setw(10) << "Heston" << std::setw(9) << "SE"
              << std::setw(10) << "spread" << std::setw(10) << "vol pts" << std::setw(9)
              << "% notl\n";
    bool any_positive = false;
    for (const auto& c : contracts(spot)) {
      const auto cmp =
          exotics::compare_barrier_models(slice, fit.value().params, c.contract, config);
      ASSERT_TRUE(cmp.has_value()) << name << " " << c.name << ": " << cmp.error().message;
      const auto& r = cmp.value();
      std::cout << std::left << std::setw(16) << c.name << std::right << std::setw(10) << r.gk_flat
                << std::setw(10) << r.vv_unweighted << std::setw(10) << r.vv_survival
                << std::setw(10) << r.vv_vanilla_less_knock_in << std::setw(10) << r.heston
                << std::setw(9) << r.heston_standard_error << std::setw(10) << r.spread
                << std::setw(10)
                << (r.spread_vol_points ? std::to_string(*r.spread_vol_points).substr(0, 7) : "n/a")
                << std::setw(9) << r.spread_percent_notional << "\n";
      EXPECT_TRUE(std::isfinite(r.heston) && std::isfinite(r.vv_survival)) << name << c.name;
      any_positive = any_positive || r.spread > 0.0;
      if (std::string(name) == "flat") {
        EXPECT_NEAR(r.vv_survival, r.gk_flat, 1e-11) << c.name;
        EXPECT_NEAR(r.heston, r.gk_flat, 5.0 * r.heston_standard_error + 2e-4) << c.name;
      }
    }
    EXPECT_TRUE(any_positive) << name;
  }
}

}  // namespace
