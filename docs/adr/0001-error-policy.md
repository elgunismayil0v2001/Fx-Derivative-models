# ADR 0001: Error policy

* Status: accepted
* Date: reconstructed later (the repository did not contain this file when the project started)

## Context

A pricing library can fail for different reasons: a caller passes a volatility of 8 meaning 8%,
a numerical routine does not converge, a configuration file cannot be parsed, or a programmer
violates a documented precondition. These need different treatment, and none of them may produce
a plausible but wrong number.

## Decision

* **Bad input data and numerical failure are values.** Boundary functions return `Result<T>`
  (`fxval/core/error.hpp`) holding either a value or an `Error{code, message}`, with codes
  `invalid_argument`, `numerical_failure`, `not_converged`, `parse_error`, `io_error`.
* **Programmer errors are contract violations.** `FXVAL_EXPECTS(condition)` checks a precondition
  in Debug builds (`FXVAL_ENABLE_CONTRACTS`), prints it and aborts. In other builds it compiles
  away (the expression is still type-checked). Bad *data* is never a contract violation.
* **Never NaN as a failure signal.** A result that is not finite becomes `numerical_failure`.
* **Typed units validate once.** `Volatility`, `Rate`, `Years`, `Spot`, `Strike`, `Notional` are
  built through `Result`-returning factories; code that receives them can assume the value is
  finite and in range. Raw `double` does not cross a public API when a unit exists.
* **Inner-loop kernels are `noexcept`** and take validated types. No exceptions are used for
  control flow. (Exceptions from the standard library, such as `bad_alloc`, are not caught.)
* **No `-ffast-math`.** Results must be reproducible; the build refuses it.

## Alternatives considered

* Exceptions everywhere: simple call sites, but invisible in signatures and awkward in tight
  loops and in code called from other languages later.
* `std::expected`: the right tool, not available in C++20 on the supported compilers; `Result`
  is the small subset needed and can be replaced by it later.
* Returning NaN: rejected, it propagates silently into risk numbers.

## Consequences

* Every error path needs a test (CONTRIBUTING.md).
* Call sites must check results; `Result` is `[[nodiscard]]`.
