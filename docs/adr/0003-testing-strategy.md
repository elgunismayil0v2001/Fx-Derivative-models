# ADR 0003: Testing strategy (unit tests and validation tests)

* Status: accepted
* Date: 2026-10-11

## Context

This repository is both a library and an independent validation of the models in it. Two
different questions need two different kinds of test:

1. **Does the code do what its author intended, and does it keep doing so?** (software quality)
2. **Is the model, as implemented, correct and fit for purpose?** (model validation)

A test that passes because it repeats the formula under test answers neither question.

## Decision

| | Unit tests | Validation tests |
|---|---|---|
| Location | `tests/unit/` | `tests/validation/` |
| Question | Does the code behave as designed? | Is the model implementation right? |
| Oracle | Known values, mathematical limits, symmetries, expected errors | Something independent of the code: an exact identity, finite differences of the price, an independent implementation in another language |
| Fails when | Someone changes behaviour by accident, an error path breaks, an input is not rejected | A formula has a wrong sign or derivative, a model disagrees with its benchmark |
| Listed in | not listed | `docs/test_catalogue.md` (V-01 to V-21 plus named reference tests) |
| Appears in the validation report | no | yes, with measured numbers |

Rules:

* A validation test must not reuse the code under test to build its expected value. Greeks are
  checked against finite differences of the price, not against a second copy of the Greek
  formula. The one allowed exception is stated where it is used (volga is differenced from the
  analytic vega, because second differences of the price are limited by rounding noise).
* Validation tests **record the worst error and where it occurred**, not only pass or fail, so
  the margin to the tolerance is visible and a drift is noticed before it fails.
* A tolerance is changed only with a reason written in `docs/test_catalogue.md`.
* Unit tests cover the failure paths too: every `Result` error code a function can return is
  produced by at least one test (for example overflow, zero volatility, out-of-range delta).
* Contract checks (`FXVAL_EXPECTS`) are Debug only and are tested with death tests.
* The independent Python reference (`python/gen_gk_reference.py`) is deterministic. CI
  regenerates it and fails if it differs from the committed CSV, so the table cannot drift
  silently from the script that claims to have produced it.
* `tests/validation/test_catalogue_consistency.cpp` keeps the catalogue honest: every ID once.

## Consequences

* A reader can see which tests prove the code is maintained well and which prove the model is
  right, and a reviewer cannot mistake one for the other.
* Validation tests cost more to write (an independent oracle is needed) and are the part that
  matters most for a model-validation role.
* Line coverage is reported for the library but is **not** the quality target. 99.6% coverage
  says the lines ran, not that the numbers are right.
