# ADR 0001: Error policy

* Status: accepted
* Date: 2026-10-09

## Context

A pricing and risk library fails in three different ways, and mixing them up is how silent wrong
numbers reach a report:

1. The **input data is bad**: a volatility of 8 instead of 0.08, a NaN from a data feed, a
   negative strike, a config file with a missing field.
2. A **numerical method fails** on valid input: a calibration does not converge, an integral
   does not converge, a simulated variance goes negative.
3. The **caller made a programming mistake**: it read the value of a result that holds an error.

A model-validation framework has a further requirement: it must never return a plausible-looking
number when something went wrong.

## Decision

* Case 1 and 2 are reported with `fxval::Result<T>`, which holds either a `T` or an
  `fxval::Error{code, message}`. The message names the quantity and the offending value. The
  type is `[[nodiscard]]`, so ignoring a result is a compile-time warning (an error in CI).
* Market quantities are **validated once, at construction** (`Volatility`, `Rate`, `Years`,
  `Strike`, `Notional`). Code that takes these types can assume finite values in a documented
  range and does not re-check them.
* Numerical failures **never return NaN as a signal**. They return an `Error` with code
  `numerical_failure` or `not_converged`.
* Case 3 is a **contract violation**, checked with `FXVAL_EXPECTS` in Debug builds: print the
  failed condition and abort. In other builds the check compiles away. `Result::value()` on an
  error still throws `std::bad_variant_access` in those builds, so it cannot return garbage.
* Inner-loop kernels (Monte Carlo path generation, for example) are `noexcept`, take validated
  types, document their preconditions and do not return `Result`. Checks happen at the module
  boundary, not per path.
* No `-ffast-math` or `-Ofast`: CMake refuses to configure with them, because they break
  reproducibility.

## Alternatives considered

* **Exceptions everywhere.** Familiar, but it hides which functions can fail, makes the cost of
  failure paths invisible in hot code, and a library-wide `catch` can swallow a real bug.
* **`std::expected`.** The right tool, but it is C++23 and the project targets C++20 so that it
  builds on the compilers in CI. `Result<T>` is the small subset needed and can be replaced by
  `std::expected` with a mechanical change later.
* **Return NaN and let the caller check.** Rejected: it is exactly the silent failure this
  policy exists to prevent.
* **Release-mode contract checks.** Cheap to add, but they add branches to hot paths for a class
  of error (programmer mistakes) that Debug and sanitizer CI jobs already catch.

## Consequences

* Every error path needs a test. `tests/unit/test_units.cpp` is the pattern.
* Callers must handle `Result`. That is intended friction at the boundary and none inside kernels.
* The sanity bounds in `limits.hpp` (for example volatility at most 500%) will reject some
  extreme but real inputs. They are deliberately generous and can be widened with a documented
  reason.
* Death tests cannot report coverage for the abort path. `contract_violation` is excluded from
  the coverage measurement with a comment explaining why.
