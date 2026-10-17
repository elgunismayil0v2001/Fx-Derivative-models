// Unit tests for antithetic variates and control variates. They protect the code (exact identities,
// error handling, thread-count invariance). Evidence that the reduced estimators are unbiased and
// how much variance they remove is in tests/validation/test_v08_variance_reduction.cpp.

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>

#include "fxval/mc/gbm.hpp"
#include "fxval/mc/heston_qe.hpp"
#include "fxval/mc/variance_reduction.hpp"
#include "support/mc_test_support.hpp"

namespace {

using fxval::ErrorCode;
using fxval::mc::GbmModel;
using fxval::mc::NoControl;
using fxval::mc::NormalStream;
using fxval::mc::run_antithetic;
using fxval::mc::run_variance_reduced;
using fxval::testing::bits_of;
using fxval::testing::make_config;
using fxval::testing::make_params;
using fxval::testing::must;

struct FirstNormalModel {
  [[nodiscard]] double simulate_terminal_spot(NormalStream& rng, std::size_t) const noexcept {
    double z = 0.0;
    double unused = 0.0;
    rng.next_pair(z, unused);
    return z;
  }
};
struct ConstantModel {
  [[nodiscard]] double simulate_terminal_spot(NormalStream&, std::size_t) const noexcept {
    return 2.5;
  }
};

constexpr auto identity = [](double x) noexcept {
  return x;
};

GbmModel make_gbm() {
  return must(GbmModel::create(must(fxval::Spot::from_value(1.1)),
                               must(fxval::Rate::from_decimal(0.03)),
                               must(fxval::Rate::from_decimal(0.01)),
                               must(fxval::Volatility::from_decimal(0.2)),
                               must(fxval::Years::from_years(1.0))));
}

TEST(AntitheticStreamTest, ReturnsExactlyTheNegatedNumbersOfTheOrdinaryStream) {
  for (const std::uint64_t stream : {0ULL, 1ULL, 12345ULL, (1ULL << 40U) + 3U}) {
    NormalStream plain(77, stream);
    NormalStream mirror(77, stream, true);
    for (int block = 0; block < 20; ++block) {
      double a1 = 0.0;
      double a2 = 0.0;
      double b1 = 0.0;
      double b2 = 0.0;
      plain.next_pair(a1, a2);
      mirror.next_pair(b1, b2);
      EXPECT_EQ(bits_of(b1), bits_of(-a1));
      EXPECT_EQ(bits_of(b2), bits_of(-a2));
    }
  }
}

TEST(AntitheticStreamTest, DefaultConstructionIsUnchangedFromWeekFour) {
  // The third constructor argument has a default, so existing call sites keep their numbers.
  NormalStream a(5, 6);
  NormalStream b(5, 6, false);
  double a1 = 0.0;
  double a2 = 0.0;
  double b1 = 0.0;
  double b2 = 0.0;
  a.next_pair(a1, a2);
  b.next_pair(b1, b2);
  EXPECT_EQ(bits_of(a1), bits_of(b1));
  EXPECT_EQ(bits_of(a2), bits_of(b2));
}

TEST(AntitheticTest, OddFunctionOfTheNoiseCancelsExactly) {
  // f(z) = z is odd, so each pair averages to exactly zero: the estimator has zero mean and zero
  // variance, which can only happen if the second path really uses -z.
  const auto r = must(run_antithetic(FirstNormalModel{}, identity, make_config(3, 1000, 1)));
  EXPECT_EQ(r.mean, 0.0);
  EXPECT_EQ(r.variance, 0.0);
  EXPECT_EQ(r.paths, 1000U);
}

TEST(AntitheticTest, EvenFunctionOfTheNoiseGainsNothing) {
  // f(z) = z^2: z and -z give the same value, so the pair average equals the single value and the
  // variance is that of the plain estimator (no gain, no loss).
  const auto square = [](double x) noexcept {
    return x * x;
  };
  const auto plain =
      must(fxval::mc::run_monte_carlo(FirstNormalModel{}, square, make_config(3, 4000, 1)));
  const auto anti = must(run_antithetic(FirstNormalModel{}, square, make_config(3, 4000, 1)));
  EXPECT_EQ(bits_of(plain.mean), bits_of(anti.mean));
  EXPECT_EQ(bits_of(plain.variance), bits_of(anti.variance));
}

TEST(ControlVariateTest, PerfectControlRemovesAllVariance) {
  const auto model = make_gbm();
  const double forward = 1.1 * std::exp(0.02);
  const auto r = must(run_variance_reduced(
      model, identity, identity, forward, 1.0, false, make_config(8, 5000, 1)));
  EXPECT_NEAR(r.mean, forward, 1e-12);
  EXPECT_NEAR(r.variance, 0.0, 1e-24);
}

TEST(ControlVariateTest, PilotRecoversTheKnownCoefficient) {
  // payoff = 3 S and control = S: Cov / Var = 3 exactly, whatever the sample.
  const auto model = make_gbm();
  const auto triple = [](double x) noexcept {
    return 3.0 * x;
  };
  const auto c =
      must(fxval::mc::estimate_control_coefficient(model, triple, identity, false, 4, 1, 500));
  EXPECT_NEAR(c, 3.0, 1e-12);
  const auto c_anti =
      must(fxval::mc::estimate_control_coefficient(model, triple, identity, true, 4, 1, 500));
  EXPECT_NEAR(c_anti, 3.0, 1e-12);
}

TEST(ControlVariateTest, PilotUsesStreamsThatTheMainRunCannotUse) {
  // The pilot covariance must be the one of paths pilot_stream_offset + i. Reproduce it by hand.
  const auto model = make_gbm();
  const auto square = [](double x) noexcept {
    return x * x;
  };
  const auto c =
      must(fxval::mc::estimate_control_coefficient(model, square, identity, false, 21, 1, 200));
  double sf = 0.0;
  double sg = 0.0;
  double sfg = 0.0;
  double sgg = 0.0;
  for (std::uint64_t i = 0; i < 200; ++i) {
    NormalStream rng(21, fxval::mc::pilot_stream_offset + i);
    const double s = model.simulate_terminal_spot(rng, 1);
    sf += s * s;
    sg += s;
    sfg += s * s * s;
    sgg += s * s;
  }
  const double n = 200.0;
  const double expected = (sfg - sf * sg / n) / (sgg - sg * sg / n);
  EXPECT_NEAR(c, expected, 1e-10);
  EXPECT_GE(fxval::mc::pilot_stream_offset, static_cast<std::uint64_t>(fxval::mc::max_paths));
}

TEST(ControlVariateTest, ErrorsAreReportedAsValues) {
  const auto model = make_gbm();
  const auto few =
      fxval::mc::estimate_control_coefficient(model, identity, identity, false, 1, 1, 99);
  ASSERT_FALSE(few.has_value());
  EXPECT_EQ(few.error().code, ErrorCode::invalid_argument);
  const auto bad_steps =
      fxval::mc::estimate_control_coefficient(model, identity, identity, false, 1, 0, 1000);
  EXPECT_EQ(bad_steps.error().code, ErrorCode::invalid_argument);
  // A control that never varies has no usable coefficient.
  const auto flat = fxval::mc::estimate_control_coefficient(
      ConstantModel{}, identity, identity, false, 1, 1, 1000);
  ASSERT_FALSE(flat.has_value());
  EXPECT_EQ(flat.error().code, ErrorCode::numerical_failure);
  // Non-finite control mean or coefficient.
  const double nan = std::numeric_limits<double>::quiet_NaN();
  EXPECT_EQ(run_variance_reduced(model, identity, identity, nan, 1.0, false, make_config(1, 100, 1))
                .error()
                .code,
            ErrorCode::invalid_argument);
  EXPECT_EQ(run_variance_reduced(model, identity, identity, 1.0, nan, false, make_config(1, 100, 1))
                .error()
                .code,
            ErrorCode::invalid_argument);
  // Configuration errors still come from the engine.
  EXPECT_EQ(run_antithetic(model, identity, make_config(1, 1, 1)).error().code,
            ErrorCode::invalid_argument);
}

TEST(VarianceReductionTest, NoControlAndZeroCoefficientIsBitwisePlainWhenNotAntithetic) {
  const auto model = make_gbm();
  const auto plain =
      must(fxval::mc::run_monte_carlo(model, identity, make_config(6, 10'000, 1, 2)));
  const auto reduced = must(run_variance_reduced(
      model, identity, NoControl{}, 0.0, 0.0, false, make_config(6, 10'000, 1, 2)));
  EXPECT_EQ(bits_of(plain.mean), bits_of(reduced.mean));
  EXPECT_EQ(bits_of(plain.standard_error), bits_of(reduced.standard_error));
  EXPECT_EQ(bits_of(plain.variance), bits_of(reduced.variance));
}

// The V-07 property carries over: reduced estimators do not depend on the thread count.
TEST(VarianceReductionTest, ResultsAreBitwiseIdenticalForAnyThreadCount) {
  const auto params = make_params(0.04, 2.0, 0.04, 0.3, -0.5);
  const auto model =
      must(fxval::mc::HestonQuadraticExponential::create(must(fxval::Spot::from_value(1.1)),
                                                         must(fxval::Rate::from_decimal(0.03)),
                                                         must(fxval::Rate::from_decimal(0.01)),
                                                         must(fxval::Years::from_years(1.0)),
                                                         params));
  const auto call = [](double s) noexcept {
    return std::max(s - 1.1, 0.0);
  };
  const double forward = 1.1 * std::exp(0.02);
  for (const bool antithetic : {false, true}) {
    const auto reference = must(run_variance_reduced(
        model, call, identity, forward, 0.7, antithetic, make_config(12, 30'000, 8, 1)));
    for (const unsigned threads : {2U, 3U, 7U, 16U, 0U}) {
      const auto r = must(run_variance_reduced(
          model, call, identity, forward, 0.7, antithetic, make_config(12, 30'000, 8, threads)));
      EXPECT_EQ(bits_of(reference.mean), bits_of(r.mean)) << threads << " threads";
      EXPECT_EQ(bits_of(reference.standard_error), bits_of(r.standard_error)) << threads;
      EXPECT_EQ(bits_of(reference.variance), bits_of(r.variance)) << threads;
    }
  }
}

TEST(VanillaReductionTest, WrappersReturnFinitePricesAndReportErrors) {
  const auto params = make_params(0.04, 2.0, 0.04, 0.3, -0.5);
  const auto market = fxval::testing::make_heston_market(1.1, 1.1, 0.03, 0.01, 1.0);
  fxval::mc::McReduction both;
  both.antithetic = true;
  both.forward_control = true;
  const auto qe = must(fxval::mc::heston_qe_mc_price(
      market, params, fxval::pricing::OptionType::call, make_config(2, 20'000, 8, 2), both));
  EXPECT_TRUE(std::isfinite(qe.price));
  EXPECT_GT(qe.standard_error, 0.0);
  const auto euler = must(fxval::mc::heston_euler_mc_price(
      market, params, fxval::pricing::OptionType::call, make_config(2, 20'000, 8, 2), both));
  EXPECT_TRUE(std::isfinite(euler.price));

  fxval::mc::McReduction tiny_pilot;
  tiny_pilot.forward_control = true;
  tiny_pilot.pilot_paths = 10;
  EXPECT_EQ(
      fxval::mc::heston_qe_mc_price(
          market, params, fxval::pricing::OptionType::call, make_config(2, 1000, 8), tiny_pilot)
          .error()
          .code,
      ErrorCode::invalid_argument);

  const auto no_vol_of_vol = make_params(0.04, 2.0, 0.04, 0.0, -0.5);
  EXPECT_EQ(fxval::mc::heston_qe_mc_price(
                market, no_vol_of_vol, fxval::pricing::OptionType::call, make_config(2, 1000, 8))
                .error()
                .code,
            ErrorCode::invalid_argument);
}

}  // namespace
