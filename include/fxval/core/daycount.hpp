#pragma once

#include <chrono>

#include "fxval/core/units.hpp"

namespace fxval {

// ACT/365 fixed: year fraction = (end - start) / 365, using calendar days.
// `end == start` is a zero expiry. `end < start` is invalid_argument.
[[nodiscard]] Result<Years> act365_fixed(std::chrono::sys_days start, std::chrono::sys_days end);

}  // namespace fxval
