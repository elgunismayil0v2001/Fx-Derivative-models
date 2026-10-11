# Changelog

All notable changes are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

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
