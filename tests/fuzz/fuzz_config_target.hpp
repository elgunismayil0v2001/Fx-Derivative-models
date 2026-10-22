#pragma once

// Fuzz target for the `fxval-price` configuration parser (needs the apps library and
// nlohmann_json). Invariants: it never throws; a refusal carries a message; the same input gives
// the same outcome; an accepted document satisfies the invariants the pricer relies on (positive
// spot, strikes, expiries and notionals, non-empty identifiers, at least one option and no more
// than max_options).

#include <cstdint>
#include <string>
#include <string_view>

#include "price_config.hpp"

namespace fxval::fuzz {

inline std::string check_price_config(const std::uint8_t* data, std::size_t size) {
  const std::string_view text(reinterpret_cast<const char*>(data), size);
  const auto first = app::parse_price_config(text);
  const auto second = app::parse_price_config(text);
  if (first.has_value() != second.has_value()) {
    return "the parser is not deterministic (success differs)";
  }
  if (!first) {
    if (first.error().message.empty()) {
      return "a refusal has an empty message";
    }
    if (!(first.error() == second.error())) {
      return "the parser is not deterministic (error differs)";
    }
    return {};
  }
  const auto& config = first.value();
  if (config.options.empty() || config.options.size() > app::max_options) {
    return "an accepted document has no options or too many";
  }
  if (!(config.market.spot.value() > 0.0)) {
    return "an accepted document has a non-positive spot";
  }
  for (const auto& option : config.options) {
    if (!(option.strike.value() > 0.0) || !(option.expiry.value() > 0.0) ||
        !(option.notional.value() > 0.0) || option.id.empty()) {
      return "an accepted option violates the invariants";
    }
  }
  if (second.value().options.size() != config.options.size()) {
    return "the parser is not deterministic (option count differs)";
  }
  return {};
}

}  // namespace fxval::fuzz
