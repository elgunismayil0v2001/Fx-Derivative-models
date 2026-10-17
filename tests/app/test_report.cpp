// Tests for the report writers: structure, lossless numbers, error rows, CSV quoting, and the
// metadata that makes a report auditable.

#include <gtest/gtest.h>

#include <charconv>
#include <sstream>
#include <string>
#include <vector>

#include "app_test_support.hpp"
#include "price_runner.hpp"
#include "report.hpp"

namespace {

using fxval::app::csv_field;
using fxval::app::make_run_metadata;
using fxval::app::report_to_csv;
using fxval::app::report_to_json;
using fxval::app::run_price;
using fxval::apptest::heston_mc_json;
using fxval::apptest::must_parse;
using fxval::apptest::must_parse_example;
using fxval::testing::bits_of;
using nlohmann::json;

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);) {
    lines.push_back(line);
  }
  return lines;
}

TEST(CsvFieldTest, QuotesOnlyWhenNeeded) {
  EXPECT_EQ(csv_field("plain"), "plain");
  EXPECT_EQ(csv_field(""), "");
  EXPECT_EQ(csv_field("a,b"), "\"a,b\"");
  EXPECT_EQ(csv_field("say \"hi\""), "\"say \"\"hi\"\"\"");
  EXPECT_EQ(csv_field("two\nlines"), "\"two\nlines\"");
  EXPECT_EQ(csv_field("cr\rhere"), "\"cr\rhere\"");
}

TEST(ReportJsonTest, StructureAndMetadata) {
  const auto config = must_parse_example("gk_vanillas.json");
  const auto report = run_price(config);
  const auto metadata = make_run_metadata("config bytes", 1'700'000'000);
  const json doc = json::parse(report_to_json(metadata, "my/config.json", config, report));

  EXPECT_EQ(doc["format_version"], 1);
  EXPECT_EQ(doc["metadata"]["tool"], "fxval-price");
  EXPECT_EQ(doc["metadata"]["config_file"], "my/config.json");
  EXPECT_EQ(doc["metadata"]["config_sha256"], metadata.config_sha256);
  EXPECT_EQ(doc["metadata"]["timestamp_utc"], "2023-11-14T22:13:20Z");
  EXPECT_EQ(doc["metadata"]["git_commit"], metadata.git_commit);
  EXPECT_EQ(doc["inputs"]["market"]["spot"], 1.1);
  EXPECT_EQ(doc["inputs"]["model"]["type"], "garman_kohlhagen");
  EXPECT_EQ(doc["inputs"]["model"]["volatility"], 0.08);
  EXPECT_EQ(doc["summary"]["options"], 5);
  EXPECT_EQ(doc["summary"]["failed"], 0);
  ASSERT_EQ(doc["results"].size(), 5U);
  EXPECT_EQ(doc["results"][0]["id"], "EURUSD-1Y-ATM-C");
  EXPECT_EQ(doc["results"][0]["status"], "ok");
  EXPECT_TRUE(doc["results"][0].contains("greeks"));
  EXPECT_FALSE(doc["results"][0].contains("standard_error"));
  EXPECT_FALSE(doc["results"][0].contains("error"));
}

TEST(ReportJsonTest, TopLevelFieldOrderIsFixed) {
  const auto config = must_parse_example("gk_vanillas.json");
  const std::string text =
      report_to_json(make_run_metadata("x", 0), "c.json", config, run_price(config));
  const auto at = [&](const char* key) {
    return text.find(std::string("\n  \"") + key + "\"");
  };
  ASSERT_NE(at("format_version"), std::string::npos);
  EXPECT_LT(at("format_version"), at("metadata"));
  EXPECT_LT(at("metadata"), at("inputs"));
  EXPECT_LT(at("inputs"), at("summary"));
  EXPECT_LT(at("summary"), at("results"));
}

TEST(ReportJsonTest, NumbersSurviveTheTextRepresentationExactly) {
  const auto config = must_parse_example("heston_semi_analytic_smile.json");
  const auto report = run_price(config);
  const json doc = json::parse(report_to_json(make_run_metadata("x", 0), "c.json", config, report));
  for (std::size_t i = 0; i < report.options.size(); ++i) {
    EXPECT_EQ(bits_of(doc["results"][i]["price"].get<double>()), bits_of(report.options[i].price));
    EXPECT_EQ(bits_of(doc["results"][i]["present_value"].get<double>()),
              bits_of(report.options[i].present_value));
  }
}

TEST(ReportJsonTest, MonteCarloReportEchoesSeedAndSettings) {
  const auto config = must_parse(heston_mc_json("qe", 20'000, 2, true, false, 424242).dump());
  const auto report = run_price(config);
  const json doc = json::parse(report_to_json(make_run_metadata("x", 0), "c.json", config, report));
  const json& mc = doc["inputs"]["model"]["monte_carlo"];
  EXPECT_EQ(mc["seed"], 424242);
  EXPECT_EQ(mc["paths"], 20'000);
  EXPECT_EQ(mc["steps"], 16);
  EXPECT_EQ(mc["antithetic"], true);
  EXPECT_EQ(mc["control_variate"], false);
  EXPECT_EQ(doc["inputs"]["model"]["scheme"], "qe");
  EXPECT_TRUE(doc["results"][0].contains("standard_error"));
  EXPECT_FALSE(doc["results"][0].contains("greeks"));
}

TEST(ReportJsonTest, FailedOptionsHaveAnErrorAndNoNumbers) {
  const auto config = must_parse_example("gk_vanillas.json");
  const auto report = run_price(
      config,
      [](const fxval::app::PriceConfig& cfg,
         const fxval::app::OptionSpec& option) -> fxval::Result<fxval::app::PricedOption> {
        if (option.id == "EURUSD-3M-OTM-C") {
          return fxval::Error{fxval::ErrorCode::not_converged, "no \"convergence\", sorry"};
        }
        return fxval::app::price_option(cfg, option);
      });
  const json doc = json::parse(report_to_json(make_run_metadata("x", 0), "c.json", config, report));
  EXPECT_EQ(doc["summary"]["failed"], 1);
  const json& row = doc["results"][2];
  EXPECT_EQ(row["status"], "error");
  EXPECT_EQ(row["error"], "no \"convergence\", sorry");
  EXPECT_FALSE(row.contains("price"));
  EXPECT_FALSE(row.contains("present_value"));
  EXPECT_EQ(doc["results"][0]["status"], "ok");
}

TEST(ReportCsvTest, MetadataCommentsHeaderAndOneRowPerOption) {
  const auto config = must_parse_example("gk_vanillas.json");
  const auto report = run_price(config);
  const auto metadata = make_run_metadata("config bytes", 1'700'000'000);
  const auto lines = split_lines(report_to_csv(metadata, "c.json", config, report));

  std::size_t comments = 0;
  while (comments < lines.size() && lines[comments].rfind("# ", 0) == 0) {
    ++comments;
  }
  EXPECT_GE(comments, 9U);
  EXPECT_NE(lines[0].find("# tool: fxval-price"), std::string::npos);
  bool has_hash = false;
  for (std::size_t i = 0; i < comments; ++i) {
    has_hash = has_hash || lines[i] == "# config_sha256: " + metadata.config_sha256;
  }
  EXPECT_TRUE(has_hash);

  ASSERT_EQ(lines.size(), comments + 1 + 5);
  EXPECT_EQ(lines[comments].rfind("id,type,strike,expiry_years,notional,status,price,", 0), 0U);
  const std::size_t header_columns =
      static_cast<std::size_t>(std::count(lines[comments].begin(), lines[comments].end(), ',')) + 1;
  for (std::size_t i = comments + 1; i < lines.size(); ++i) {
    EXPECT_EQ(static_cast<std::size_t>(std::count(lines[i].begin(), lines[i].end(), ',')) + 1,
              header_columns)
        << lines[i];
    EXPECT_NE(lines[i].find(",ok,"), std::string::npos);
  }
}

TEST(ReportCsvTest, PriceTextReadsBackToTheSameDouble) {
  const auto config = must_parse_example("heston_semi_analytic_smile.json");
  const auto report = run_price(config);
  const auto lines =
      split_lines(report_to_csv(make_run_metadata("x", 0), "c.json", config, report));
  std::size_t header = 0;
  while (lines[header].rfind("# ", 0) == 0) {
    ++header;
  }
  for (std::size_t i = 0; i < report.options.size(); ++i) {
    std::vector<std::string> cells;
    std::istringstream row(lines[header + 1 + i]);
    for (std::string cell; std::getline(row, cell, ',');) {
      cells.push_back(cell);
    }
    ASSERT_GT(cells.size(), 7U);
    EXPECT_EQ(cells[0], report.options[i].id);
    double price = 0.0;
    std::from_chars(cells[6].data(), cells[6].data() + cells[6].size(), price);
    EXPECT_EQ(bits_of(price), bits_of(report.options[i].price));
  }
}

TEST(ReportCsvTest, ErrorTextWithCommasAndQuotesIsQuotedAndKeepsTheColumnCount) {
  const auto config = must_parse_example("gk_vanillas.json");
  const auto report = run_price(
      config,
      [](const fxval::app::PriceConfig& cfg,
         const fxval::app::OptionSpec& option) -> fxval::Result<fxval::app::PricedOption> {
        if (option.id == "EURUSD-1Y-ATM-P") {
          return fxval::Error{fxval::ErrorCode::numerical_failure, "bad, \"very\" bad\nreally"};
        }
        return fxval::app::price_option(cfg, option);
      });
  const std::string csv = report_to_csv(make_run_metadata("x", 0), "c.json", config, report);
  EXPECT_NE(csv.find("\"bad, \"\"very\"\" bad\nreally\""), std::string::npos) << csv;
  const auto row = csv.find("EURUSD-1Y-ATM-P");
  ASSERT_NE(row, std::string::npos);
  EXPECT_EQ(csv.substr(row).rfind("EURUSD-1Y-ATM-P,put,1.1,1,1000000,error,,,", 0), 0U);
}

TEST(ReportCsvTest, MonteCarloCommentsCarrySeedAndPaths) {
  const auto config = must_parse(heston_mc_json("euler", 5000, 1, false, false, 31337).dump());
  const auto report = run_price(config);
  const std::string csv = report_to_csv(make_run_metadata("x", 0), "c.json", config, report);
  EXPECT_NE(csv.find("# random_seed: 31337\n"), std::string::npos);
  EXPECT_NE(csv.find("# paths: 5000\n"), std::string::npos);
  EXPECT_NE(csv.find("# model: heston_monte_carlo\n"), std::string::npos);
}

}  // namespace
