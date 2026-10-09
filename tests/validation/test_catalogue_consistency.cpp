// Keeps docs/test_catalogue.md honest: the plan promises 21 validation tests (V-01 .. V-21),
// each listed exactly once. As tests are implemented, this file is where a check that every
// "implemented" row has a matching named test will be added.

#include <gtest/gtest.h>

#include <fstream>
#include <iomanip>
#include <map>
#include <regex>
#include <sstream>
#include <string>

#ifndef FXVAL_TEST_CATALOGUE_PATH
#error "FXVAL_TEST_CATALOGUE_PATH must be defined by the build system"
#endif

namespace {

constexpr int planned_validation_tests = 21;

std::string id_for(int n) {
  std::ostringstream id;
  id << "V-" << std::setw(2) << std::setfill('0') << n;
  return id.str();
}

TEST(TestCatalogue, ListsEveryPlannedValidationTestExactlyOnce) {
  std::ifstream catalogue(FXVAL_TEST_CATALOGUE_PATH);
  ASSERT_TRUE(catalogue.is_open()) << "cannot open " << FXVAL_TEST_CATALOGUE_PATH;

  const std::regex table_row(R"(^\|\s*(V-\d{2})\s*\|)");
  std::map<std::string, int> occurrences;
  for (std::string line; std::getline(catalogue, line);) {
    std::smatch match;
    if (std::regex_search(line, match, table_row)) {
      ++occurrences[match[1].str()];
    }
  }

  for (int n = 1; n <= planned_validation_tests; ++n) {
    EXPECT_EQ(occurrences[id_for(n)], 1) << id_for(n) << " must appear exactly once";
  }
  EXPECT_EQ(static_cast<int>(occurrences.size()), planned_validation_tests)
      << "the catalogue lists IDs outside V-01..V-" << planned_validation_tests;
}

}  // namespace
