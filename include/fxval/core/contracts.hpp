#pragma once

// Contract checks for programmer errors (violated preconditions).
//
// Policy (docs/adr/0001-error-policy.md):
//  - Bad *input data* is never a contract violation. It is reported through Result<T>.
//  - A contract violation is a bug in the caller. In Debug builds (FXVAL_ENABLE_CONTRACTS) it
//    prints the failed condition and aborts. In other builds the check compiles away.

namespace fxval::detail {

[[noreturn]] void contract_violation(const char* expression, const char* file, int line) noexcept;

}  // namespace fxval::detail

#if defined(FXVAL_ENABLE_CONTRACTS)
#define FXVAL_EXPECTS(condition) \
  (static_cast<bool>(condition)  \
       ? static_cast<void>(0)    \
       : ::fxval::detail::contract_violation(#condition, __FILE__, __LINE__))
#else
// sizeof keeps the expression compiled (so it cannot rot) without evaluating it.
#define FXVAL_EXPECTS(condition) static_cast<void>(sizeof(static_cast<bool>(condition)))
#endif
