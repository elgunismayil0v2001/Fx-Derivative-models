#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "fxval/core/contracts.hpp"

namespace fxval {

// Categories of recoverable failure. See docs/adr/0001-error-policy.md.
enum class ErrorCode : std::uint8_t {
  invalid_argument,   // input outside the documented domain; the caller can fix it
  numerical_failure,  // a computation produced a non-finite or unusable value
  not_converged,      // an iterative method hit its iteration or tolerance limit
  parse_error,        // configuration or data could not be parsed
  io_error,           // a file could not be read or written
};

[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;

struct Error {
  ErrorCode code;
  std::string message;

  friend bool operator==(const Error&, const Error&) = default;
};

// Either a value or an Error. Never silently holds NaN as a stand-in for failure.
// (C++20 has no std::expected; this is the small subset the library needs.)
template <class T>
class [[nodiscard]] Result {
public:
  Result(T value) : data_(std::in_place_index<0>, std::move(value)) {}
  Result(Error error) : data_(std::in_place_index<1>, std::move(error)) {}

  [[nodiscard]] bool has_value() const noexcept { return data_.index() == 0; }
  [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

  [[nodiscard]] const T& value() const& {
    FXVAL_EXPECTS(has_value());
    return std::get<0>(data_);
  }
  [[nodiscard]] T& value() & {
    FXVAL_EXPECTS(has_value());
    return std::get<0>(data_);
  }
  [[nodiscard]] T&& value() && {
    FXVAL_EXPECTS(has_value());
    return std::get<0>(std::move(data_));
  }

  [[nodiscard]] const Error& error() const& {
    FXVAL_EXPECTS(!has_value());
    return std::get<1>(data_);
  }

  template <class U>
  [[nodiscard]] T value_or(U&& fallback) const& {
    return has_value() ? std::get<0>(data_) : static_cast<T>(std::forward<U>(fallback));
  }

private:
  std::variant<T, Error> data_;
};

}  // namespace fxval
