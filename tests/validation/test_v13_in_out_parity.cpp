// V-13: in-out parity. A knock-in plus a knock-out with the same barrier is the vanilla.
//
// The library prices the knock-in and the knock-out from separate formula tables (Reiner and
// Rubinstein), neither of which is computed as "vanilla minus the other", so the identity
// KI + KO = vanilla is a check, not a definition. Criterion (fixed in advance): 1e-12 for 96
// contracts (call/put, down/up, 3 strikes, 4 barrier distances including strike above/below the
// barrier, 2 volatilities); and no-touch + one-touch = discount factor, 1e-13.
// CONTROL: a knock-in with a barrier shifted by 0.5% added to the knock-out must miss the
// vanilla by more than 1e-6 for at least 75 of the 96 cases, so the parity test can fail. (The
// first draft said 80, a guess; the first run gave 79. My first explanation of the misses, "far
// barriers" only, was wrong, and the test said so; my second, "knock-out worthless" only, was
// also incomplete. The undetected cases are those where ONE LEG IS WORTH (almost) NOTHING: a far
// barrier makes the knock-in ~0, a strike beyond an up barrier makes the knock-out 0, and then the
// shifted barrier cannot change the sum. The test asserts that explanation (smaller leg < 1e-4),
// so the threshold is explained and not tuned.)

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "support/barrier_test_support.hpp"

namespace {

using namespace fxval;
using fxval::testing::barrier_price;
using fxval::testing::make_barrier_vanilla;
using fxval::testing::must;
using fxval::testing::vol;
using pricing::BarrierDirection;
using pricing::BarrierEffect;
using pricing::OptionType;

double vanilla(const pricing::FxMarket& m, double sigma, OptionType type, double strike) {
  return must(pricing::garman_kohlhagen_price(
      pricing::VanillaFx{
          m.spot, must(Strike::from_value(strike)), m.domestic, m.foreign, m.expiry, vol(sigma)},
      type));
}

TEST(V13_InOutParity, KnockInPlusKnockOutIsTheVanilla) {
  const auto m = fxval::testing::make_fx_market(1.10, 0.035, 0.012, 0.75);
  int cases = 0, control_detected = 0;
  for (OptionType type : {OptionType::call, OptionType::put}) {
    for (BarrierDirection dir : {BarrierDirection::down, BarrierDirection::up}) {
      for (double strike : {1.02, 1.10, 1.18}) {
        for (double distance : {0.03, 0.08, 0.15, 0.25}) {
          const double barrier =
              dir == BarrierDirection::down ? 1.10 * (1.0 - distance) : 1.10 * (1.0 + distance);
          for (double sigma : {0.08, 0.18}) {
            const auto ko =
                make_barrier_vanilla(type, strike, barrier, dir, BarrierEffect::knock_out);
            const auto ki =
                make_barrier_vanilla(type, strike, barrier, dir, BarrierEffect::knock_in);
            const double sum = barrier_price(m, sigma, ko) + barrier_price(m, sigma, ki);
            const double v = vanilla(m, sigma, type, strike);
            EXPECT_NEAR(sum, v, 1e-12) << "case " << cases;
            const double shifted = barrier * (dir == BarrierDirection::down ? 0.995 : 1.005);
            const auto bad =
                make_barrier_vanilla(type, strike, shifted, dir, BarrierEffect::knock_in);
            if (std::abs(barrier_price(m, sigma, ko) + barrier_price(m, sigma, bad) - v) > 1e-6) {
              ++control_detected;
            } else {
              EXPECT_LT(std::min(barrier_price(m, sigma, ko), barrier_price(m, sigma, ki)), 1e-4)
                  << "undetected, but both legs are worth something";
            }
            ++cases;
          }
        }
      }
    }
  }
  RecordProperty("cases", cases);
  RecordProperty("control_detected", control_detected);
  EXPECT_EQ(cases, 96);
  EXPECT_GE(control_detected, 75);
}

TEST(V13_InOutParity, NoTouchPlusOneTouchIsTheDiscountFactor) {
  const auto m = fxval::testing::make_fx_market(1.10, 0.035, 0.012, 0.75);
  for (auto dir : {BarrierDirection::down, BarrierDirection::up}) {
    for (double distance : {0.02, 0.06, 0.12}) {
      const double b =
          dir == BarrierDirection::down ? 1.10 * (1 - distance) : 1.10 * (1 + distance);
      const double s = must(pricing::one_touch_price(m, vol(0.1), fxval::testing::level(b), dir)) +
                       must(pricing::no_touch_price(m, vol(0.1), fxval::testing::level(b), dir));
      EXPECT_NEAR(s, std::exp(-0.035 * 0.75), 1e-13);
    }
  }
}

}  // namespace
