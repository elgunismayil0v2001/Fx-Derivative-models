# Contributing

This is a single-maintainer project, but it is run like a team project so that the history shows
how decisions were made.

## Workflow

1. Branch from `main` (`feature/<topic>` or `fix/<topic>`). Do not commit to `main` directly.
2. Keep changes small and focused. One logical change per pull request.
3. Use conventional commit messages: `feat:`, `fix:`, `test:`, `docs:`, `build:`, `ci:`,
   `refactor:`, `perf:`.
4. Open a pull request. CI must be green before merging.
5. A change that adds or alters a model, tolerance or error behaviour updates the relevant
   document (`docs/model_documentation.md`, `docs/test_catalogue.md`, or an ADR) in the same PR.

## Before you push

```sh
pip install pre-commit && pre-commit install   # once
scripts/check.sh                                # format, build, tests, sanitizers, tidy, python
```

## Rules for code

* Follow `docs/adr/0001-error-policy.md`: `Result<T>` at module boundaries, `noexcept` kernels,
  contract checks for programmer errors, never NaN as a failure signal.
* Use the typed units for market quantities. Do not pass raw `double` volatilities across APIs.
* No `-ffast-math`, no global mutable state, no unseeded randomness.
* Every error path has a test. Every claim in a document is backed by a test or a figure.
* A tolerance change needs a reason in `docs/test_catalogue.md`.
