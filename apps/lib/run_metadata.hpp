#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace fxval::app {

// Everything needed to tie an output file to the code, input and settings that produced it.
// Model-risk teams ask for exactly this: given a number in a report, which build, which input and
// which seed produced it? (docs/validation_report.md, section on reproducibility).
struct RunMetadata {
  std::string tool;           // program name
  std::string fxval_version;  // library version (semantic versioning)
  std::string git_commit;  // 12 hex digits, "-dirty" if tracked files were modified, or "unknown"
  std::string compiler;    // compiler id and version used to build the program
  std::string build_type;  // CMake build type
  std::string config_sha256;  // SHA-256 of the exact bytes of the input configuration
  std::string timestamp_utc;  // ISO 8601, e.g. 2026-10-17T14:03:09Z
};

// Builds the metadata. `config_bytes` is the configuration exactly as read. `epoch_seconds` is the
// time to record (seconds since 1970-01-01 UTC): the caller passes the real clock, or the value of
// SOURCE_DATE_EPOCH when set, so that tests and reproducible builds can fix it.
[[nodiscard]] RunMetadata make_run_metadata(std::string_view config_bytes,
                                            std::int64_t epoch_seconds);

// "2026-10-17T14:03:09Z" for seconds since the Unix epoch (proleptic Gregorian calendar, UTC).
// Written out by hand (Hinnant's civil-from-days) because gmtime is not thread safe and the C++20
// calendar formatting is not available in every supported compiler. Valid for years 0..9999.
[[nodiscard]] std::string format_utc_timestamp(std::int64_t epoch_seconds);

}  // namespace fxval::app
