#include <gtest/gtest.h>

#include <string>

#include "fxval/version.hpp"

TEST(Version, StringMatchesComponents) {
  const std::string expected = std::to_string(fxval::version_major) + "." +
                               std::to_string(fxval::version_minor) + "." +
                               std::to_string(fxval::version_patch);
  EXPECT_EQ(fxval::version_string, expected);
}
