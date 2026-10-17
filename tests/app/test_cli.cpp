// Tests for the command-line program, called as a function with its streams injected: argument
// handling, every exit code, output to stdout and to a file, standard input, size limit,
// determinism, and the separation of report (stdout) from log (stderr).
// The real executable is tested separately by tests/integration/run_cli.cmake.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "app_test_support.hpp"
#include "cli.hpp"
#include "fxval/version.hpp"

namespace {

using fxval::app::CliEnvironment;
using fxval::app::ExitCode;
using fxval::app::parse_epoch_seconds;
using fxval::app::run_price_cli;
using nlohmann::json;

struct Outcome {
  int code;
  std::string out;
  std::string err;
};

Outcome run(const std::vector<std::string>& args,
            const std::string& input = {},
            const CliEnvironment& environment = {}) {
  std::istringstream in(input);
  std::ostringstream out;
  std::ostringstream err;
  const int code = run_price_cli(args, in, out, err, environment);
  return {code, out.str(), err.str()};
}

std::string example_path(const char* name) {
  return std::string(FXVAL_CONFIG_EXAMPLES_DIR) + "/" + name;
}

class TempDir {
public:
  TempDir() {
    static int counter = 0;
    path_ = std::filesystem::temp_directory_path() /
            ("fxval_cli_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + "_" +
             std::to_string(counter++));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  [[nodiscard]] std::string file(const std::string& name) const { return (path_ / name).string(); }
  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
  std::filesystem::path path_;
};

CliEnvironment fixed_clock() {
  CliEnvironment environment;
  environment.epoch_seconds = 1'700'000'000;
  return environment;
}

TEST(CliTest, HelpAndVersionExitZeroAndPrintToStdout) {
  const auto help = run({"--help"});
  EXPECT_EQ(help.code, 0);
  EXPECT_NE(help.out.find("usage: fxval-price"), std::string::npos);
  EXPECT_NE(help.out.find("exit codes:"), std::string::npos);
  EXPECT_TRUE(help.err.empty());
  EXPECT_EQ(run({"-h"}).code, 0);

  const auto version = run({"--version"});
  EXPECT_EQ(version.code, 0);
  EXPECT_NE(version.out.find(std::string(fxval::version_string)), std::string::npos);
}

TEST(CliTest, HelpTextDocumentsEveryExitCode) {
  const auto help = run({"--help"});
  for (const char* line : {"  0  success",
                           "  1  bad command line",
                           "  2  invalid configuration",
                           "  3  report written, but at least one option failed",
                           "  4  file could not be read or written",
                           "  5  internal error"}) {
    EXPECT_NE(help.out.find(line), std::string::npos) << line;
  }
}

TEST(CliTest, UsageErrorsExitOneAndSayWhy) {
  struct Case {
    std::vector<std::string> args;
    std::string expected;
  };
  const Case cases[] = {
      {{}, "missing required option --config"},
      {{"--format", "json"}, "missing required option --config"},
      {{"--config"}, "option --config needs a value"},
      {{"--config", "x.json", "--format", "xml"}, "unknown format 'xml'"},
      {{"--config", "x.json", "--frobnicate"}, "unknown or malformed option '--frobnicate'"},
      {{"--config", "x.json", "extra"}, "unknown or malformed option 'extra'"},
      {{"--verbose=1", "--config", "x.json"}, "unknown or malformed option '--verbose=1'"},
  };
  for (const Case& test_case : cases) {
    const auto outcome = run(test_case.args);
    EXPECT_EQ(outcome.code, static_cast<int>(ExitCode::usage)) << test_case.expected;
    EXPECT_NE(outcome.err.find(test_case.expected), std::string::npos) << outcome.err;
    EXPECT_NE(outcome.err.find("usage: fxval-price"), std::string::npos);
    EXPECT_TRUE(outcome.out.empty());
  }
}

TEST(CliTest, ReportGoesToStdoutAndIsValidJson) {
  const auto outcome = run({"--config", example_path("gk_vanillas.json")}, {}, fixed_clock());
  ASSERT_EQ(outcome.code, 0) << outcome.err;
  const json doc = json::parse(outcome.out);  // throws if stdout holds anything else
  EXPECT_EQ(doc["summary"]["options"], 5);
  EXPECT_EQ(doc["metadata"]["timestamp_utc"], "2023-11-14T22:13:20Z");
  EXPECT_NE(outcome.err.find("configuration ok: 5 option(s)"), std::string::npos);
}

TEST(CliTest, VerbosityOnlyChangesTheLogNeverTheReport) {
  const std::vector<std::string> base = {"--config", example_path("gk_vanillas.json")};
  auto quiet_args = base;
  quiet_args.emplace_back("--quiet");
  auto verbose_args = base;
  verbose_args.emplace_back("-v");
  const auto normal = run(base, {}, fixed_clock());
  const auto quiet = run(quiet_args, {}, fixed_clock());
  const auto verbose = run(verbose_args, {}, fixed_clock());
  EXPECT_EQ(normal.out, quiet.out);
  EXPECT_EQ(normal.out, verbose.out);
  EXPECT_TRUE(quiet.err.empty());
  EXPECT_NE(verbose.err.find("option EURUSD-1Y-ATM-C priced"), std::string::npos);
  EXPECT_EQ(normal.err.find("priced"), std::string::npos);
}

TEST(CliTest, SameInputAndClockGiveByteIdenticalOutput) {
  const std::vector<std::string> args = {"--config", example_path("heston_mc_qe.json")};
  const auto first = run(args, {}, fixed_clock());
  const auto second = run(args, {}, fixed_clock());
  ASSERT_EQ(first.code, 0) << first.err;
  EXPECT_EQ(first.out, second.out);
}

TEST(CliTest, ConfigurationCanComeFromStandardInput) {
  const auto direct = run({"--config", example_path("gk_vanillas.json")}, {}, fixed_clock());
  const std::string text = fxval::apptest::example_text("gk_vanillas.json");
  const auto piped = run({"--config", "-"}, text, fixed_clock());
  ASSERT_EQ(piped.code, 0) << piped.err;
  const json a = json::parse(direct.out);
  const json b = json::parse(piped.out);
  EXPECT_EQ(a["results"], b["results"]);
  EXPECT_EQ(a["metadata"]["config_sha256"], b["metadata"]["config_sha256"]);  // same bytes
  EXPECT_EQ(b["metadata"]["config_file"], "-");
}

TEST(CliTest, InlineEqualsFormIsAccepted) {
  const auto outcome =
      run({"--config=" + example_path("gk_vanillas.json"), "--format=csv"}, {}, fixed_clock());
  ASSERT_EQ(outcome.code, 0) << outcome.err;
  EXPECT_EQ(outcome.out.rfind("# tool: fxval-price", 0), 0U);
}

TEST(CliTest, MissingConfigurationFileExitsFour) {
  const auto outcome = run({"--config", "/definitely/not/here.json"});
  EXPECT_EQ(outcome.code, static_cast<int>(ExitCode::io_error));
  EXPECT_NE(outcome.err.find("cannot open configuration file"), std::string::npos);
  EXPECT_TRUE(outcome.out.empty());
}

TEST(CliTest, InvalidConfigurationExitsTwoNamesTheFieldAndPricesNothing) {
  const auto outcome = run({"--config", "-"}, R"({"format_version": 1, "market": {"spot": -1}})");
  EXPECT_EQ(outcome.code, static_cast<int>(ExitCode::invalid_config));
  EXPECT_NE(outcome.err.find("invalid configuration"), std::string::npos);
  EXPECT_NE(outcome.err.find("market.spot"), std::string::npos) << outcome.err;
  EXPECT_TRUE(outcome.out.empty());

  const auto garbage = run({"--config", "-"}, "this is not json");
  EXPECT_EQ(garbage.code, static_cast<int>(ExitCode::invalid_config));
  EXPECT_NE(garbage.err.find("parse_error"), std::string::npos);
}

TEST(CliTest, ConfigurationLargerThanTheLimitIsRejected) {
  const std::string huge(fxval::app::max_config_bytes + 1, ' ');
  const auto outcome = run({"--config", "-"}, huge);
  EXPECT_EQ(outcome.code, static_cast<int>(ExitCode::invalid_config));
  EXPECT_NE(outcome.err.find("larger than 16 MiB"), std::string::npos);

  const std::string exactly(fxval::app::max_config_bytes,
                            ' ');  // at the limit: read, then not JSON
  const auto at_limit = run({"--config", "-"}, exactly);
  EXPECT_EQ(at_limit.code, static_cast<int>(ExitCode::invalid_config));
  EXPECT_EQ(at_limit.err.find("larger than"), std::string::npos);
}

TEST(CliTest, ReportCanBeWrittenToAFileAtomically) {
  const TempDir dir;
  const std::string target = dir.file("report.json");
  const auto outcome =
      run({"--config", example_path("gk_vanillas.json"), "--output", target}, {}, fixed_clock());
  ASSERT_EQ(outcome.code, 0) << outcome.err;
  EXPECT_TRUE(outcome.out.empty());
  EXPECT_NE(outcome.err.find("report written to"), std::string::npos);
  const auto direct = run({"--config", example_path("gk_vanillas.json")}, {}, fixed_clock());
  EXPECT_EQ(fxval::apptest::read_file(target), direct.out);
  EXPECT_FALSE(std::filesystem::exists(target + ".tmp"));  // no temporary file left behind
}

TEST(CliTest, ExistingReportIsReplacedNotAppended) {
  const TempDir dir;
  const std::string target = dir.file("report.csv");
  {
    std::ofstream old(target);
    old << "OLD CONTENT THAT MUST DISAPPEAR\n";
  }
  const auto outcome =
      run({"--config", example_path("gk_vanillas.json"), "--output", target, "--format", "csv"},
          {},
          fixed_clock());
  ASSERT_EQ(outcome.code, 0) << outcome.err;
  const std::string written = fxval::apptest::read_file(target);
  EXPECT_EQ(written.find("OLD CONTENT"), std::string::npos);
  EXPECT_EQ(written.rfind("# tool: fxval-price", 0), 0U);
}

TEST(CliTest, UnwritableOutputPathExitsFourAndLeavesNothingBehind) {
  const TempDir dir;
  const std::string target = dir.file("no_such_directory/report.json");
  const auto outcome = run({"--config", example_path("gk_vanillas.json"), "--output", target});
  EXPECT_EQ(outcome.code, static_cast<int>(ExitCode::io_error));
  EXPECT_NE(outcome.err.find("cannot open"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(target));
}

TEST(CliTest, OutputOverADirectoryExitsFour) {
  const TempDir dir;
  const std::string target = dir.file("a_directory");
  std::filesystem::create_directories(target);
  const auto outcome =
      run({"--config", example_path("gk_vanillas.json"), "--output", target}, {}, fixed_clock());
  EXPECT_EQ(outcome.code, static_cast<int>(ExitCode::io_error));
  EXPECT_TRUE(std::filesystem::is_directory(target));
  EXPECT_FALSE(std::filesystem::exists(target + ".tmp"));
}

TEST(CliTest, FailedOptionStillProducesTheReportAndExitsThree) {
  CliEnvironment environment = fixed_clock();
  environment.pricer =
      [](const fxval::app::PriceConfig& config,
         const fxval::app::OptionSpec& option) -> fxval::Result<fxval::app::PricedOption> {
    if (option.id == "EURUSD-1Y-ATM-P") {
      return fxval::Error{fxval::ErrorCode::numerical_failure, "synthetic failure"};
    }
    return fxval::app::price_option(config, option);
  };
  const auto outcome = run({"--config", example_path("gk_vanillas.json")}, {}, environment);
  EXPECT_EQ(outcome.code, static_cast<int>(ExitCode::pricing_failed));
  const json doc = json::parse(outcome.out);  // the report was written in full
  EXPECT_EQ(doc["summary"]["failed"], 1);
  EXPECT_EQ(doc["results"][1]["status"], "error");
  EXPECT_EQ(doc["results"][0]["status"], "ok");
  EXPECT_NE(outcome.err.find("option EURUSD-1Y-ATM-P: synthetic failure"), std::string::npos);
  EXPECT_NE(outcome.err.find("1 of 5 option(s) failed"), std::string::npos);
}

TEST(CliTest, UnexpectedExceptionExitsFiveWithoutCrashing) {
  CliEnvironment environment = fixed_clock();
  environment.pricer =
      [](const fxval::app::PriceConfig&,
         const fxval::app::OptionSpec&) -> fxval::Result<fxval::app::PricedOption> {
    throw std::runtime_error("boom");
  };
  const auto outcome = run({"--config", example_path("gk_vanillas.json")}, {}, environment);
  EXPECT_EQ(outcome.code, static_cast<int>(ExitCode::internal_error));
  EXPECT_NE(outcome.err.find("internal error: boom"), std::string::npos);

  environment.pricer =
      [](const fxval::app::PriceConfig&,
         const fxval::app::OptionSpec&) -> fxval::Result<fxval::app::PricedOption> {
    throw 42;
  };  // NOLINT: not derived from exception
  const auto odd = run({"--config", example_path("gk_vanillas.json")}, {}, environment);
  EXPECT_EQ(odd.code, static_cast<int>(ExitCode::internal_error));
  EXPECT_NE(odd.err.find("unknown exception"), std::string::npos);
}

TEST(CliTest, EpochOverrideAppearsInBothFormats) {
  CliEnvironment environment;
  environment.epoch_seconds = 951'782'400;  // 2000-02-29
  const auto json_out = run({"--config", example_path("gk_vanillas.json")}, {}, environment);
  EXPECT_NE(json_out.out.find("\"timestamp_utc\": \"2000-02-29T00:00:00Z\""), std::string::npos);
  const auto csv_out =
      run({"--config", example_path("gk_vanillas.json"), "--format", "csv"}, {}, environment);
  EXPECT_NE(csv_out.out.find("# timestamp_utc: 2000-02-29T00:00:00Z"), std::string::npos);
}

TEST(CliTest, WithoutOverrideTheSystemClockIsUsed) {
  const auto outcome = run({"--config", example_path("gk_vanillas.json")});
  ASSERT_EQ(outcome.code, 0);
  const json doc = json::parse(outcome.out);
  const std::string stamp = doc["metadata"]["timestamp_utc"];
  EXPECT_EQ(stamp.size(), 20U);
  EXPECT_GE(stamp, "2026-01-01T00:00:00Z");
}

TEST(ParseEpochTest, AcceptsOnlyWholeSecondsInRange) {
  EXPECT_EQ(parse_epoch_seconds("0"), 0);
  EXPECT_EQ(parse_epoch_seconds("1700000000"), 1'700'000'000);
  EXPECT_EQ(parse_epoch_seconds("253402300799"), 253'402'300'799);
  for (const char* bad : {"",
                          "-1",
                          "+1",
                          "1.5",
                          "1e9",
                          " 1",
                          "1 ",
                          "abc",
                          "253402300800",
                          "99999999999999999999",
                          "0x10"}) {
    EXPECT_FALSE(parse_epoch_seconds(bad).has_value()) << bad;
  }
}

}  // namespace
