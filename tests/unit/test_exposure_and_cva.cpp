// Unit tests of exposure simulation, netting, exposure profiles and CVA: hand-computed values,
// structural properties and error paths.

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "support/credit_test_support.hpp"

namespace {

using fxval::ErrorCode;
using fxval::testing::credit_market;
using fxval::testing::forward_trade;
using fxval::testing::must;
using fxval::testing::option_trade;
namespace credit = fxval::credit;
using fxval::pricing::OptionType;

credit::ExposureSettings settings(std::vector<double> dates, std::size_t paths = 2000) {
  credit::ExposureSettings s;
  s.dates = std::move(dates);
  s.paths = paths;
  s.seed = 7;
  return s;
}

TEST(ExposureTest, ForwardValueByHand) {
  const auto m = credit_market();
  const auto f = forward_trade(1.0e6, 1.00, 1.0);
  const double expected = 1.0e6 * (1.10 * std::exp(-0.01) - 1.00 * std::exp(-0.03));
  EXPECT_NEAR(credit::forward_value(m, f, 1.10, 1.0), expected, 1e-8);
  EXPECT_DOUBLE_EQ(credit::forward_value(m, f, 1.10, 0.0), 0.0);  // matured: nothing exposed
  EXPECT_DOUBLE_EQ(credit::forward_value(m, f, 1.10, -0.5), 0.0);
}

TEST(ExposureTest, SpotPathsAreDeterministicAndStartAtTheSpot) {
  const auto m = credit_market();
  const auto a = must(credit::simulate_spot_paths(m, settings({0.5, 1.0})));
  const auto b = must(credit::simulate_spot_paths(m, settings({0.5, 1.0})));
  EXPECT_EQ(a.data(), b.data());
  ASSERT_EQ(a.rows(), 3U);
  EXPECT_EQ(a.dates(), (std::vector<double>{0.0, 0.5, 1.0}));
  for (const double s : a.row(0)) {
    EXPECT_DOUBLE_EQ(s, 1.10);
  }
  auto other = settings({0.5, 1.0});
  other.seed = 8;
  EXPECT_NE(must(credit::simulate_spot_paths(m, other)).data(), a.data());
}

TEST(ExposureTest, TheSpotIsAMartingaleUnderTheForwardDrift) {
  // E[S_T] = S0 exp((rd - rf) T): within 4 standard errors at 20,000 paths.
  const auto m = credit_market();
  const auto sp = must(credit::simulate_spot_paths(m, settings({1.0}, 20000)));
  double sum = 0.0;
  double sq = 0.0;
  for (const double s : sp.row(1)) {
    sum += s;
    sq += s * s;
  }
  const double n = 20000.0;
  const double mean = sum / n;
  const double se = std::sqrt((sq / n - mean * mean) / n);
  EXPECT_LT(std::abs(mean - 1.10 * std::exp(0.02)), 4.0 * se);
}

TEST(ExposureTest, RealWorldDriftMovesTheMean) {
  const auto m = credit_market(0.10);
  const auto sp = must(credit::simulate_spot_paths(m, settings({1.0}, 20000)));
  double sum = 0.0;
  for (const double s : sp.row(1)) {
    sum += s;
  }
  EXPECT_NEAR(sum / 20000.0, 1.10 * std::exp(0.10), 0.01);
}

TEST(ExposureTest, OffsettingForwardsNetToExactlyZero) {
  const auto m = credit_market();
  credit::NettingSet set;
  set.forwards = {forward_trade(1.0e6, 1.05, 1.0), forward_trade(-1.0e6, 1.05, 1.0)};
  const auto v = must(credit::simulate_netting_set_values(m, set, settings({0.25, 0.5})));
  for (const double x : v.data()) {
    EXPECT_DOUBLE_EQ(x, 0.0);
  }
  const auto profile = must(credit::exposure_profile(v, 0.95));
  for (std::size_t i = 0; i < profile.ee.size(); ++i) {
    EXPECT_DOUBLE_EQ(profile.ee[i], 0.0);
    EXPECT_DOUBLE_EQ(profile.pfe[i], 0.0);
  }
}

TEST(ExposureTest, NettingNeverIncreasesExposure) {
  // max(a + b, 0) <= max(a, 0) + max(b, 0) on every path, so EE(net) <= EE(gross).
  const auto m = credit_market();
  const auto f1 = forward_trade(1.0e6, 1.00, 1.0);
  const auto f2 = forward_trade(-8.0e5, 1.20, 1.0);
  const auto o = option_trade(OptionType::call, 1.15, 1.0, 0.10, 5.0e5);
  credit::NettingSet set;
  set.forwards = {f1, f2};
  set.options = {o};
  const auto s = settings({0.25, 0.5, 0.75}, 5000);
  const auto net =
      must(credit::exposure_profile(must(credit::simulate_netting_set_values(m, set, s)), 0.95));
  const auto g1 =
      must(credit::exposure_profile(must(credit::simulate_forward_values(m, f1, s)), 0.95));
  const auto g2 =
      must(credit::exposure_profile(must(credit::simulate_forward_values(m, f2, s)), 0.95));
  const auto g3 =
      must(credit::exposure_profile(must(credit::simulate_option_values(m, o, s)), 0.95));
  bool strictly_less_somewhere = false;
  for (std::size_t i = 0; i < net.ee.size(); ++i) {
    const double gross = g1.ee[i] + g2.ee[i] + g3.ee[i];
    EXPECT_LE(net.ee[i], gross + 1e-9);
    strictly_less_somewhere = strictly_less_somewhere || net.ee[i] < gross - 1.0;
  }
  EXPECT_TRUE(strictly_less_somewhere);
}

TEST(ExposureTest, OptionValuesMatchGarmanKohlhagenAndExpire) {
  const auto m = credit_market();
  const auto o = option_trade(OptionType::call, 1.10, 0.5, 0.10, 2.0);
  const auto v = must(credit::simulate_option_values(m, o, settings({0.25, 0.75}, 50)));
  const double price = must(fxval::pricing::garman_kohlhagen_price(
      fxval::pricing::VanillaFx{m.spot, o.strike, m.domestic, m.foreign, o.expiry, o.volatility},
      OptionType::call));
  EXPECT_NEAR(v.row(0)[0], 2.0 * price, 1e-12);
  for (const double x : v.row(2)) {  // after the expiry
    EXPECT_DOUBLE_EQ(x, 0.0);
  }
  for (const double x : v.row(1)) {
    EXPECT_GT(x, 0.0);
  }
}

TEST(ExposureTest, NestedValuesAreUnbiasedAndLeaveTodayAlone) {
  const auto m = credit_market();
  const auto o = option_trade(OptionType::call, 1.10, 1.0, 0.10, 1.0);
  auto s = settings({0.5}, 4000);
  const auto analytic = must(credit::simulate_option_values(m, o, s));
  s.inner_paths = 400;
  const auto nested = must(credit::simulate_option_values(m, o, s));
  EXPECT_EQ(nested.row(0)[0], analytic.row(0)[0]);  // t = 0 is never nested
  EXPECT_NE(nested.row(1)[0], analytic.row(1)[0]);
  double diff = 0.0;
  double sq = 0.0;
  for (std::size_t p = 0; p < 4000; ++p) {
    const double d = nested.row(1)[p] - analytic.row(1)[p];
    diff += d;
    sq += d * d;
  }
  const double mean = diff / 4000.0;
  const double se = std::sqrt((sq / 4000.0 - mean * mean) / 4000.0);
  EXPECT_LT(std::abs(mean), 4.0 * se);
}

TEST(ExposureTest, ProfileByHand) {
  // two paths, dates 0 and 1: values {-1, 2} and {3, -4}
  const fxval::credit::ValueMatrix v({0.0, 1.0}, 2, {-1.0, 2.0, 3.0, -4.0});
  const auto p = must(credit::exposure_profile(v, 0.5));
  EXPECT_DOUBLE_EQ(p.ee[0], 1.0);
  EXPECT_DOUBLE_EQ(p.ee[1], 1.5);
  EXPECT_DOUBLE_EQ(p.ee_standard_error[0],
                   1.0);            // exposures {0, 2}: sample variance 2, se sqrt(2 / 2)
  EXPECT_DOUBLE_EQ(p.pfe[0], 0.0);  // k = ceil(2 * 0.5) = 1: the smallest of {0, 2}
  EXPECT_DOUBLE_EQ(p.pfe[1], 0.0);  // the smallest of {3, 0}
  EXPECT_DOUBLE_EQ(p.epe, 1.25);
  const auto top = must(credit::exposure_profile(v, 0.99));  // k = 2: the largest
  EXPECT_DOUBLE_EQ(top.pfe[0], 2.0);
  EXPECT_DOUBLE_EQ(top.pfe[1], 3.0);
}

TEST(CvaTest, WeightsAndProfileByHand) {
  const std::vector<double> dates{0.0, 1.0};
  const credit::CreditInputs in{0.10, 0.40, 0.05};
  const double dq = 1.0 - std::exp(-0.10);
  const auto w = must(credit::cva_weights(dates, in));
  ASSERT_EQ(w.size(), 2U);
  EXPECT_NEAR(w[0], 0.6 * 0.5 * dq, 1e-15);
  EXPECT_NEAR(w[1], 0.6 * 0.5 * std::exp(-0.05) * dq, 1e-15);
  const std::vector<double> ee{2.0, 4.0};
  EXPECT_NEAR(must(credit::cva_from_profile(dates, ee, in)), w[0] * 2.0 + w[1] * 4.0, 1e-15);
}

TEST(CvaTest, ZeroHazardAndFullExposureLimits) {
  const std::vector<double> dates{0.0, 0.5, 1.0};
  const std::vector<double> ee{1.0, 2.0, 3.0};
  EXPECT_DOUBLE_EQ(must(credit::cva_from_profile(dates, ee, {0.0, 0.4, 0.03})), 0.0);
  // constant discounted exposure c: CVA = (1 - R) c (1 - exp(-lambda T)) exactly (the sum
  // telescopes)
  const credit::CreditInputs in{0.07, 0.25, 0.02};
  std::vector<double> flat(dates.size());
  for (std::size_t i = 0; i < dates.size(); ++i) {
    flat[i] = 5.0 * std::exp(in.domestic_rate * dates[i]);
  }
  EXPECT_NEAR(
      must(credit::cva_from_profile(dates, flat, in)), 0.75 * 5.0 * (1.0 - std::exp(-0.07)), 1e-14);
}

TEST(CvaTest, EstimateOfAConstantMatrixMatchesTheProfileAndHasNoError) {
  const fxval::credit::ValueMatrix v({0.0, 0.5, 1.0}, 3, {1, 1, 1, 2, 2, 2, -3, -3, -3});
  const credit::CreditInputs in{0.05, 0.4, 0.03};
  const auto e = must(credit::cva_estimate(v, in));
  EXPECT_NEAR(
      e.cva, must(credit::cva_from_profile(v.dates(), std::vector<double>{1, 2, 0}, in)), 1e-15);
  EXPECT_DOUBLE_EQ(e.standard_error, 0.0);
  const auto d = must(credit::cva_difference(v, v, in));
  EXPECT_DOUBLE_EQ(d.cva, 0.0);
  EXPECT_DOUBLE_EQ(d.standard_error, 0.0);
}

TEST(CvaTest, ErrorPaths) {
  const std::vector<double> dates{0.0, 1.0};
  EXPECT_EQ(credit::cva_weights(dates, {0.1, 1.0, 0.0}).error().code, ErrorCode::invalid_argument);
  EXPECT_FALSE(credit::cva_weights(dates, {-0.1, 0.4, 0.0}).has_value());
  EXPECT_FALSE(credit::cva_weights(std::vector<double>{0.5, 1.0}, {0.1, 0.4, 0.0}).has_value());
  EXPECT_FALSE(credit::cva_weights(std::vector<double>{0.0}, {0.1, 0.4, 0.0}).has_value());
  EXPECT_FALSE(
      credit::cva_weights(std::vector<double>{0.0, 1.0, 1.0}, {0.1, 0.4, 0.0}).has_value());
  EXPECT_FALSE(
      credit::cva_from_profile(dates, std::vector<double>{1.0}, {0.1, 0.4, 0.0}).has_value());
  const fxval::credit::ValueMatrix a({0.0, 1.0}, 2, {1, 2, 3, 4});
  const fxval::credit::ValueMatrix b({0.0, 1.0}, 3, {1, 2, 3, 4, 5, 6});
  EXPECT_FALSE(credit::cva_difference(a, b, {0.1, 0.4, 0.0}).has_value());
}

TEST(ExposureTest, ErrorPaths) {
  const auto m = credit_market();
  EXPECT_FALSE(credit::simulate_spot_paths(m, settings({})).has_value());
  EXPECT_FALSE(credit::simulate_spot_paths(m, settings({0.5, 0.5})).has_value());
  EXPECT_FALSE(credit::simulate_spot_paths(m, settings({-0.5})).has_value());
  EXPECT_FALSE(credit::simulate_spot_paths(m, settings({0.5}, 1)).has_value());
  EXPECT_FALSE(
      credit::simulate_netting_set_values(m, credit::NettingSet{}, settings({0.5})).has_value());
  const fxval::credit::ValueMatrix a({0.0, 1.0}, 2, {1, 2, 3, 4});
  const fxval::credit::ValueMatrix b({0.0, 1.0}, 3, {1, 2, 3, 4, 5, 6});
  const std::vector<credit::ValueMatrix> parts{a, b};
  EXPECT_FALSE(credit::sum_values(parts).has_value());
  EXPECT_FALSE(credit::exposure_profile(a, 1.0).has_value());
  EXPECT_FALSE(credit::exposure_profile(a, 0.0).has_value());
}

TEST(ExposureTest, ATradeIsWorthNothingOnItsOwnExpiryDate) {
  // the exposure grid contains the expiry exactly: the option has settled and the forward has
  // matured
  const auto m = credit_market();
  const auto o = option_trade(OptionType::call, 0.90, 0.5, 0.10, 1.0);  // deep in the money
  const auto f = forward_trade(1.0, 1.00, 1.0);
  const auto s = settings({0.5, 1.0}, 50);
  const auto ov = must(credit::simulate_option_values(m, o, s));
  const auto fv = must(credit::simulate_forward_values(m, f, s));
  for (const double x : ov.row(1)) {
    EXPECT_DOUBLE_EQ(x, 0.0);
  }
  for (const double x : fv.row(2)) {
    EXPECT_DOUBLE_EQ(x, 0.0);
  }
  EXPECT_GT(ov.row(0)[0], 0.0);
}

// F-24: distant exposure dates used to overflow the simulated spot, and the resulting NaN values
// were returned as a success (found by the fuzz replay).
TEST(ExposureTest, RefusesDatesBeyondTheHorizonLimit) {
  const auto m = credit_market();
  const auto r = credit::simulate_spot_paths(m, settings({1.0, 1e300}));
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::invalid_argument);
  EXPECT_FALSE(credit::simulate_spot_paths(m, settings({50.0, 100.5})).has_value());
  EXPECT_TRUE(credit::simulate_spot_paths(m, settings({50.0, 100.0}, 10)).has_value());
}

TEST(ExposureTest, RefusesTradeSizesThatOverflowTheValues) {
  const auto m = credit_market();
  const auto set =
      credit::NettingSet{{forward_trade(1e308, 1.0, 1.0), forward_trade(-1e308, 5.0, 1.0)}, {}};
  const auto r = credit::simulate_netting_set_values(m, set, settings({0.5, 1.0}, 10));
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::numerical_failure);
}

TEST(ExposureTest, ProfileAndCvaRefuseNonFiniteValues) {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  // dates must start at 0 for the CVA weights; the NaN is the only thing wrong with this matrix
  const credit::ValueMatrix bad({0.0, 0.5, 1.0}, 2, {1.0, 2.0, nan, 3.0, 0.5, 1.0});
  const auto profile = credit::exposure_profile(bad, 0.95);
  ASSERT_FALSE(profile.has_value());
  EXPECT_EQ(profile.error().code, ErrorCode::invalid_argument);
  const auto cva = credit::cva_estimate(bad, {0.1, 0.4, 0.02});
  ASSERT_FALSE(cva.has_value());
  EXPECT_EQ(cva.error().code, ErrorCode::invalid_argument);
  // The same matrix with a finite value is accepted, so the refusal is about the value.
  const credit::ValueMatrix good({0.0, 0.5, 1.0}, 2, {1.0, 2.0, 0.25, 3.0, 0.5, 1.0});
  EXPECT_TRUE(credit::cva_estimate(good, {0.1, 0.4, 0.02}).has_value());
}

TEST(ExposureTest, RefusesASpotPathThatLeavesTheRepresentableRange) {
  // A real-world drift of 1e6 per year overflows exp() within one date; a drift of -1e6 underflows
  // to zero.
  for (const double drift : {1.0e6, -1.0e6}) {
    const auto m = credit_market(drift);
    const auto r = credit::simulate_spot_paths(m, settings({1.0}, 10));
    ASSERT_FALSE(r.has_value()) << drift;
    EXPECT_EQ(r.error().code, ErrorCode::numerical_failure);
  }
}

}  // namespace
