// Unit tests for the Monte Carlo engine, the GBM and Heston-Euler models and the vanilla
// wrappers. They protect the code (configuration errors, block handling, stream numbering,
// martingale property). Evidence that the prices are right is in tests/validation/ (V-05, V-07).

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>

#include "fxval/mc/engine.hpp"
#include "fxval/mc/gbm.hpp"
#include "fxval/mc/heston_euler.hpp"
#include "fxval/mc/vanilla.hpp"
#include "support/mc_test_support.hpp"

namespace {

using fxval::ErrorCode;
using fxval::mc::GbmModel;
using fxval::mc::HestonEulerFullTruncation;
using fxval::mc::McConfig;
using fxval::mc::NormalStream;
using fxval::mc::PathModel;
using fxval::mc::run_monte_carlo;
using fxval::pricing::OptionType;
using fxval::testing::make_config;
using fxval::testing::make_market;
using fxval::testing::make_params;
using fxval::testing::must;
using fxval::testing::scaled_paths;

struct ConstantModel {
  [[nodiscard]] double simulate_terminal_spot(NormalStream&, std::size_t) const noexcept {
    return 2.5;
  }
};
struct FirstNormalModel {
  [[nodiscard]] double simulate_terminal_spot(NormalStream& rng, std::size_t) const noexcept {
    double z = 0.0;
    double unused = 0.0;
    rng.next_pair(z, unused);
    return z;
  }
};
struct StepsEchoModel {
  [[nodiscard]] double simulate_terminal_spot(NormalStream&, std::size_t steps) const noexcept {
    return static_cast<double>(steps);
  }
};
struct InfiniteModel {
  [[nodiscard]] double simulate_terminal_spot(NormalStream&, std::size_t) const noexcept {
    return std::numeric_limits<double>::infinity();
  }
};
struct NotAModel {};
struct ThrowingSignature {
  double simulate_terminal_spot(NormalStream&, std::size_t) const { return 0.0; }  // not noexcept
};

static_assert(PathModel<ConstantModel>);
static_assert(PathModel<GbmModel>);
static_assert(PathModel<HestonEulerFullTruncation>);
static_assert(!PathModel<NotAModel>);
static_assert(!PathModel<ThrowingSignature>);

constexpr auto identity = [](double x) noexcept {
  return x;
};

TEST(McEngineTest, ConstantPayoffHasZeroSpread) {
  const auto r = must(run_monte_carlo(ConstantModel{}, identity, make_config(1, 1000, 1)));
  EXPECT_EQ(r.mean, 2.5);
  EXPECT_EQ(r.variance, 0.0);
  EXPECT_EQ(r.standard_error, 0.0);
  EXPECT_EQ(r.paths, 1000U);
}

TEST(McEngineTest, PayoffIsAppliedAndStepsArePassedToTheModel) {
  const auto doubled = [](double x) noexcept {
    return 2.0 * x;
  };
  EXPECT_EQ(must(run_monte_carlo(StepsEchoModel{}, doubled, make_config(1, 50, 37))).mean, 74.0);
}

TEST(McEngineTest, PathNumberIsTheStreamNumber) {
  // Two paths: the estimate must be the mean of the first normal of stream 0 and of stream 1.
  McConfig config = make_config(99, 2, 1);
  const auto r = must(run_monte_carlo(FirstNormalModel{}, identity, config));
  double z0 = 0.0;
  double z1 = 0.0;
  double unused = 0.0;
  NormalStream(99, 0).next_pair(z0, unused);
  NormalStream(99, 1).next_pair(z1, unused);
  EXPECT_NEAR(r.mean, 0.5 * (z0 + z1), 1e-16);
}

TEST(McEngineTest, PartialLastBlockAndOddBlockSizesAreHandled) {
  // 1001 paths in blocks of 100: ten full blocks and one block of a single path.
  McConfig config = make_config(5, 1001, 1);
  config.block_size = 100;
  const auto r = must(run_monte_carlo(FirstNormalModel{}, identity, config));
  EXPECT_EQ(r.paths, 1001U);
  fxval::mc::RunningStats reference;
  for (std::uint64_t p = 0; p < 1001; ++p) {
    double z = 0.0;
    double unused = 0.0;
    NormalStream(5, p).next_pair(z, unused);
    reference.add(z);
  }
  EXPECT_NEAR(r.mean, reference.mean(), 1e-14);
  EXPECT_NEAR(r.variance, reference.variance(), 1e-13);
}

TEST(McEngineTest, BlockSizeOneAndHugeBlockSizeBothWork) {
  McConfig a = make_config(5, 300, 1);
  a.block_size = 1;
  McConfig b = a;
  b.block_size = 1'000'000;
  const auto ra = must(run_monte_carlo(FirstNormalModel{}, identity, a));
  const auto rb = must(run_monte_carlo(FirstNormalModel{}, identity, b));
  EXPECT_NEAR(ra.mean, rb.mean, 1e-14);
  EXPECT_NEAR(ra.variance, rb.variance, 1e-13);
}

TEST(McEngineTest, MoreThreadsThanBlocksAndHardwareConcurrencyBothWork) {
  McConfig config = make_config(5, 100, 1, 64);  // 1 block, 64 requested threads
  const auto r1 = must(run_monte_carlo(FirstNormalModel{}, identity, config));
  config.threads = 0;
  const auto r2 = must(run_monte_carlo(FirstNormalModel{}, identity, config));
  EXPECT_EQ(fxval::testing::bits_of(r1.mean), fxval::testing::bits_of(r2.mean));
}

TEST(McEngineTest, EveryBadConfigurationIsReportedWithItsFieldName) {
  const auto expect_bad = [](McConfig config, const char* field) {
    const auto r = run_monte_carlo(ConstantModel{}, identity, config);
    ASSERT_FALSE(r.has_value()) << field;
    EXPECT_EQ(r.error().code, ErrorCode::invalid_argument) << field;
    EXPECT_NE(r.error().message.find(field), std::string::npos) << r.error().message;
  };
  McConfig good = make_config(1, 100, 10);
  McConfig c = good;
  c.paths = 0;
  expect_bad(c, "paths");
  c.paths = 1;
  expect_bad(c, "paths");
  c.paths = fxval::mc::max_paths + 1;
  expect_bad(c, "paths");
  c = good;
  c.steps = 0;
  expect_bad(c, "steps");
  c.steps = fxval::mc::max_steps + 1;
  expect_bad(c, "steps");
  c = good;
  c.threads = fxval::mc::max_threads + 1;
  expect_bad(c, "threads");
  c = good;
  c.block_size = 0;
  expect_bad(c, "block_size");
}

TEST(McEngineTest, NonFinitePayoffIsANumericalFailureNotANumber) {
  const auto r = run_monte_carlo(InfiniteModel{}, identity, make_config(1, 100, 1));
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ErrorCode::numerical_failure);
}

TEST(McModelsTest, ZeroExpiryIsRejected) {
  const auto spot = must(fxval::Spot::from_value(1.1));
  const auto rate = must(fxval::Rate::from_decimal(0.01));
  const auto vol = must(fxval::Volatility::from_decimal(0.1));
  const auto zero = must(fxval::Years::from_years(0.0));
  const auto gbm = GbmModel::create(spot, rate, rate, vol, zero);
  ASSERT_FALSE(gbm.has_value());
  EXPECT_EQ(gbm.error().code, ErrorCode::invalid_argument);
  const auto heston = HestonEulerFullTruncation::create(
      spot, rate, rate, zero, make_params(0.04, 2.0, 0.04, 0.3, -0.5));
  ASSERT_FALSE(heston.has_value());
  EXPECT_EQ(heston.error().code, ErrorCode::invalid_argument);
  const auto market = make_market(1.1, 1.1, 0.01, 0.01, 0.0, 0.1);
  EXPECT_FALSE(fxval::mc::gbm_mc_price(market, OptionType::call, make_config(1, 100, 1)));
}

TEST(McModelsTest, PriceWrappersPassConfigurationErrorsOn) {
  McConfig bad = make_config(1, 100, 10);
  bad.paths = 1;
  const auto gbm =
      fxval::mc::gbm_mc_price(make_market(1.1, 1.1, 0.01, 0.0, 1.0, 0.1), OptionType::put, bad);
  ASSERT_FALSE(gbm.has_value());
  EXPECT_EQ(gbm.error().code, ErrorCode::invalid_argument);
  const auto heston =
      fxval::mc::heston_euler_mc_price(fxval::testing::make_heston_market(1.1, 1.1, 0.01, 0.0, 1.0),
                                       make_params(0.04, 2.0, 0.04, 0.3, -0.5),
                                       OptionType::put,
                                       bad);
  ASSERT_FALSE(heston.has_value());
  EXPECT_EQ(heston.error().code, ErrorCode::invalid_argument);
  EXPECT_FALSE(
      fxval::mc::heston_euler_mc_price(fxval::testing::make_heston_market(1.1, 1.1, 0.01, 0.0, 0.0),
                                       make_params(0.04, 2.0, 0.04, 0.3, -0.5),
                                       OptionType::call,
                                       make_config(1, 100, 10)));
}

TEST(McModelsTest, ZeroVolatilityGbmPriceIsTheDiscountedForwardIntrinsicValue) {
  const auto market = make_market(1.2, 1.0, 0.03, 0.01, 1.0, 0.0);
  const auto r = must(fxval::mc::gbm_mc_price(market, OptionType::call, make_config(1, 100, 1)));
  const double expected = (1.2 * std::exp(0.02) - 1.0) * std::exp(-0.03);
  EXPECT_NEAR(r.price, expected, 1e-14);
  EXPECT_EQ(r.standard_error, 0.0);
}

// E[S_T] = S_0 exp((r_d - r_f) T) must hold for every step count, because each log-Euler step
// multiplies by exp(-v+ dt / 2 + sqrt(v+ dt) Z) with Z standard normal given the past. Checked
// within 4.5 standard errors (a failure is a defect, not bad luck).
TEST(McModelsTest, SimulatedSpotIsAMartingaleUnderTheDomesticMeasure) {
  const std::size_t paths = scaled_paths(200'000);
  const double forward = 1.1 * std::exp(0.02);
  const auto check = [&](const auto& model, std::size_t steps) {
    const auto r = must(run_monte_carlo(model, identity, make_config(11, paths, steps, 2)));
    EXPECT_NEAR(r.mean, forward, 4.5 * r.standard_error) << "steps " << steps;
  };
  check(must(GbmModel::create(must(fxval::Spot::from_value(1.1)),
                              must(fxval::Rate::from_decimal(0.03)),
                              must(fxval::Rate::from_decimal(0.01)),
                              must(fxval::Volatility::from_decimal(0.2)),
                              must(fxval::Years::from_years(1.0)))),
        1);
  for (const std::size_t steps : {std::size_t{1}, std::size_t{5}, std::size_t{40}}) {
    check(must(HestonEulerFullTruncation::create(must(fxval::Spot::from_value(1.1)),
                                                 must(fxval::Rate::from_decimal(0.03)),
                                                 must(fxval::Rate::from_decimal(0.01)),
                                                 must(fxval::Years::from_years(1.0)),
                                                 make_params(0.04, 2.0, 0.04, 0.3, -0.5))),
          steps);
    // A strongly Feller-violating set, where the raw variance often goes negative.
    check(must(HestonEulerFullTruncation::create(must(fxval::Spot::from_value(1.1)),
                                                 must(fxval::Rate::from_decimal(0.03)),
                                                 must(fxval::Rate::from_decimal(0.01)),
                                                 must(fxval::Years::from_years(1.0)),
                                                 make_params(0.04, 0.5, 0.04, 1.0, -0.9))),
          steps);
  }
}

// With xi = 0 and v0 = theta the variance never moves, so ln(S_T / F) is exactly
// N(-theta T / 2, theta T) for any number of steps.
TEST(McModelsTest, ConstantVarianceHestonGivesTheLognormalLaw) {
  const double theta = 0.04;
  const double years = 1.5;
  const auto model =
      must(HestonEulerFullTruncation::create(must(fxval::Spot::from_value(1.0)),
                                             must(fxval::Rate::from_decimal(0.02)),
                                             must(fxval::Rate::from_decimal(0.02)),
                                             must(fxval::Years::from_years(years)),
                                             make_params(theta, 3.0, theta, 0.0, 0.4)));
  const std::size_t n = 100'000;
  fxval::mc::RunningStats log_returns;
  for (std::uint64_t p = 0; p < n; ++p) {
    NormalStream rng(3, p);
    const auto state = model.simulate_terminal_state(rng, 7);
    EXPECT_EQ(state.variance, theta);  // exactly constant, bit for bit
    log_returns.add(std::log(state.spot));
  }
  const double dn = static_cast<double>(n);
  EXPECT_NEAR(log_returns.mean(), -0.5 * theta * years, 4.5 * std::sqrt(theta * years / dn));
  EXPECT_NEAR(log_returns.variance(), theta * years, 4.5 * theta * years * std::sqrt(2.0 / dn));
}

TEST(McModelsTest, FullTruncationIsReachedWhenFellerFailsAndSpotStaysPositive) {
  const auto model =
      must(HestonEulerFullTruncation::create(must(fxval::Spot::from_value(1.0)),
                                             must(fxval::Rate::from_decimal(0.0)),
                                             must(fxval::Rate::from_decimal(0.0)),
                                             must(fxval::Years::from_years(2.0)),
                                             make_params(0.04, 0.5, 0.04, 1.0, -0.9)));
  int negative_variance = 0;
  for (std::uint64_t p = 0; p < 5000; ++p) {
    NormalStream rng(8, p);
    const auto state = model.simulate_terminal_state(rng, 16);
    ASSERT_TRUE(std::isfinite(state.spot));
    ASSERT_GT(state.spot, 0.0);
    negative_variance += state.variance < 0.0 ? 1 : 0;
  }
  EXPECT_GT(negative_variance, 0);  // the truncation branch really is exercised
}

TEST(McModelsTest, SameStreamGivesTheSamePathBitForBit) {
  const auto model =
      must(HestonEulerFullTruncation::create(must(fxval::Spot::from_value(1.0)),
                                             must(fxval::Rate::from_decimal(0.01)),
                                             must(fxval::Rate::from_decimal(0.0)),
                                             must(fxval::Years::from_years(1.0)),
                                             make_params(0.04, 2.0, 0.04, 0.5, -0.5)));
  NormalStream a(1, 2);
  NormalStream b(1, 2);
  const auto sa = model.simulate_terminal_state(a, 30);
  const auto sb = model.simulate_terminal_state(b, 30);
  EXPECT_EQ(fxval::testing::bits_of(sa.spot), fxval::testing::bits_of(sb.spot));
  EXPECT_EQ(fxval::testing::bits_of(sa.variance), fxval::testing::bits_of(sb.variance));
}

}  // namespace
