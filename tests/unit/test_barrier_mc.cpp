// Unit tests of the barrier Monte Carlo: reproducibility, thread invariance, error paths.

#include <gtest/gtest.h>

#include "fxval/mc/barrier_mc.hpp"
#include "support/barrier_test_support.hpp"
#include "support/mc_test_support.hpp"

namespace {

using namespace fxval;
using fxval::testing::must;

pricing::BarrierContract doc() {
  return fxval::testing::make_barrier_vanilla(pricing::OptionType::call,
                                              1.10,
                                              1.00,
                                              pricing::BarrierDirection::down,
                                              pricing::BarrierEffect::knock_out);
}

TEST(BarrierMcTest, SameSeedGivesBitIdenticalResultsAndThreadsDoNotChangeThem) {
  const auto m = fxval::testing::make_fx_market(1.10, 0.03, 0.01, 1.0);
  const auto one = must(mc::gbm_barrier_mc_price(m,
                                                 fxval::testing::vol(0.1),
                                                 doc(),
                                                 mc::BarrierMonitoring::bridge,
                                                 fxval::testing::make_config(7, 20'000, 8, 1)));
  const auto again = must(mc::gbm_barrier_mc_price(m,
                                                   fxval::testing::vol(0.1),
                                                   doc(),
                                                   mc::BarrierMonitoring::bridge,
                                                   fxval::testing::make_config(7, 20'000, 8, 1)));
  const auto four = must(mc::gbm_barrier_mc_price(m,
                                                  fxval::testing::vol(0.1),
                                                  doc(),
                                                  mc::BarrierMonitoring::bridge,
                                                  fxval::testing::make_config(7, 20'000, 8, 4)));
  EXPECT_EQ(fxval::testing::bits_of(one.price), fxval::testing::bits_of(again.price));
  EXPECT_EQ(fxval::testing::bits_of(one.price), fxval::testing::bits_of(four.price));
  const auto other = must(mc::gbm_barrier_mc_price(m,
                                                   fxval::testing::vol(0.1),
                                                   doc(),
                                                   mc::BarrierMonitoring::bridge,
                                                   fxval::testing::make_config(8, 20'000, 8, 1)));
  EXPECT_NE(fxval::testing::bits_of(one.price), fxval::testing::bits_of(other.price));
}

TEST(BarrierMcTest, BridgeKnockOutIsBelowDiscreteMonitoring) {
  const auto m = fxval::testing::make_fx_market(1.10, 0.03, 0.01, 1.0);
  const auto cfg = fxval::testing::make_config(11, 50'000, 6);
  const auto bridge = must(mc::gbm_barrier_mc_price(
      m, fxval::testing::vol(0.15), doc(), mc::BarrierMonitoring::bridge, cfg));
  const auto discrete = must(mc::gbm_barrier_mc_price(
      m, fxval::testing::vol(0.15), doc(), mc::BarrierMonitoring::discrete, cfg));
  EXPECT_LT(bridge.price, discrete.price);
}

TEST(BarrierMcTest, BreachedBarrierAndEmptyConfigAreErrors) {
  const auto m = fxval::testing::make_fx_market(1.10, 0.03, 0.01, 1.0);
  const pricing::BarrierContract breached =
      fxval::testing::make_barrier_vanilla(pricing::OptionType::call,
                                           1.10,
                                           1.20,
                                           pricing::BarrierDirection::down,
                                           pricing::BarrierEffect::knock_out);
  EXPECT_FALSE(mc::gbm_barrier_mc_price(m,
                                        fxval::testing::vol(0.1),
                                        breached,
                                        mc::BarrierMonitoring::bridge,
                                        fxval::testing::make_config(1, 1000, 8))
                   .has_value());
  EXPECT_FALSE(mc::gbm_barrier_mc_price(m,
                                        fxval::testing::vol(0.1),
                                        doc(),
                                        mc::BarrierMonitoring::bridge,
                                        fxval::testing::make_config(1, 0, 8))
                   .has_value());
  EXPECT_FALSE(mc::gbm_barrier_mc_price(m,
                                        fxval::testing::vol(0.1),
                                        doc(),
                                        mc::BarrierMonitoring::bridge,
                                        fxval::testing::make_config(1, 1000, 0))
                   .has_value());
}

}  // namespace
