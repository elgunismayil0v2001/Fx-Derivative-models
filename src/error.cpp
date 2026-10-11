#include "fxval/core/error.hpp"

namespace fxval {

std::string_view to_string(ErrorCode code) noexcept {
  switch (code) {
  case ErrorCode::invalid_argument:
    return "invalid_argument";
  case ErrorCode::numerical_failure:
    return "numerical_failure";
  case ErrorCode::not_converged:
    return "not_converged";
  case ErrorCode::parse_error:
    return "parse_error";
  case ErrorCode::io_error:
    return "io_error";
  }
  return "unknown";  // GCOVR_EXCL_LINE: unreachable for valid enumerators; silences -Wreturn-type
}

}  // namespace fxval
