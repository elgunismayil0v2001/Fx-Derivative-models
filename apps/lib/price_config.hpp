#pragma once

#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "fxval/core/error.hpp"
#include "fxval/core/units.hpp"
#include "fxval/mc/engine.hpp"
#include "fxval/mc/vanilla.hpp"
#include "fxval/models/heston.hpp"
#include "fxval/pricing/garman_kohlhagen.hpp"

namespace fxval::app {

// The configuration of the `fxval-price` program, validated and converted to the library's typed
// units. Format and rules: docs/cli.md. The format is versioned (format_version); version 1 is
// the only one that exists.

inline constexpr int price_config_format_version = 1;
inline constexpr std::size_t max_options = 100'000;

enum class McScheme { euler, qe };

struct McSettings {
  mc::McConfig config;
  mc::McReduction reduction;
};

struct GarmanKohlhagenSpec {
  Volatility volatility;
};
struct HestonSemiAnalyticSpec {
  models::HestonParams params;
};
struct GbmMonteCarloSpec {
  Volatility volatility;
  McSettings settings;
};
struct HestonMonteCarloSpec {
  models::HestonParams params;
  McScheme scheme;
  McSettings settings;
};
using ModelSpec = std::
    variant<GarmanKohlhagenSpec, HestonSemiAnalyticSpec, GbmMonteCarloSpec, HestonMonteCarloSpec>;

struct MarketSpec {
  Spot spot;
  Rate domestic;
  Rate foreign;
};

struct OptionSpec {
  std::string id;
  pricing::OptionType type;
  Strike strike;
  Years expiry;       // year fraction, ACT/365 fixed by convention
  Notional notional;  // in units of foreign currency
};

struct PriceConfig {
  MarketSpec market;
  ModelSpec model;
  std::vector<OptionSpec> options;
};

// The model's name as it appears in the configuration and in the output.
[[nodiscard]] std::string_view model_name(const ModelSpec& model) noexcept;

// Parses and validates a configuration. Strict by design: an unknown key, a repeated key, a
// missing field, a wrong type or a value outside its domain is an error, never silently ignored
// or defaulted (a mistyped "volatilty" must not become a default volatility). Every message starts
// with the path of the offending field, for example `options[2].strike: strike must be ...`.
// Errors: parse_error (not JSON, wrong top-level shape, too deeply nested), invalid_argument
// (everything else). Never throws.
[[nodiscard]] Result<PriceConfig> parse_price_config(std::string_view input);

}  // namespace fxval::app
