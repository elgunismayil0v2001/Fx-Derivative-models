// Run metadata: the timestamp formatter against dates computed independently with Python's
// datetime (fixed list: boundaries such as leap days, the century rules, year 9999, times before
// 1970, plus 40 random instants), and the metadata record itself.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "fxval/core/sha256.hpp"
#include "fxval/version.hpp"
#include "run_metadata.hpp"

namespace {

using fxval::app::format_utc_timestamp;
using fxval::app::make_run_metadata;

struct Row {
  std::int64_t epoch;
  const char* expected;
};

constexpr Row kRows[] = {
    {0LL, "1970-01-01T00:00:00Z"},
    {1LL, "1970-01-01T00:00:01Z"},
    {59LL, "1970-01-01T00:00:59Z"},
    {60LL, "1970-01-01T00:01:00Z"},
    {86399LL, "1970-01-01T23:59:59Z"},
    {86400LL, "1970-01-02T00:00:00Z"},
    {951782399LL, "2000-02-28T23:59:59Z"},
    {951782400LL, "2000-02-29T00:00:00Z"},
    {951868800LL, "2000-03-01T00:00:00Z"},
    {1078012800LL, "2004-02-29T00:00:00Z"},
    {1709164800LL, "2024-02-29T00:00:00Z"},
    {1709251200LL, "2024-03-01T00:00:00Z"},
    {1700000000LL, "2023-11-14T22:13:20Z"},
    {4102444800LL, "2100-01-01T00:00:00Z"},
    {253402300799LL, "9999-12-31T23:59:59Z"},
    {-1LL, "1969-12-31T23:59:59Z"},
    {-86400LL, "1969-12-31T00:00:00Z"},
    {-86401LL, "1969-12-30T23:59:59Z"},
    {-2208988800LL, "1900-01-01T00:00:00Z"},
    {-62103974400LL, "0002-01-02T00:00:00Z"},
    {241903809LL, "1977-08-31T19:30:09Z"},
    {1464710496LL, "2016-05-31T16:01:36Z"},
    {2125293655LL, "2037-05-07T07:20:55Z"},
    {-756068454LL, "1946-01-16T04:59:06Z"},
    {840618605LL, "1996-08-21T09:10:05Z"},
    {2768109668LL, "2057-09-19T07:21:08Z"},
    {-1884307418LL, "1910-04-16T21:16:22Z"},
    {3944969618LL, "2095-01-04T08:53:38Z"},
    {1205742961LL, "2008-03-17T08:36:01Z"},
    {3378947314LL, "2077-01-27T04:28:34Z"},
    {3658340639LL, "2085-12-04T21:43:59Z"},
    {-275180038LL, "1961-04-13T01:06:02Z"},
    {-1732883976LL, "1915-02-02T11:20:24Z"},
    {1759455744LL, "2025-10-03T01:42:24Z"},
    {2943144335LL, "2063-04-07T04:05:35Z"},
    {-1098013884LL, "1935-03-17T12:08:36Z"},
    {319124957LL, "1980-02-11T13:49:17Z"},
    {743509920LL, "1993-07-24T10:32:00Z"},
    {-671998116LL, "1948-09-15T05:51:24Z"},
    {2646569873LL, "2053-11-12T14:17:53Z"},
    {2903165334LL, "2061-12-30T10:48:54Z"},
    {1488390700LL, "2017-03-01T17:51:40Z"},
    {1951262632LL, "2031-11-01T01:23:52Z"},
    {-1923402202LL, "1909-01-19T09:36:38Z"},
    {3893523388LL, "2093-05-18T22:16:28Z"},
    {2516883910LL, "2049-10-03T14:25:10Z"},
    {-476825330LL, "1954-11-22T04:31:10Z"},
    {2121043530LL, "2037-03-19T02:45:30Z"},
    {3904845648LL, "2093-09-26T23:20:48Z"},
    {1325914985LL, "2012-01-07T05:43:05Z"},
    {1727123979LL, "2024-09-23T20:39:39Z"},
    {-973732298LL, "1939-02-22T22:48:22Z"},
    {391509186LL, "1982-05-29T08:33:06Z"},
    {2111968117LL, "2036-12-04T01:48:37Z"},
    {-455066671LL, "1955-08-01T00:35:29Z"},
    {-852383197LL, "1942-12-28T10:53:23Z"},
    {1377861615LL, "2013-08-30T11:20:15Z"},
    {3122811506LL, "2068-12-15T15:38:26Z"},
    {-1443862134LL, "1924-03-31T15:11:06Z"},
    {828515727LL, "1996-04-03T07:15:27Z"},
};

TEST(RunMetadataTest, TimestampMatchesPythonDatetime) {
  for (const Row& row : kRows) {
    EXPECT_EQ(format_utc_timestamp(row.epoch), row.expected) << row.epoch;
  }
}

TEST(RunMetadataTest, MetadataRecordsTheInputHashVersionAndTime) {
  const std::string bytes = "{\"format_version\": 1}\n";
  const auto metadata = make_run_metadata(bytes, 1'700'000'000);
  EXPECT_EQ(metadata.tool, "fxval-price");
  EXPECT_EQ(metadata.fxval_version, std::string(fxval::version_string));
  EXPECT_EQ(metadata.config_sha256, fxval::sha256_hex(bytes));
  EXPECT_EQ(metadata.timestamp_utc, "2023-11-14T22:13:20Z");
  EXPECT_FALSE(metadata.git_commit.empty());
  EXPECT_FALSE(metadata.compiler.empty());
  EXPECT_FALSE(metadata.build_type.empty());
}

TEST(RunMetadataTest, DifferentInputBytesGiveDifferentHashes) {
  EXPECT_NE(make_run_metadata("a", 0).config_sha256, make_run_metadata("a ", 0).config_sha256);
  EXPECT_EQ(make_run_metadata("a", 0).config_sha256, make_run_metadata("a", 99).config_sha256);
}

}  // namespace
