#pragma once

#include <string>

#include "portfolio_analysis.hpp"

namespace fxval::example {

// The whole report as JSON (no external library; numbers with 12 significant digits, a non-finite
// number is written as null so the output is always valid JSON).
[[nodiscard]] std::string to_json(const Report& report);

// A plain-text summary for the terminal.
[[nodiscard]] std::string to_text(const Report& report);

// One self-contained HTML page (inline CSS and JavaScript, no network access) that draws the
// report from the embedded JSON.
[[nodiscard]] std::string to_html(const Report& report);

}  // namespace fxval::example
