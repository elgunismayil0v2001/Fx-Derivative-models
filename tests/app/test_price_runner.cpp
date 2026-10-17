// Tests for the pricing runner: it must agree exactly with direct library calls, keep options
// independent of each other, and isolate a failure in one option. Properties checked here (put-call
// parity, order invariance, thread-count invariance, notional scaling) are checked end to end,
// through configuration text, which is the path a user's input takes.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string_view>

#include "app_test_support.hpp"
#include "fxval/mc/vanilla.hpp"
#include "fxval/models/heston.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"
#include "price_runner.hpp"

namespace {

using fxval::app::OptionResult;
using fxval::app::PriceReport;
using fxval::app::run_price;
using fxval::apptest::heston_mc_json;
using fxval::apptest::must_parse;
using fxval::apptest::must_parse_example;
using fxval::testing::bits_of;
using fxval::testing::scaled_paths;

const OptionResult& find(const PriceReport& report, std::string_view id) {
  for (const OptionResult& option : report.options) {
    if (option.id == id) {
      return option;
    }
  }
  ADD_FAILURE() << "no option " << id;
  std::abort();
}

TEST(PriceRunnerTest, GarmanKohlhagenAgreesBitForBitWithTheLibrary) {
  const auto config = must_parse_example("gk_vanillas.json");
  const PriceReport report = run_price(config);
  ASSERT_EQ(report.options.size(), config.options.size());
  ASSERT_EQ(report.failed(), 0U);
  const auto& spec = std::get<fxval::app::GarmanKohlhagenSpec>(config.model);
  for (std::size_t i = 0; i < config.options.size(); ++i) {
    const auto& option = config.options[i];
    const fxval::pricing::VanillaFx vanilla{config.market.spot,
                                            option.strike,
                                            config.market.domestic,
                                            config.market.foreign,
                                            option.expiry,
                                            spec.volatility};
    const double direct = fxval::pricing::garman_kohlhagen_price(vanilla, option.type).value();
    EXPECT_EQ(bits_of(report.options[i].price), bits_of(direct)) << option.id;
    EXPECT_EQ(report.options[i].id, option.id);
    ASSERT_TRUE(report.options[i].greeks.has_value());
    const auto greeks = fxval::pricing::garman_kohlhagen_greeks(vanilla, option.type).value();
    EXPECT_EQ(bits_of(report.options[i].greeks->vega), bits_of(greeks.vega));
    EXPECT_FALSE(report.options[i].standard_error.has_value());
  }
}

TEST(PriceRunnerTest, HestonSemiAnalyticAgreesBitForBitWithTheLibrary) {
  const auto config = must_parse_example("heston_semi_analytic_smile.json");
  const PriceReport report = run_price(config);
  ASSERT_EQ(report.failed(), 0U);
  const auto& params = std::get<fxval::app::HestonSemiAnalyticSpec>(config.model).params;
  for (std::size_t i = 0; i < config.options.size(); ++i) {
    const auto& option = config.options[i];
    const fxval::models::HestonVanilla vanilla{config.market.spot,
                                               option.strike,
                                               config.market.domestic,
                                               config.market.foreign,
                                               option.expiry};
    const double direct = fxval::models::heston_price(vanilla, params, option.type).value();
    EXPECT_EQ(bits_of(report.options[i].price), bits_of(direct)) << option.id;
    EXPECT_FALSE(report.options[i].greeks.has_value());  // no Heston Greeks yet
  }
}

TEST(PriceRunnerTest, PresentValueIsPriceTimesNotional) {
  const PriceReport report = run_price(must_parse_example("gk_vanillas.json"));
  const OptionResult& atm = find(report, "EURUSD-1Y-ATM-C");
  EXPECT_EQ(atm.notional, 1'000'000.0);
  EXPECT_EQ(bits_of(atm.present_value), bits_of(atm.price * 1'000'000.0));
  const OptionResult& unit = find(report, "EURUSD-2W-ATM-C");
  EXPECT_EQ(unit.notional, 1.0);
  EXPECT_EQ(bits_of(unit.present_value), bits_of(unit.price));
}

TEST(PriceRunnerTest, PutCallParityHoldsThroughTheConfigurationPath) {
  const auto config = must_parse_example("gk_vanillas.json");
  const PriceReport report = run_price(config);
  const OptionResult& call = find(report, "EURUSD-1Y-ATM-C");
  const OptionResult& put = find(report, "EURUSD-1Y-ATM-P");
  const double t = call.expiry_years;
  const double expected =
      config.market.spot.value() * std::exp(-config.market.foreign.value() * t) -
      call.strike * std::exp(-config.market.domestic.value() * t);
  EXPECT_NEAR((call.price - put.price) / expected, 1.0, 1e-12);
}

TEST(PriceRunnerTest, EveryOptionIsIndependentOfTheOthersAndOfTheirOrder) {
  auto json = heston_mc_json("qe", scaled_paths(20'000), 2, true, true);
  const auto forward = run_price(must_parse(json.dump()));
  auto reversed_json = json;
  std::reverse(reversed_json["options"].begin(), reversed_json["options"].end());
  const auto reversed = run_price(must_parse(reversed_json.dump()));
  ASSERT_EQ(forward.options.size(), reversed.options.size());
  for (const OptionResult& option : forward.options) {
    const OptionResult& other = find(reversed, option.id);
    EXPECT_EQ(bits_of(option.price), bits_of(other.price)) << option.id;
    EXPECT_EQ(bits_of(*option.standard_error), bits_of(*other.standard_error)) << option.id;
  }
}

TEST(PriceRunnerTest, ResultsDoNotDependOnTheThreadCountSetInTheConfiguration) {
  // V-07 (thread-count invariance), reached through the user-facing configuration.
  const std::size_t paths = scaled_paths(30'000);
  const auto one = run_price(must_parse(heston_mc_json("qe", paths, 1, true, true).dump()));
  for (const unsigned threads : {2U, 3U, 8U, 0U}) {
    const auto many =
        run_price(must_parse(heston_mc_json("qe", paths, threads, true, true).dump()));
    for (const OptionResult& option : one.options) {
      const OptionResult& other = find(many, option.id);
      EXPECT_EQ(bits_of(option.price), bits_of(other.price)) << option.id << " threads " << threads;
      EXPECT_EQ(bits_of(*option.standard_error), bits_of(*other.standard_error))
          << option.id << " threads " << threads;
    }
  }
}

TEST(PriceRunnerTest, SameSeedGivesSameNumbersAndDifferentSeedDoesNot) {
  const std::size_t paths = scaled_paths(20'000);
  const auto a = run_price(must_parse(heston_mc_json("euler", paths, 1, false, false, 1).dump()));
  const auto b = run_price(must_parse(heston_mc_json("euler", paths, 1, false, false, 1).dump()));
  const auto c = run_price(must_parse(heston_mc_json("euler", paths, 1, false, false, 2).dump()));
  for (std::size_t i = 0; i < a.options.size(); ++i) {
    EXPECT_EQ(bits_of(a.options[i].price), bits_of(b.options[i].price));
    EXPECT_NE(bits_of(a.options[i].price), bits_of(c.options[i].price));
  }
}

TEST(PriceRunnerTest, MonteCarloPriceIsConsistentWithTheSemiAnalyticPrice) {
  // A sanity check of the wiring (the statistical evidence is V-05, V-06 and V-08, which test the
  // library directly): QE with variance reduction against the semi-analytic price of the same
  // model, within 4 standard errors, for the four options. Seed fixed, so the outcome is fixed.
  auto mc_json = heston_mc_json("qe", scaled_paths(100'000), 2, true, true);
  const PriceReport mc = run_price(must_parse(mc_json.dump()));
  auto exact_json = mc_json;
  exact_json["model"].erase("scheme");
  exact_json["model"].erase("monte_carlo");
  exact_json["model"]["type"] = "heston_semi_analytic";
  const PriceReport exact = run_price(must_parse(exact_json.dump()));
  for (const OptionResult& option : mc.options) {
    ASSERT_TRUE(option.standard_error.has_value());
    const double z = (option.price - find(exact, option.id).price) / *option.standard_error;
    EXPECT_LT(std::abs(z), 4.0) << option.id;
  }
}

TEST(PriceRunnerTest, GbmMonteCarloIsConsistentWithGarmanKohlhagen) {
  auto json = nlohmann::json::parse(fxval::apptest::example_text("gk_vanillas.json"));
  const PriceReport exact = run_price(must_parse(json.dump()));
  json["model"]["type"] = "gbm_monte_carlo";
  json["model"]["monte_carlo"] = {{"seed", 99}, {"paths", scaled_paths(200'000)}, {"threads", 2}};
  const PriceReport mc = run_price(must_parse(json.dump()));
  for (const OptionResult& option : mc.options) {
    const double z = (option.price - find(exact, option.id).price) / *option.standard_error;
    EXPECT_LT(std::abs(z), 4.0) << option.id;
  }
}

TEST(PriceRunnerTest, AFailingOptionIsRecordedAndTheOthersAreStillPriced) {
  const auto config = must_parse_example("gk_vanillas.json");
  int calls = 0;
  const fxval::app::OptionPricer flaky =
      [&](const fxval::app::PriceConfig& cfg,
          const fxval::app::OptionSpec& option) -> fxval::Result<fxval::app::PricedOption> {
    ++calls;
    if (option.id == "EURUSD-1Y-ATM-P") {
      return fxval::Error{fxval::ErrorCode::numerical_failure, "synthetic failure"};
    }
    return fxval::app::price_option(cfg, option);
  };
  const PriceReport report = run_price(config, flaky);
  EXPECT_EQ(calls, 5);
  ASSERT_EQ(report.options.size(), 5U);
  EXPECT_EQ(report.failed(), 1U);
  EXPECT_FALSE(find(report, "EURUSD-1Y-ATM-P").ok);
  EXPECT_EQ(find(report, "EURUSD-1Y-ATM-P").error, "synthetic failure");
  EXPECT_TRUE(find(report, "EURUSD-1Y-ATM-C").ok);
  EXPECT_TRUE(find(report, "EURUSD-3M-OTM-C").ok);
  EXPECT_EQ(report.options[1].id, "EURUSD-1Y-ATM-P");  // order preserved
}

TEST(PriceRunnerTest, MissingGreeksAreExplainedNotHidden) {
  // sigma = 0 is a valid volatility for the price but Greeks are undefined there.
  auto json = nlohmann::json::parse(fxval::apptest::example_text("gk_vanillas.json"));
  json["model"]["volatility"] = 0.0;
  const PriceReport report = run_price(must_parse(json.dump()));
  for (const OptionResult& option : report.options) {
    EXPECT_TRUE(option.ok) << option.id;
    EXPECT_FALSE(option.greeks.has_value()) << option.id;
    EXPECT_NE(option.note.find("Greeks not available"), std::string::npos) << option.note;
  }
}

}  // namespace
