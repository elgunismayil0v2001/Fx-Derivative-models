// V-12: barrier Monte Carlo (Brownian-bridge correction) against the closed forms.
//
// Fixed in advance (seed 20261022, never changed):
//  A. GBM + BRIDGE. The bridge is exact under GBM, so 16 time steps must reproduce the continuous
//     closed form within 3 standard errors, for 8 contracts: down-and-out call, up-and-out call,
//     down-and-in put, up-and-in call, one-touch down, one-touch up, no-touch up, double no-touch.
//  B. POWER CONTROL. The same down-and-out call with PLAIN DISCRETE monitoring at 12 steps must be
//     above the closed form by more than 3 SE (discrete monitoring misses crossings between dates,
//     so a knock-out is overpriced). If this control passed, test A would not prove anything.
//  C. HESTON, NO VOL OF VOL. Heston with xi = 1e-3 and v0 = theta is a Black-Scholes model with
//     sigma = sqrt(v0); the bridge Monte Carlo must agree with the closed form within 3 SE for a
//     down-and-out call and a one-touch.
//  D. HESTON STEP STUDY (reported, not asserted against a reference: no closed form exists).
//     Prices at 16, 64 and 256 steps go to the log; asserted only that they are finite and that the
//     standard errors are positive. The Heston bridge is an approximation (L-17).
//  Reduced path counts in debug and sanitizer builds (support/mc_test_support.hpp).

#include <gtest/gtest.h>

#include <cmath>

#include "fxval/mc/barrier_mc.hpp"
#include "support/barrier_test_support.hpp"
#include "support/mc_test_support.hpp"

namespace {

using namespace fxval;
using fxval::testing::level;
using fxval::testing::make_barrier_vanilla;
using fxval::testing::make_config;
using fxval::testing::must;
using fxval::testing::scaled_paths;
using fxval::testing::vol;
using pricing::BarrierDirection;
using pricing::BarrierEffect;
using pricing::OptionType;

constexpr std::uint64_t kSeed = 20261022;
constexpr double kRd = 0.03, kRf = 0.01, kT = 1.0, kSigma = 0.12;

pricing::FxMarket market() {
  return fxval::testing::make_fx_market(1.10, kRd, kRf, kT);
}

struct Case {
  const char* name;
  pricing::BarrierContract contract;
};

std::vector<Case> cases() {
  return {
      {"down-and-out call",
       make_barrier_vanilla(
           OptionType::call, 1.10, 1.02, BarrierDirection::down, BarrierEffect::knock_out)},
      {"up-and-out call",
       make_barrier_vanilla(
           OptionType::call, 1.05, 1.22, BarrierDirection::up, BarrierEffect::knock_out)},
      {"down-and-in put",
       make_barrier_vanilla(
           OptionType::put, 1.10, 1.03, BarrierDirection::down, BarrierEffect::knock_in)},
      {"up-and-in call",
       make_barrier_vanilla(
           OptionType::call, 1.10, 1.20, BarrierDirection::up, BarrierEffect::knock_in)},
      {"one-touch down", pricing::TouchContract{level(1.00), BarrierDirection::down, true}},
      {"one-touch up", pricing::TouchContract{level(1.22), BarrierDirection::up, true}},
      {"no-touch up", pricing::TouchContract{level(1.20), BarrierDirection::up, false}},
      {"double no-touch", pricing::DoubleNoTouchContract{level(1.00), level(1.22)}},
  };
}

TEST(V12_BarrierMc, GbmBridgeAgreesWithClosedFormWithinThreeStandardErrors) {
  for (const Case& c : cases()) {
    const double exact = must(pricing::barrier_contract_price(market(), vol(kSigma), c.contract));
    const auto mc = must(mc::gbm_barrier_mc_price(market(),
                                                  vol(kSigma),
                                                  c.contract,
                                                  mc::BarrierMonitoring::bridge,
                                                  make_config(kSeed, scaled_paths(400'000), 16)));
    RecordProperty(std::string("exact_") + c.name, std::to_string(exact));
    EXPECT_GT(mc.standard_error, 0.0) << c.name;
    EXPECT_LT(std::abs(mc.price - exact), 3.0 * mc.standard_error)
        << c.name << " mc " << mc.price << " exact " << exact << " se " << mc.standard_error;
  }
}

TEST(V12_BarrierMc, ControlDiscreteMonitoringOverpricesAKnockOutByMoreThanThreeStandardErrors) {
  const auto contract = make_barrier_vanilla(
      OptionType::call, 1.10, 1.02, BarrierDirection::down, BarrierEffect::knock_out);
  const double exact = must(pricing::barrier_contract_price(market(), vol(kSigma), contract));
  const auto mc = must(mc::gbm_barrier_mc_price(market(),
                                                vol(kSigma),
                                                contract,
                                                mc::BarrierMonitoring::discrete,
                                                make_config(kSeed, scaled_paths(400'000), 12)));
  RecordProperty("discrete_bias", std::to_string(mc.price - exact));
  EXPECT_GT(mc.price - exact, 3.0 * mc.standard_error);
}

TEST(V12_BarrierMc, HestonWithoutVolOfVolAgreesWithTheBlackScholesClosedForm) {
  const auto params = fxval::testing::make_params(0.0144, 1.5, 0.0144, 1e-3, 0.0);
  const std::vector<pricing::BarrierContract> contracts{
      make_barrier_vanilla(
          OptionType::call, 1.10, 1.02, BarrierDirection::down, BarrierEffect::knock_out),
      pricing::TouchContract{level(1.00), BarrierDirection::down, true}};
  for (const auto& contract : contracts) {
    const double exact = must(pricing::barrier_contract_price(market(), vol(0.12), contract));
    const auto mc =
        must(mc::heston_barrier_mc_price(market(),
                                         params,
                                         contract,
                                         mc::BarrierMonitoring::bridge,
                                         make_config(kSeed, scaled_paths(400'000), 32)));
    EXPECT_LT(std::abs(mc.price - exact), 3.0 * mc.standard_error)
        << "mc " << mc.price << " exact " << exact << " se " << mc.standard_error;
  }
}

TEST(V12_BarrierMc, HestonStepStudyIsReported) {
  const auto params = fxval::testing::make_params(0.0144, 1.5, 0.0144, 0.5, -0.4);
  const auto contract = make_barrier_vanilla(
      OptionType::call, 1.10, 1.02, BarrierDirection::down, BarrierEffect::knock_out);
  for (std::size_t steps : {16u, 64u, 256u}) {
    const auto mc =
        must(mc::heston_barrier_mc_price(market(),
                                         params,
                                         contract,
                                         mc::BarrierMonitoring::bridge,
                                         make_config(kSeed, scaled_paths(200'000), steps)));
    RecordProperty("heston_steps_" + std::to_string(steps), std::to_string(mc.price));
    EXPECT_TRUE(std::isfinite(mc.price));
    EXPECT_GT(mc.standard_error, 0.0);
  }
}

}  // namespace
