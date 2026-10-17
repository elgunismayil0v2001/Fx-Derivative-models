// Tests for the fxval-price configuration parser: every rejection path (one test case per rule,
// each checking that the message names the offending field), the accepted examples, and a
// robustness test that feeds thousands of randomly damaged documents to the parser.
// These tests protect the code. Nothing here is evidence about a financial model.

#include <gtest/gtest.h>

#include <cctype>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <random>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>

#include "app_test_support.hpp"
#include "price_config.hpp"

namespace {

using fxval::ErrorCode;
using fxval::app::GarmanKohlhagenSpec;
using fxval::app::GbmMonteCarloSpec;
using fxval::app::HestonMonteCarloSpec;
using fxval::app::HestonSemiAnalyticSpec;
using fxval::app::McScheme;
using fxval::app::parse_price_config;
using nlohmann::json;

json base_gk() {
  return json::parse(R"({
    "format_version": 1,
    "market": { "spot": 1.10, "domestic_rate": 0.03, "foreign_rate": 0.01 },
    "model": { "type": "garman_kohlhagen", "volatility": 0.08 },
    "options": [
      { "id": "A", "type": "call", "strike": 1.10, "expiry_years": 1.0, "notional": 1000000 },
      { "id": "B", "type": "put", "strike": 1.05, "expiry_years": 0.5 }
    ]
  })");
}

json base_heston() {
  json config = base_gk();
  config["model"] = json::parse(R"({
    "type": "heston_semi_analytic",
    "v0": 0.04, "kappa": 2.0, "theta": 0.04, "xi": 0.3, "rho": -0.5
  })");
  return config;
}

json base_heston_mc() {
  json config = base_gk();
  config["model"] = json::parse(R"({
    "type": "heston_monte_carlo",
    "v0": 0.04, "kappa": 2.0, "theta": 0.04, "xi": 0.3, "rho": -0.5,
    "scheme": "qe",
    "monte_carlo": { "seed": 7, "paths": 1000, "steps": 8, "threads": 2,
                     "antithetic": true, "control_variate": true }
  })");
  return config;
}

json base_gbm_mc() {
  json config = base_gk();
  config["model"] = json::parse(R"({
    "type": "gbm_monte_carlo", "volatility": 0.08,
    "monte_carlo": { "seed": 7, "paths": 1000 }
  })");
  return config;
}

struct RejectCase {
  const char* name;
  json (*base)();
  std::function<void(json&)> damage;
  const char* expected;  // must appear in the error message
};

// Every rule of the format, one case each.
const RejectCase kRejectCases[] = {
    {"missing format_version",
     base_gk,
     [](json& c) { c.erase("format_version"); },
     "format_version: missing required field"},
    {"future format_version",
     base_gk,
     [](json& c) { c["format_version"] = 2; },
     "format_version: unsupported value 2"},
    {"format_version as text",
     base_gk,
     [](json& c) { c["format_version"] = "1"; },
     "format_version: unsupported value \"1\""},
    {"format_version as real",
     base_gk,
     [](json& c) { c["format_version"] = 1.5; },
     "format_version: unsupported value 1.5"},
    {"unknown top-level key", base_gk, [](json& c) { c["extra"] = 1; }, "extra: unknown field"},
    {"missing market",
     base_gk,
     [](json& c) { c.erase("market"); },
     "market: missing required field"},
    {"market not an object",
     base_gk,
     [](json& c) { c["market"] = 3; },
     "market: expected an object, got a number"},
    {"spot negative", base_gk, [](json& c) { c["market"]["spot"] = -1.0; }, "market.spot:"},
    {"spot zero", base_gk, [](json& c) { c["market"]["spot"] = 0.0; }, "market.spot:"},
    {"spot as text",
     base_gk,
     [](json& c) { c["market"]["spot"] = "1.1"; },
     "market.spot: expected a number, got a string"},
    {"spot missing",
     base_gk,
     [](json& c) { c["market"].erase("spot"); },
     "market.spot: missing required field"},
    {"rate in percent by mistake",
     base_gk,
     [](json& c) { c["market"]["domestic_rate"] = 3.0; },
     "market.domestic_rate:"},
    {"foreign rate in percent by mistake",
     base_gk,
     [](json& c) { c["market"]["foreign_rate"] = 150.0; },
     "market.foreign_rate:"},
    {"unknown market key",
     base_gk,
     [](json& c) { c["market"]["colour"] = 1; },
     "market.colour: unknown field"},
    {"volatility in percent by mistake",
     base_gk,
     [](json& c) { c["model"]["volatility"] = 8.0; },
     "model.volatility:"},
    {"volatility negative",
     base_gk,
     [](json& c) { c["model"]["volatility"] = -0.1; },
     "model.volatility:"},
    {"volatility missing",
     base_gk,
     [](json& c) { c["model"].erase("volatility"); },
     "model.volatility: missing required field"},
    {"volatility misspelt",
     base_gk,
     [](json& c) {
       c["model"].erase("volatility");
       c["model"]["volatilty"] = 0.08;
     },
     "model.volatilty: unknown field"},
    {"unknown model",
     base_gk,
     [](json& c) { c["model"]["type"] = "black_scholes"; },
     "model.type: unknown model \"black_scholes\""},
    {"model type missing",
     base_gk,
     [](json& c) { c["model"].erase("type"); },
     "model.type: missing required field"},
    {"model not an object",
     base_gk,
     [](json& c) { c["model"] = "garman_kohlhagen"; },
     "model: expected an object, got a string"},
    {"heston rho out of range", base_heston, [](json& c) { c["model"]["rho"] = 1.5; }, "model:"},
    {"heston kappa zero", base_heston, [](json& c) { c["model"]["kappa"] = 0.0; }, "model:"},
    {"heston v0 negative", base_heston, [](json& c) { c["model"]["v0"] = -0.04; }, "model:"},
    {"heston missing theta",
     base_heston,
     [](json& c) { c["model"].erase("theta"); },
     "model.theta: missing required field"},
    {"heston carries a volatility",
     base_heston,
     [](json& c) { c["model"]["volatility"] = 0.1; },
     "model.volatility: unknown field"},
    {"scheme unknown",
     base_heston_mc,
     [](json& c) { c["model"]["scheme"] = "milstein"; },
     "model.scheme: unknown scheme \"milstein\""},
    {"scheme missing",
     base_heston_mc,
     [](json& c) { c["model"].erase("scheme"); },
     "model.scheme: missing required field"},
    {"qe with xi zero",
     base_heston_mc,
     [](json& c) { c["model"]["xi"] = 0.0; },
     "model.xi: the QE scheme needs xi > 0"},
    {"monte_carlo missing",
     base_heston_mc,
     [](json& c) { c["model"].erase("monte_carlo"); },
     "model.monte_carlo: missing required field"},
    {"monte_carlo not an object",
     base_heston_mc,
     [](json& c) { c["model"]["monte_carlo"] = 5; },
     "model.monte_carlo: expected an object"},
    {"seed missing",
     base_heston_mc,
     [](json& c) { c["model"]["monte_carlo"].erase("seed"); },
     "model.monte_carlo.seed: missing required field"},
    {"seed negative",
     base_heston_mc,
     [](json& c) { c["model"]["monte_carlo"]["seed"] = -1; },
     "model.monte_carlo.seed: must not be negative, got -1"},
    {"seed fractional",
     base_heston_mc,
     [](json& c) { c["model"]["monte_carlo"]["seed"] = 1.5; },
     "model.monte_carlo.seed: expected a non-negative integer"},
    {"seed as text",
     base_heston_mc,
     [](json& c) { c["model"]["monte_carlo"]["seed"] = "7"; },
     "model.monte_carlo.seed: expected a non-negative integer, got a string"},
    {"paths zero",
     base_heston_mc,
     [](json& c) { c["model"]["monte_carlo"]["paths"] = 0; },
     "model.monte_carlo:"},
    {"paths beyond the limit",
     base_heston_mc,
     [](json& c) { c["model"]["monte_carlo"]["paths"] = 3'000'000'000ULL; },
     "model.monte_carlo:"},
    {"steps missing for heston",
     base_heston_mc,
     [](json& c) { c["model"]["monte_carlo"].erase("steps"); },
     "model.monte_carlo.steps: missing required field"},
    {"steps zero",
     base_heston_mc,
     [](json& c) { c["model"]["monte_carlo"]["steps"] = 0; },
     "model.monte_carlo:"},
    {"threads beyond the limit",
     base_heston_mc,
     [](json& c) { c["model"]["monte_carlo"]["threads"] = 1000; },
     "model.monte_carlo:"},
    {"threads absurd",
     base_heston_mc,
     [](json& c) { c["model"]["monte_carlo"]["threads"] = 18'000'000'000ULL; },
     "model.monte_carlo:"},
    {"antithetic as text",
     base_heston_mc,
     [](json& c) { c["model"]["monte_carlo"]["antithetic"] = "yes"; },
     "model.monte_carlo.antithetic: expected true or false, got a string"},
    {"control_variate as number",
     base_heston_mc,
     [](json& c) { c["model"]["monte_carlo"]["control_variate"] = 1; },
     "model.monte_carlo.control_variate: expected true or false"},
    {"unknown monte_carlo key",
     base_heston_mc,
     [](json& c) { c["model"]["monte_carlo"]["gpu"] = true; },
     "model.monte_carlo.gpu: unknown field"},
    {"gbm takes no steps",
     base_gbm_mc,
     [](json& c) { c["model"]["monte_carlo"]["steps"] = 10; },
     "model.monte_carlo.steps: unknown field"},
    {"gbm takes no antithetic",
     base_gbm_mc,
     [](json& c) { c["model"]["monte_carlo"]["antithetic"] = true; },
     "model.monte_carlo.antithetic: unknown field"},
    {"options missing",
     base_gk,
     [](json& c) { c.erase("options"); },
     "options: missing required field"},
    {"options not an array",
     base_gk,
     [](json& c) { c["options"] = json::object(); },
     "options: expected an array, got an object"},
    {"options empty",
     base_gk,
     [](json& c) { c["options"] = json::array(); },
     "options: must contain at least one option"},
    {"option not an object",
     base_gk,
     [](json& c) { c["options"][0] = 3; },
     "options[0]: expected an object, got a number"},
    {"id with a space",
     base_gk,
     [](json& c) { c["options"][0]["id"] = "bad id"; },
     "options[0].id: must be 1 to 64 characters"},
    {"id empty", base_gk, [](json& c) { c["options"][0]["id"] = ""; }, "options[0].id:"},
    {"id starts with a dash",
     base_gk,
     [](json& c) { c["options"][0]["id"] = "-x"; },
     "options[0].id:"},
    {"id starts with a formula character",
     base_gk,
     [](json& c) { c["options"][0]["id"] = "=1+1"; },
     "options[0].id:"},
    {"id with a comma", base_gk, [](json& c) { c["options"][0]["id"] = "a,b"; }, "options[0].id:"},
    {"id too long",
     base_gk,
     [](json& c) { c["options"][0]["id"] = std::string(65, 'a'); },
     "options[0].id:"},
    {"id not text",
     base_gk,
     [](json& c) { c["options"][0]["id"] = 5; },
     "options[0].id: expected a string, got a number"},
    {"duplicate id",
     base_gk,
     [](json& c) { c["options"][1]["id"] = "A"; },
     "options[1].id: duplicate id \"A\""},
    {"option type unknown",
     base_gk,
     [](json& c) { c["options"][0]["type"] = "straddle"; },
     "options[0].type: unknown option type \"straddle\""},
    {"option type capitalised",
     base_gk,
     [](json& c) { c["options"][0]["type"] = "Call"; },
     "options[0].type: unknown option type"},
    {"expiry zero",
     base_gk,
     [](json& c) { c["options"][0]["expiry_years"] = 0.0; },
     "options[0].expiry_years: must be greater than 0"},
    {"expiry negative",
     base_gk,
     [](json& c) { c["options"][1]["expiry_years"] = -1.0; },
     "options[1].expiry_years: must be greater than 0"},
    {"expiry beyond the limit",
     base_gk,
     [](json& c) { c["options"][0]["expiry_years"] = 500.0; },
     "options[0].expiry_years:"},
    {"expiry in days by mistake",
     base_gk,
     [](json& c) { c["options"][0]["expiry_years"] = "30d"; },
     "options[0].expiry_years: expected a number, got a string"},
    {"strike negative",
     base_gk,
     [](json& c) { c["options"][1]["strike"] = -1.1; },
     "options[1].strike:"},
    {"strike zero", base_gk, [](json& c) { c["options"][0]["strike"] = 0; }, "options[0].strike:"},
    {"strike missing",
     base_gk,
     [](json& c) { c["options"][0].erase("strike"); },
     "options[0].strike: missing required field"},
    {"notional zero",
     base_gk,
     [](json& c) { c["options"][0]["notional"] = 0; },
     "options[0].notional:"},
    {"notional negative",
     base_gk,
     [](json& c) { c["options"][0]["notional"] = -5; },
     "options[0].notional:"},
    {"notional as text",
     base_gk,
     [](json& c) { c["options"][0]["notional"] = "1m"; },
     "options[0].notional: expected a number, got a string"},
    {"unknown option key",
     base_gk,
     [](json& c) { c["options"][0]["quantity"] = 1; },
     "options[0].quantity: unknown field"},
};

class RejectTest : public testing::TestWithParam<RejectCase> {};

TEST_P(RejectTest, IsRejectedAndTheMessageNamesTheField) {
  const RejectCase& test_case = GetParam();
  json config = test_case.base();
  test_case.damage(config);
  const auto result = parse_price_config(config.dump());
  ASSERT_FALSE(result.has_value()) << test_case.name;
  EXPECT_EQ(result.error().code, ErrorCode::invalid_argument) << test_case.name;
  EXPECT_NE(result.error().message.find(test_case.expected), std::string::npos)
      << test_case.name << ": expected \"" << test_case.expected << "\" in \""
      << result.error().message << "\"";
}

INSTANTIATE_TEST_SUITE_P(Rules,
                         RejectTest,
                         testing::ValuesIn(kRejectCases),
                         [](const testing::TestParamInfo<RejectCase>& param) {
                           std::string name = param.param.name;
                           for (char& c : name) {
                             if (!std::isalnum(static_cast<unsigned char>(c))) {
                               c = '_';
                             }
                           }
                           return name + "_" + std::to_string(param.index);
                         });

TEST(PriceConfigSyntaxTest, MalformedDocumentsAreParseErrors) {
  struct Case {
    std::string text;
    std::string expected;
  };
  const Case cases[] = {
      {"", "not valid JSON"},
      {"{", "not valid JSON"},
      {"{} trailing", "not valid JSON"},
      {R"({"format_version": NaN})", "not valid JSON"},
      {R"({"format_version": 1,})", "not valid JSON"},
      {"[]", "the top level must be an object, not an array"},
      {"3", "the top level must be an object, not a number"},
      {"\"text\"", "the top level must be an object, not a string"},
      {"null", "the top level must be an object, not null"},
  };
  for (const Case& test_case : cases) {
    const auto result = parse_price_config(test_case.text);
    ASSERT_FALSE(result.has_value()) << test_case.text;
    EXPECT_EQ(result.error().code, ErrorCode::parse_error) << test_case.text;
    EXPECT_NE(result.error().message.find(test_case.expected), std::string::npos)
        << test_case.text << " -> " << result.error().message;
  }
}

TEST(PriceConfigSyntaxTest, RepeatedKeysAreRejectedNotSilentlyResolved) {
  // nlohmann::json alone would keep the second value. Here a validation tool must refuse.
  const std::string text = R"({
    "format_version": 1,
    "market": { "spot": 1.10, "domestic_rate": 0.03, "foreign_rate": 0.01 },
    "model": { "type": "garman_kohlhagen", "volatility": 0.08, "volatility": 0.50 },
    "options": [ { "id": "A", "type": "call", "strike": 1.1, "expiry_years": 1 } ]
  })";
  const auto result = parse_price_config(text);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::parse_error);
  EXPECT_NE(result.error().message.find("duplicate key \"volatility\""), std::string::npos)
      << result.error().message;
}

TEST(PriceConfigSyntaxTest, ExcessiveNestingIsRejected) {
  const std::string deep = std::string(5000, '[') + std::string(5000, ']');
  const auto arrays = parse_price_config(deep);
  ASSERT_FALSE(arrays.has_value());
  EXPECT_NE(arrays.error().message.find("nested more than 16 levels"), std::string::npos);

  std::string objects;
  for (int i = 0; i < 100; ++i) {
    objects += "{\"a\":";
  }
  objects += "1";
  objects += std::string(100, '}');
  const auto nested = parse_price_config(objects);
  ASSERT_FALSE(nested.has_value());
  EXPECT_NE(nested.error().message.find("nested more than 16 levels"), std::string::npos);
}

TEST(PriceConfigSyntaxTest, TooManyOptionsAreRejected) {
  json config = base_gk();
  json options = json::array();
  for (std::size_t i = 0; i <= fxval::app::max_options; ++i) {
    options.push_back({{"id", "o" + std::to_string(i)},
                       {"type", "call"},
                       {"strike", 1.1},
                       {"expiry_years", 1.0}});
  }
  config["options"] = std::move(options);
  const auto result = parse_price_config(config.dump());
  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().message.find("options: too many options"), std::string::npos)
      << result.error().message;
}

TEST(PriceConfigAcceptTest, GarmanKohlhagenValuesAreConverted) {
  const auto result = parse_price_config(base_gk().dump());
  ASSERT_TRUE(result.has_value()) << result.error().message;
  const auto& config = result.value();
  EXPECT_EQ(fxval::app::model_name(config.model), "garman_kohlhagen");
  EXPECT_EQ(config.market.spot.value(), 1.10);
  EXPECT_EQ(config.market.domestic.value(), 0.03);
  EXPECT_EQ(config.market.foreign.value(), 0.01);
  ASSERT_TRUE(std::holds_alternative<GarmanKohlhagenSpec>(config.model));
  EXPECT_EQ(std::get<GarmanKohlhagenSpec>(config.model).volatility.value(), 0.08);
  ASSERT_EQ(config.options.size(), 2U);
  EXPECT_EQ(config.options[0].id, "A");
  EXPECT_EQ(config.options[0].type, fxval::pricing::OptionType::call);
  EXPECT_EQ(config.options[0].notional.value(), 1'000'000.0);
  EXPECT_EQ(config.options[1].type, fxval::pricing::OptionType::put);
  EXPECT_EQ(config.options[1].notional.value(), 1.0);  // default: one unit of foreign currency
}

TEST(PriceConfigAcceptTest, HestonSemiAnalyticAndMonteCarloValuesAreConverted) {
  const auto semi = parse_price_config(base_heston().dump());
  ASSERT_TRUE(semi.has_value()) << semi.error().message;
  ASSERT_TRUE(std::holds_alternative<HestonSemiAnalyticSpec>(semi.value().model));
  const auto& params = std::get<HestonSemiAnalyticSpec>(semi.value().model).params;
  EXPECT_EQ(params.v0(), 0.04);
  EXPECT_EQ(params.kappa(), 2.0);
  EXPECT_EQ(params.theta(), 0.04);
  EXPECT_EQ(params.xi(), 0.3);
  EXPECT_EQ(params.rho(), -0.5);

  const auto mc = parse_price_config(base_heston_mc().dump());
  ASSERT_TRUE(mc.has_value()) << mc.error().message;
  ASSERT_TRUE(std::holds_alternative<HestonMonteCarloSpec>(mc.value().model));
  const auto& spec = std::get<HestonMonteCarloSpec>(mc.value().model);
  EXPECT_EQ(spec.scheme, McScheme::qe);
  EXPECT_EQ(spec.settings.config.seed, 7U);
  EXPECT_EQ(spec.settings.config.paths, 1000U);
  EXPECT_EQ(spec.settings.config.steps, 8U);
  EXPECT_EQ(spec.settings.config.threads, 2U);
  EXPECT_TRUE(spec.settings.reduction.antithetic);
  EXPECT_TRUE(spec.settings.reduction.forward_control);

  const auto gbm = parse_price_config(base_gbm_mc().dump());
  ASSERT_TRUE(gbm.has_value()) << gbm.error().message;
  ASSERT_TRUE(std::holds_alternative<GbmMonteCarloSpec>(gbm.value().model));
  EXPECT_EQ(std::get<GbmMonteCarloSpec>(gbm.value().model).settings.config.threads, 1U);  // default
}

TEST(PriceConfigAcceptTest, EulerAcceptsZeroVolOfVolButQeDoesNot) {
  json euler = base_heston_mc();
  euler["model"]["scheme"] = "euler";
  euler["model"]["xi"] = 0.0;
  EXPECT_TRUE(parse_price_config(euler.dump()).has_value());
}

TEST(PriceConfigAcceptTest, LargeSeedsKeepAllSixtyFourBits) {
  json config = base_heston_mc();
  config["model"]["monte_carlo"]["seed"] = 18'446'744'073'709'551'615ULL;
  const auto result = parse_price_config(config.dump());
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(std::get<HestonMonteCarloSpec>(result.value().model).settings.config.seed,
            18'446'744'073'709'551'615ULL);
}

TEST(PriceConfigAcceptTest, IdsMayUseTheDocumentedCharacters) {
  json config = base_gk();
  config["options"][0]["id"] = "EURUSD_1Y-ATM.call:v2";
  EXPECT_TRUE(parse_price_config(config.dump()).has_value());
  config["options"][0]["id"] = std::string(64, 'z');
  EXPECT_TRUE(parse_price_config(config.dump()).has_value());
}

TEST(PriceConfigAcceptTest, ShippedExamplesParse) {
  for (const char* name :
       {"gk_vanillas.json", "heston_semi_analytic_smile.json", "heston_mc_qe.json"}) {
    const std::string text = fxval::apptest::example_text(name);
    ASSERT_FALSE(text.empty()) << name;
    const auto result = parse_price_config(text);
    EXPECT_TRUE(result.has_value()) << name << ": " << (result ? "" : result.error().message);
  }
}

// ---- Robustness: damaged documents ------------------------------------------------------------
// A stand-in for fuzzing that runs in every build (libFuzzer targets are planned for weeks 11-14).
// Take valid documents, damage them at random (change, delete, insert, truncate, swap characters)
// and require: no exception escapes, every rejection has a message, and every ACCEPTED document
// still satisfies the invariants the pricer relies on.
TEST(PriceConfigRobustnessTest, RandomlyDamagedDocumentsNeverEscapeAndAcceptedOnesAreSound) {
  const std::string documents[] = {
      base_gk().dump(2), base_heston().dump(2), base_heston_mc().dump(2), base_gbm_mc().dump(2)};
  const std::string alphabet = "{}[]:,\"0123456789.-+eE tfnul\\/_abcxyzAZ\n";
  std::mt19937_64 rng(20261017);
  std::size_t accepted = 0;
  std::size_t rejected = 0;
  constexpr int iterations_per_document = 6000;
  for (const std::string& original : documents) {
    for (int i = 0; i < iterations_per_document; ++i) {
      std::string text = original;
      const int edits = 1 + static_cast<int>(rng() % 3);
      for (int e = 0; e < edits && !text.empty(); ++e) {
        const std::size_t at = rng() % text.size();
        switch (rng() % 5) {
        case 0:
          text[at] = alphabet[rng() % alphabet.size()];
          break;
        case 1:
          text.erase(at, 1);
          break;
        case 2:
          text.insert(at, 1, alphabet[rng() % alphabet.size()]);
          break;
        case 3:
          text.resize(at);
          break;
        default:
          std::swap(text[at], text[rng() % text.size()]);
          break;
        }
      }
      const auto result = parse_price_config(text);  // must not throw (a throw fails the test)
      if (!result) {
        ++rejected;
        ASSERT_FALSE(result.error().message.empty()) << text;
        continue;
      }
      ++accepted;
      const auto& config = result.value();
      ASSERT_FALSE(config.options.empty());
      ASSERT_LE(config.options.size(), fxval::app::max_options);
      for (const auto& option : config.options) {
        ASSERT_GT(option.strike.value(), 0.0) << text;
        ASSERT_GT(option.expiry.value(), 0.0) << text;
        ASSERT_GT(option.notional.value(), 0.0) << text;
        ASSERT_FALSE(option.id.empty()) << text;
      }
      ASSERT_GT(config.market.spot.value(), 0.0) << text;
    }
  }
  RecordProperty("accepted_documents", static_cast<int>(accepted));
  RecordProperty("rejected_documents", static_cast<int>(rejected));
  // Both outcomes must occur, otherwise the damage is too weak or too strong to test anything.
  EXPECT_GT(accepted, 100U);
  EXPECT_GT(rejected, 10'000U);
}

}  // namespace
