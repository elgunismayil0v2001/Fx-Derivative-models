#pragma once

// Shared helpers for the tests of the command-line layer (apps/lib).

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "price_config.hpp"
#include "support/mc_test_support.hpp"

namespace fxval::apptest {

inline std::string read_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream text;
  text << file.rdbuf();
  return text.str();
}

inline std::string example_text(const char* name) {
  return read_file(std::string(FXVAL_CONFIG_EXAMPLES_DIR) + "/" + name);
}

inline app::PriceConfig must_parse(const std::string& text) {
  auto result = app::parse_price_config(text);
  if (!result) {
    ADD_FAILURE() << "configuration rejected: " << result.error().message;
    std::abort();
  }
  return std::move(result).value();
}

inline app::PriceConfig must_parse_example(const char* name) {
  return must_parse(example_text(name));
}

// A Heston Monte Carlo configuration with a path count suited to the build type.
inline nlohmann::json heston_mc_json(const char* scheme,
                                     std::size_t paths,
                                     unsigned threads,
                                     bool antithetic,
                                     bool control,
                                     std::uint64_t seed = 20261017) {
  return nlohmann::json{
      {"format_version", 1},
      {"market", {{"spot", 1.10}, {"domestic_rate", 0.03}, {"foreign_rate", 0.01}}},
      {"model",
       {{"type", "heston_monte_carlo"},
        {"v0", 0.04},
        {"kappa", 2.0},
        {"theta", 0.04},
        {"xi", 0.3},
        {"rho", -0.5},
        {"scheme", scheme},
        {"monte_carlo",
         {{"seed", seed},
          {"paths", paths},
          {"steps", 16},
          {"threads", threads},
          {"antithetic", antithetic},
          {"control_variate", control}}}}},
      {"options",
       nlohmann::json::array(
           {{{"id", "K100"}, {"type", "call"}, {"strike", 1.00}, {"expiry_years", 1.0}},
            {{"id", "K110"}, {"type", "call"}, {"strike", 1.10}, {"expiry_years", 1.0}},
            {{"id", "K110P"}, {"type", "put"}, {"strike", 1.10}, {"expiry_years", 0.5}},
            {{"id", "K120"}, {"type", "call"}, {"strike", 1.20}, {"expiry_years", 1.0}}})}};
}

}  // namespace fxval::apptest
