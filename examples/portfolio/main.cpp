// fxval-portfolio: value, Greeks, VaR and CVA of an example FX book with the library's models.
//
//   fxval-portfolio [--html report.html] [--json report.json] [--returns daily_log_returns.csv]
//                   [--level 0.99] [--paths 20000] [--seed 20261023] [--no-heston]
//
// Without --returns a synthetic GARCH history is used. The terminal summary is always printed.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "portfolio_analysis.hpp"
#include "report_writers.hpp"

namespace {

bool write_file(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary);
  out << content;
  return static_cast<bool>(out);
}

void usage(const char* program) {
  std::cerr << "usage: " << program
            << " [--html FILE] [--json FILE] [--returns FILE] [--level L] [--paths N] [--seed N]"
               " [--no-heston]\n";
}

}  // namespace

int main(int argc, char** argv) {
  using namespace fxval::example;
  Scenario scenario = default_scenario();
  std::string html_path;
  std::string json_path;
  std::string returns_path;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (arg == "--no-heston") {
      scenario.run_heston = false;
    } else if (arg == "--html" && has_value) {
      html_path = argv[++i];
    } else if (arg == "--json" && has_value) {
      json_path = argv[++i];
    } else if (arg == "--returns" && has_value) {
      returns_path = argv[++i];
    } else if (arg == "--level" && has_value) {
      scenario.var_level = std::atof(argv[++i]);
    } else if (arg == "--paths" && has_value) {
      scenario.exposure_paths = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
    } else if (arg == "--seed" && has_value) {
      scenario.seed = std::strtoull(argv[++i], nullptr, 10);
    } else {
      usage(argv[0]);
      return 2;
    }
  }
  if (!returns_path.empty()) {
    std::ifstream in(returns_path);
    if (!in) {
      std::cerr << "cannot open " << returns_path << '\n';
      return 2;
    }
    std::ostringstream text;
    text << in.rdbuf();
    auto parsed = parse_returns(text.str());
    if (!parsed) {
      std::cerr << returns_path << ": " << parsed.error().message << '\n';
      return 2;
    }
    scenario.returns = std::move(parsed).value();
  }

  const auto report = analyse(scenario);
  if (!report) {
    std::cerr << "analysis failed: " << fxval::to_string(report.error().code) << ": "
              << report.error().message << '\n';
    return 1;
  }
  std::cout << to_text(report.value());
  if (!json_path.empty() && !write_file(json_path, to_json(report.value()))) {
    std::cerr << "cannot write " << json_path << '\n';
    return 1;
  }
  if (!html_path.empty() && !write_file(html_path, to_html(report.value()))) {
    std::cerr << "cannot write " << html_path << '\n';
    return 1;
  }
  return 0;
}
