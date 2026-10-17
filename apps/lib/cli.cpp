#include "cli.hpp"

#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>

#include "build_info.hpp"
#include "fxval/version.hpp"
#include "logger.hpp"
#include "price_config.hpp"
#include "price_runner.hpp"
#include "report.hpp"
#include "run_metadata.hpp"

namespace fxval::app {

namespace {

constexpr std::int64_t max_epoch_seconds = 253'402'300'799;  // 9999-12-31T23:59:59Z

constexpr std::string_view usage_text = R"(usage: fxval-price --config FILE [options]

Prices European FX options from a JSON configuration and writes a report.

options:
  --config FILE      configuration (JSON, format_version 1); "-" reads standard input
  --output FILE      write the report to FILE instead of standard output
  --format json|csv  report format (default: json)
  -v, --verbose      log details to standard error
  -q, --quiet        log nothing but usage errors
  --version          print the version and exit
  --help             print this text and exit

exit codes:
  0  success
  1  bad command line
  2  invalid configuration (nothing priced)
  3  report written, but at least one option failed
  4  file could not be read or written
  5  internal error

environment:
  SOURCE_DATE_EPOCH  seconds since 1970-01-01 UTC to record as the timestamp (default: now)

The configuration format is described in docs/cli.md.
)";

struct Arguments {
  std::string config_path;
  std::string output_path;
  std::string format = "json";
  LogLevel level = LogLevel::info;
  bool help = false;
  bool version = false;
};

// Returns an empty string on success, otherwise a description of the problem.
std::string parse_arguments(const std::vector<std::string>& args, Arguments& parsed) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    std::string name = args[i];
    std::string value;
    bool has_inline_value = false;
    if (name.starts_with("--")) {
      const std::size_t equals = name.find('=');
      if (equals != std::string::npos) {
        value = name.substr(equals + 1);
        name = name.substr(0, equals);
        has_inline_value = true;
      }
    }
    const auto take_value = [&](std::string& target) -> std::string {
      if (has_inline_value) {
        target = value;
      } else if (i + 1 < args.size()) {
        target = args[++i];
      } else {
        return "option " + name + " needs a value";
      }
      return {};
    };
    std::string problem;
    if (name == "--config") {
      problem = take_value(parsed.config_path);
    } else if (name == "--output") {
      problem = take_value(parsed.output_path);
    } else if (name == "--format") {
      problem = take_value(parsed.format);
    } else if ((name == "--verbose" || name == "-v") && !has_inline_value) {
      parsed.level = LogLevel::debug;
    } else if ((name == "--quiet" || name == "-q") && !has_inline_value) {
      parsed.level = LogLevel::quiet;
    } else if ((name == "--help" || name == "-h") && !has_inline_value) {
      parsed.help = true;
    } else if (name == "--version" && !has_inline_value) {
      parsed.version = true;
    } else {
      problem = "unknown or malformed option '" + args[i] + "'";
    }
    if (!problem.empty()) {
      return problem;
    }
  }
  if (parsed.help || parsed.version) {
    return {};
  }
  if (parsed.config_path.empty()) {
    return "missing required option --config";
  }
  if (parsed.format != "json" && parsed.format != "csv") {
    return "unknown format '" + parsed.format + "' (allowed: json, csv)";
  }
  return {};
}

// Reads at most max_config_bytes + 1 bytes so an oversized input is detected without loading it.
bool read_stream(std::istream& in, std::string& bytes) {
  bytes.clear();
  char buffer[65536];
  while (in && bytes.size() <= max_config_bytes) {
    in.read(buffer, sizeof(buffer));
    bytes.append(buffer, static_cast<std::size_t>(in.gcount()));
  }
  return !in.bad();
}

// Write to a temporary file next to the target, then rename: a failed run never leaves a
// half-written report under the final name.
bool write_file(const std::string& path, const std::string& content, std::string& problem) {
  const std::string temporary = path + ".tmp";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) {
      problem = "cannot open '" + temporary + "' for writing";
      return false;
    }
    file << content;
    file.flush();
    if (!file) {
      problem = "writing '" + temporary + "' failed";
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      return false;
    }
  }
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error) {
    problem = "cannot move the report to '" + path + "': " + error.message();
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return false;
  }
  return true;
}

int code(ExitCode value) noexcept {
  return static_cast<int>(value);
}

int run(const std::vector<std::string>& args,
        std::istream& standard_in,
        std::ostream& standard_out,
        std::ostream& standard_err,
        const CliEnvironment& environment) {
  Arguments arguments;
  const std::string usage_problem = parse_arguments(args, arguments);
  if (!usage_problem.empty()) {
    standard_err << "fxval-price: error: " << usage_problem << "\n\n" << usage_text;
    return code(ExitCode::usage);
  }
  if (arguments.help) {
    standard_out << usage_text;
    return code(ExitCode::ok);
  }
  if (arguments.version) {
    standard_out << "fxval-price " << fxval::version_string << " (git " << build_info::git_commit
                 << ")\n";
    return code(ExitCode::ok);
  }

  const Logger log(standard_err, arguments.level);

  std::string bytes;
  if (arguments.config_path == "-") {
    if (!read_stream(standard_in, bytes)) {
      log.error("cannot read the configuration from standard input");
      return code(ExitCode::io_error);
    }
  } else {
    std::ifstream file(arguments.config_path, std::ios::binary);
    if (!file) {
      log.error("cannot open configuration file '" + arguments.config_path + "'");
      return code(ExitCode::io_error);
    }
    if (!read_stream(file, bytes)) {
      log.error("cannot read configuration file '" + arguments.config_path + "'");
      return code(ExitCode::io_error);
    }
  }
  if (bytes.size() > max_config_bytes) {
    log.error("configuration is larger than " + std::to_string(max_config_bytes >> 20U) + " MiB");
    return code(ExitCode::invalid_config);
  }

  const Result<PriceConfig> config = parse_price_config(bytes);
  if (!config) {
    log.error("invalid configuration (" + std::string(to_string(config.error().code)) +
              "): " + config.error().message);
    return code(ExitCode::invalid_config);
  }
  log.info("configuration ok: " + std::to_string(config.value().options.size()) +
           " option(s), model " + std::string(model_name(config.value().model)));

  const PriceReport report = environment.pricer ? run_price(config.value(), environment.pricer)
                                                : run_price(config.value());
  for (const OptionResult& option : report.options) {
    if (option.ok) {
      log.debug("option " + option.id + " priced");
    } else {
      log.error("option " + option.id + ": " + option.error);
    }
  }

  const std::int64_t epoch =
      environment.epoch_seconds.value_or(std::chrono::duration_cast<std::chrono::seconds>(
                                             std::chrono::system_clock::now().time_since_epoch())
                                             .count());
  const RunMetadata metadata = make_run_metadata(bytes, epoch);
  const std::string text =
      arguments.format == "csv"
          ? report_to_csv(metadata, arguments.config_path, config.value(), report)
          : report_to_json(metadata, arguments.config_path, config.value(), report);

  if (arguments.output_path.empty()) {
    standard_out << text;
    standard_out.flush();
    if (!standard_out) {
      log.error("writing the report to standard output failed");
      return code(ExitCode::io_error);
    }
  } else {
    std::string problem;
    if (!write_file(arguments.output_path, text, problem)) {
      log.error(problem);
      return code(ExitCode::io_error);
    }
    log.info("report written to " + arguments.output_path);
  }

  if (report.failed() > 0) {
    log.error(std::to_string(report.failed()) + " of " + std::to_string(report.options.size()) +
              " option(s) failed");
    return code(ExitCode::pricing_failed);
  }
  return code(ExitCode::ok);
}

}  // namespace

std::optional<std::int64_t> parse_epoch_seconds(const std::string& text) {
  if (text.empty() || text.size() > 12) {
    return std::nullopt;
  }
  std::int64_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    value = value * 10 + (c - '0');
  }
  if (value > max_epoch_seconds) {
    return std::nullopt;
  }
  return value;
}

int run_price_cli(const std::vector<std::string>& args,
                  std::istream& standard_in,
                  std::ostream& standard_out,
                  std::ostream& standard_err,
                  const CliEnvironment& environment) {
  try {
    return run(args, standard_in, standard_out, standard_err, environment);
  } catch (const std::exception& error) {
    standard_err << "fxval-price: error: internal error: " << error.what() << '\n';
    return code(ExitCode::internal_error);
  } catch (...) {
    standard_err << "fxval-price: error: internal error (unknown exception)\n";
    return code(ExitCode::internal_error);
  }
}

}  // namespace fxval::app
