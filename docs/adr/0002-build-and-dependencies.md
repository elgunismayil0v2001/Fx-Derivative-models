# ADR 0002: Build system and dependency management

* Status: accepted
* Date: 2026-10-09

## Context

The repository must build on a clean machine with one command, run the same checks locally and
in CI, and be usable as a library by another CMake project. The only third-party dependency in
week 1 is GoogleTest. More will come (Eigen, an optimiser, a JSON library, pybind11).

## Decision

* **CMake 3.25+ with `CMakePresets.json`** is the single entry point. Presets exist for debug,
  release, asan-ubsan, tsan, coverage and release-vcpkg. CI runs the same presets as a developer.
* **Dependencies are resolved in this order:** an installed or vcpkg package (`find_package`),
  then a pinned download with `FetchContent` when `FXVAL_FETCH_DEPS` is ON (the default).
  `vcpkg.json` lists the same dependencies, and the `release-vcpkg` preset uses it.
* The library is built as an **installable CMake package** exporting `fxval::fxval`. A separate
  project in `examples/consumer/` is built against the installed copy in CI.
* Sanitizer and coverage flags are applied to the whole build tree, because partial
  instrumentation produces false results.

## Alternatives considered

* **vcpkg only.** One source of truth, but it makes the first build depend on installing vcpkg.
  The FetchContent fallback keeps `git clone` followed by `cmake --preset release` working.
* **Conan.** Equivalent in capability. vcpkg was chosen because its manifest mode is simpler for a
  single-developer project.
* **Git submodules.** They pin versions but make clones and CI more fragile.

## Consequences

* There are two ways a dependency can arrive, so versions can drift. GoogleTest is pinned to
  v1.15.2 in `tests/CMakeLists.txt`. The vcpkg baseline is **not yet pinned** in `vcpkg.json`:
  do `vcpkg x-update-baseline` after the first vcpkg build and commit the result.
* The `release-vcpkg` preset has not been exercised yet.
* Each new dependency needs an entry in both `vcpkg.json` and the FetchContent fallback, or an
  explicit decision that it is vcpkg-only.
