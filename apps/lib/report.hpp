#pragma once

#include <string>
#include <string_view>

#include "price_config.hpp"
#include "price_runner.hpp"
#include "run_metadata.hpp"

namespace fxval::app {

inline constexpr int report_format_version = 1;

// JSON report: format_version, metadata (run metadata), inputs (the validated configuration echoed
// back in normalised form), summary, results. Numbers are written with the shortest text that
// reads back to the same double, so nothing is lost to formatting. Field order is fixed.
// `config_file` is the name the user gave (recorded as given, for the reader's reference).
[[nodiscard]] std::string report_to_json(const RunMetadata& metadata,
                                         std::string_view config_file,
                                         const PriceConfig& config,
                                         const PriceReport& report);

// CSV report: metadata as `# key: value` comment lines, a header row, one row per option. Empty
// cells mean "not applicable". Option ids are restricted by the configuration parser to a safe
// character set; free text (error messages) is quoted.
[[nodiscard]] std::string report_to_csv(const RunMetadata& metadata,
                                        std::string_view config_file,
                                        const PriceConfig& config,
                                        const PriceReport& report);

// RFC 4180 quoting: a field containing a comma, a double quote or a line break is wrapped in
// double quotes, with inner quotes doubled. Exposed for tests.
[[nodiscard]] std::string csv_field(std::string_view text);

}  // namespace fxval::app
