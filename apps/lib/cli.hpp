#pragma once

#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "price_runner.hpp"

namespace fxval::app {

// Exit codes of fxval-price (documented in docs/cli.md; scripts may rely on them).
enum class ExitCode : int {
  ok = 0,              // every option priced, report written
  usage = 1,           // bad command line
  invalid_config = 2,  // the configuration is not valid; nothing was priced
  pricing_failed = 3,  // report written, but at least one option could not be priced
  io_error = 4,        // a file could not be read or written
  internal_error = 5,  // unexpected exception: a bug, please report
};

// Everything the program takes from its environment, so tests can control it.
struct CliEnvironment {
  // Time to record in the report, seconds since the epoch. When empty, the system clock is used.
  // The program sets it from SOURCE_DATE_EPOCH (the reproducible-builds convention).
  std::optional<std::int64_t> epoch_seconds;
  // Replaces the real pricer when set. Only tests set it (see price_runner.hpp).
  OptionPricer pricer;
};

// Largest configuration the program will read.
inline constexpr std::size_t max_config_bytes = std::size_t{16} << 20U;

// Parses the text of SOURCE_DATE_EPOCH: digits only, value up to 253402300799 (year 9999).
[[nodiscard]] std::optional<std::int64_t> parse_epoch_seconds(const std::string& text);

// The whole program, with its streams injected. `args` excludes the program name. Never throws.
[[nodiscard]] int run_price_cli(const std::vector<std::string>& args,
                                std::istream& standard_in,
                                std::ostream& standard_out,
                                std::ostream& standard_err,
                                const CliEnvironment& environment);

}  // namespace fxval::app
