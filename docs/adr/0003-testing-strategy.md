# ADR 0003: Testing strategy

* Status: accepted
* Date: reconstructed later (the repository did not contain this file when the project started)

## Context

The project has two purposes: production-quality C++ and independent model validation. A single
pile of tests would mix two different questions: "does the code still do what it did?" and "is
the model right?".

## Decision

Two kinds of test, in two folders and two executables:

* **Unit tests (`tests/unit/`)** protect the code: known values, degenerate inputs, error
  paths, identities that must hold exactly, determinism. A failing unit test means a regression.
  They are not evidence about a model and are not listed in the validation catalogue.
* **Validation tests (`tests/validation/`)** gather evidence about the models and carry a V-number
  from `docs/test_catalogue.md` (V-01 to V-21): exact limits, convergence orders, no-arbitrage
  properties, comparison with an independent implementation, statistical agreement. Each has a
  stated criterion, and a status that is `passing` only when the test exists and passes.

Rules for validation tests:

* The pass criterion and seeds are fixed before the result is looked at. A failure is
  investigated, not "fixed" by changing the seed or the tolerance. A tolerance change needs a
  reason in the catalogue.
* A check that cannot fail proves nothing: where possible, include a control that must fail
  (V-04 reruns the 1993 formulation; V-05 shows a biased scheme is rejected; V-07 runs a naive
  reduction that differs between thread counts).
* An independent implementation in another language (Python, SciPy) supplies reference tables;
  it shares no code with C++. Tables are compared numerically.
* Hand mutation checks (change the code on purpose, see which tests fail) are recorded in the
  catalogue. They show which test would catch which kind of mistake, and where only one would.

CI enforces formatting, builds with four compilers, sanitizers (ASan, UBSan, TSan), clang-tidy
on the library, line coverage on `src/`, an install-and-consume check, the Python tests and a
Docker build that runs the tests. `test_catalogue_consistency.cpp` keeps the catalogue honest.

## Consequences

* More tests than a single pile, but each failure says what kind of problem it is.
* Statistical tests use bounds of several standard errors and fixed seeds, so they are
  deterministic; Debug and sanitizer builds use fewer paths and stay valid statements.
