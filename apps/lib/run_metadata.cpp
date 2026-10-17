#include "run_metadata.hpp"

#include <array>
#include <cstddef>

#include "build_info.hpp"
#include "fxval/core/sha256.hpp"
#include "fxval/version.hpp"

namespace fxval::app {

std::string format_utc_timestamp(std::int64_t epoch_seconds) {
  constexpr std::int64_t seconds_per_day = 86'400;
  std::int64_t days = epoch_seconds / seconds_per_day;
  std::int64_t second_of_day = epoch_seconds % seconds_per_day;
  if (second_of_day < 0) {  // floor division for times before 1970
    second_of_day += seconds_per_day;
    --days;
  }
  // Civil date from days since 1970-01-01 (Howard Hinnant, "chrono-compatible low-level date
  // algorithms").
  days += 719'468;
  const std::int64_t era = (days >= 0 ? days : days - 146'096) / 146'097;
  const std::int64_t day_of_era = days - era * 146'097;
  const std::int64_t year_of_era =
      (day_of_era - day_of_era / 1'460 + day_of_era / 36'524 - day_of_era / 146'096) / 365;
  const std::int64_t day_of_year =
      day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  const std::int64_t shifted_month = (5 * day_of_year + 2) / 153;
  const std::int64_t day = day_of_year - (153 * shifted_month + 2) / 5 + 1;
  const std::int64_t month = shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
  const std::int64_t year = year_of_era + era * 400 + (month <= 2 ? 1 : 0);

  const auto two = [](std::int64_t v) {
    return std::string{static_cast<char>('0' + (v / 10) % 10), static_cast<char>('0' + v % 10)};
  };
  std::string text;
  text.reserve(20);
  text += std::string{static_cast<char>('0' + (year / 1000) % 10),
                      static_cast<char>('0' + (year / 100) % 10),
                      static_cast<char>('0' + (year / 10) % 10),
                      static_cast<char>('0' + year % 10)};
  text += '-' + two(month) + '-' + two(day) + 'T' + two(second_of_day / 3600) + ':' +
          two((second_of_day / 60) % 60) + ':' + two(second_of_day % 60) + 'Z';
  return text;
}

RunMetadata make_run_metadata(std::string_view config_bytes, std::int64_t epoch_seconds) {
  return RunMetadata{"fxval-price",
                     std::string(fxval::version_string),
                     build_info::git_commit,
                     build_info::compiler,
                     build_info::build_type,
                     sha256_hex(config_bytes),
                     format_utc_timestamp(epoch_seconds)};
}

}  // namespace fxval::app
