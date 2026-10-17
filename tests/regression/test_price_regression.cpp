// Regression tests: the shipped example configurations are priced and the results compared with
// frozen outputs in tests/regression/expected/. A change in any number must be deliberate.
//
// The frozen values are NOT trusted on their own: python/tests/test_price_examples.py checks them
// against independent implementations (SciPy for Garman-Kohlhagen, the textbook Heston integral for
// the semi-analytic prices, and the semi-analytic price for the Monte Carlo example, within its
// standard errors). The check here only says "nothing changed".
//
// To refresh the frozen files after an intended change (review the diff!):
//   FXVAL_UPDATE_GOLDEN=1 ./build/release/tests/fxval_app_tests --gtest_filter='Regression*'
//
// Numbers are compared with a relative tolerance of 1e-9, not bit for bit: libm functions
// (log, cos, exp) may differ in the last digits between platforms, and the Monte Carlo example
// uses them.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "../app/app_test_support.hpp"
#include "price_runner.hpp"
#include "report.hpp"
#include "run_metadata.hpp"

namespace {

using nlohmann::json;

constexpr double kRelativeTolerance = 1e-9;

// Returns an empty string if equal, otherwise a description of the first difference.
std::string difference(const json& actual, const json& expected, const std::string& path) {
  if (actual.is_number() && expected.is_number()) {
    const double a = actual.get<double>();
    const double e = expected.get<double>();
    const double scale = std::max(std::abs(a), std::abs(e));
    if (std::abs(a - e) <= kRelativeTolerance * scale || a == e) {
      return {};
    }
    return path + ": " + std::to_string(a) + " != " + std::to_string(e);
  }
  if (actual.type() != expected.type()) {
    return path + ": different types";
  }
  if (actual.is_object()) {
    if (actual.size() != expected.size()) {
      return path + ": different number of fields";
    }
    for (const auto& item : expected.items()) {
      if (!actual.contains(item.key())) {
        return path + "." + item.key() + ": missing";
      }
      const std::string inner =
          difference(actual.at(item.key()), item.value(), path + "." + item.key());
      if (!inner.empty()) {
        return inner;
      }
    }
    return {};
  }
  if (actual.is_array()) {
    if (actual.size() != expected.size()) {
      return path + ": different length";
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
      const std::string inner =
          difference(actual[i], expected[i], path + "[" + std::to_string(i) + "]");
      if (!inner.empty()) {
        return inner;
      }
    }
    return {};
  }
  return actual == expected ? std::string{}
                            : path + ": " + actual.dump() + " != " + expected.dump();
}

void check_example(const std::string& stem) {
  const std::string text = fxval::apptest::example_text((stem + ".json").c_str());
  const auto config = fxval::apptest::must_parse(text);
  const auto report = fxval::app::run_price(config);
  ASSERT_EQ(report.failed(), 0U);
  const json document = json::parse(fxval::app::report_to_json(
      fxval::app::make_run_metadata(text, 0), stem + ".json", config, report));
  const json results = document["results"];

  const std::string golden_path = std::string(FXVAL_REGRESSION_DIR) + "/" + stem + ".results.json";
  if (std::getenv("FXVAL_UPDATE_GOLDEN") != nullptr) {  // NOLINT(concurrency-mt-unsafe)
    std::ofstream out(golden_path, std::ios::binary | std::ios::trunc);
    out << results.dump(2) << "\n";
    GTEST_SKIP() << "FXVAL_UPDATE_GOLDEN is set: rewrote " << golden_path;
  }
  const std::string golden_text = fxval::apptest::read_file(golden_path);
  ASSERT_FALSE(golden_text.empty()) << "missing " << golden_path;
  const std::string problem = difference(results, json::parse(golden_text), "results");
  EXPECT_EQ(problem, "") << "output of " << stem
                         << " changed (see the comment at the top of this file)";
}

TEST(RegressionTest, GarmanKohlhagenVanillas) {
  check_example("gk_vanillas");
}

TEST(RegressionTest, HestonSemiAnalyticSmile) {
  check_example("heston_semi_analytic_smile");
}

TEST(RegressionTest, HestonMonteCarloQe) {
  check_example("heston_mc_qe");
}

// The comparison itself must be able to fail, otherwise a regression test proves nothing.
TEST(RegressionTest, TheComparisonDetectsAChangeInTheNinthSignificantDigit) {
  const json a = json::parse(R"({"price": 0.0462556228414579, "id": "x", "v": [1.0, 2.0]})");
  json b = a;
  EXPECT_EQ(difference(a, b, ""), "");
  b["price"] = 0.0462556228414579 * (1.0 + 1e-8);
  EXPECT_NE(difference(a, b, ""), "");
  b = a;
  b["price"] = 0.0462556228414579 * (1.0 + 1e-11);
  EXPECT_EQ(difference(a, b, ""), "");
  b = a;
  b["id"] = "y";
  EXPECT_NE(difference(a, b, ""), "");
  b = a;
  b["v"] = json::array({1.0});
  EXPECT_NE(difference(a, b, ""), "");
}

}  // namespace
