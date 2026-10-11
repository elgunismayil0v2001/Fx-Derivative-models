#include "fxval/core/daycount.hpp"

#include <sstream>

namespace fxval {

Result<Years> act365_fixed(std::chrono::sys_days start, std::chrono::sys_days end) {
  const auto days = (end - start).count();
  if (days < 0) {
    std::ostringstream message;
    message << "ACT/365 fixed requires end >= start, got " << days << " days";
    return Error{ErrorCode::invalid_argument, message.str()};
  }
  return Years::from_years(static_cast<double>(days) / 365.0);
}

}  // namespace fxval
