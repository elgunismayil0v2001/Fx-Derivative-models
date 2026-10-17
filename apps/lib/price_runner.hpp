#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "fxval/pricing/garman_kohlhagen.hpp"
#include "price_config.hpp"

namespace fxval::app {

// The outcome for one option. A failure of one option does not stop the others: the report holds
// every option, and the program's exit code says whether any failed.
struct OptionResult {
  std::string id;
  pricing::OptionType type = pricing::OptionType::call;
  double strike = 0.0;
  double expiry_years = 0.0;
  double notional = 1.0;

  bool ok = false;
  std::string error;  // set when !ok

  double price = 0.0;                    // domestic currency per one unit of foreign notional
  double present_value = 0.0;            // price x notional, in domestic currency
  std::optional<double> standard_error;  // Monte Carlo only, per unit of foreign notional
  std::optional<pricing::GarmanKohlhagenGreeks> greeks;  // Garman-Kohlhagen only
  std::string note;  // non-fatal remark, e.g. why Greeks are missing
};

// What a pricer returns for one option.
struct PricedOption {
  double price;                                          // per one unit of foreign notional
  std::optional<double> standard_error;                  // Monte Carlo only
  std::optional<pricing::GarmanKohlhagenGreeks> greeks;  // Garman-Kohlhagen only
  std::string note;
};

// Prices one option with the model in the configuration (the real pricer).
[[nodiscard]] Result<PricedOption> price_option(const PriceConfig& config,
                                                const OptionSpec& option);

// A replaceable pricer. The library never fails on a valid configuration for any input we could
// find, so tests inject a pricer that fails or throws to exercise the error paths (per-option
// failure, exit code 3, exit code 5). Production code always uses price_option.
using OptionPricer = std::function<Result<PricedOption>(const PriceConfig&, const OptionSpec&)>;

struct PriceReport {
  std::vector<OptionResult> options;

  [[nodiscard]] std::size_t failed() const noexcept {
    std::size_t count = 0;
    for (const OptionResult& option : options) {
      count += option.ok ? 0U : 1U;
    }
    return count;
  }
};

// Prices every option with the configured model. Library errors (numerical_failure,
// not_converged, ...) are recorded per option; std::bad_alloc is the only exception that can
// escape (a replacement pricer may throw anything; the caller handles it). Monte Carlo options all
// use the configured seed, so they share random numbers (common random numbers): their differences
// are smoother than independent runs would give.
[[nodiscard]] PriceReport run_price(const PriceConfig& config,
                                    const OptionPricer& pricer = price_option);

}  // namespace fxval::app
