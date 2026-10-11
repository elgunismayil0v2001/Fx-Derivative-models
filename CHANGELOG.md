# Changelog

All notable changes are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Independent Python reference for Garman-Kohlhagen (`python/gen_gk_reference.py`, 616-row table)
  and a C++ test comparing against it; CI regenerates the table.
- V-01 on 8000 markets and V-02 on 120 points with 5-point stencils, recording worst errors.
- Unit tests for degenerate inputs and error paths. ADR 0003 (testing strategy), `docs/repo_guide.md`.

### Changed

- `garman_kohlhagen_greeks` returns `invalid_argument` when volatility or expiry is zero, instead
  of zeros (this also removes a wrong-sign put theta in the old zero-volatility branch).
- `.clang-format` now sets `SpacesBeforeTrailingComments: 2`, and all sources were reformatted so
  the CI format job passes. Build output and `.DS_Store` files are no longer tracked.

- Garman-Kohlhagen European FX price and Greeks, `Spot` unit, and ACT/365 fixed year fractions.
  Validation tests V-01 (put-call parity) and V-02 (Greeks vs finite differences).
- GitHub Actions CI, `.clang-format`, `.clang-tidy`, `.pre-commit-config.yaml`, and `.gitignore`.
- CMake build with presets (debug, release, asan-ubsan, tsan, coverage, release-vcpkg), install
  and export of `fxval::fxval`, and an external consumer example.
- `Result<T>` error type, contract checks, and typed units (`Volatility`, `Rate`, `Years`,
  `Strike`, `Notional`) with domain validation. ADR 0001 records the error policy.
- Unit tests and a test that keeps `docs/test_catalogue.md` consistent (V-01 to V-21 listed once).
- `python/data_download.py` for NBP, ECB and FRED data with provenance files, and offline tests.
- Documentation skeleton: model documentation draft (Garman-Kohlhagen derivation), test
  catalogue, architecture, ADR template, ADR 0001 and ADR 0002.
