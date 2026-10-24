# ADR 0007: Command-line program, configuration format and run metadata

* Status: accepted
* Date: 2026-10-17

## Context

At first the library could only be used from C++ code. A validation team, or anyone reviewing
the project, also needs to run a model from a file and get a result they can audit: which build,
which input and which seed produced this number? The plan (section 6A D) asks for a command-line
program, a validated and versioned configuration, run metadata on every output and documented
exit codes.

## Decision

* **The library stays free of third-party dependencies; only the apps use a JSON parser.** The
  programs live in `apps/`, are built into a static library `fxval_app` that is not installed or
  exported, and link `nlohmann/json` privately. A consumer of `fxval::fxval` does not need it.
  `nlohmann/json` is taken from an installed copy or vcpkg if present, else downloaded at a pinned
  tag (same mechanism as GoogleTest). It can be switched off with `-DFXVAL_BUILD_APPS=OFF`.
* **All program behaviour is a function with injected streams** (`run_price_cli`), and `main` is
  ten lines. Tests call the function directly (fast, no processes); one test script runs the
  real executable end to end.
* **Strict configuration.** A key that is not in the format, a repeated key, a missing field, a
  wrong type or a value outside its domain is an error. Nothing is defaulted silently, and in
  particular the Monte Carlo seed is required: a result nobody can reproduce is not a result.
  Every message begins with the path of the field (`options[2].strike: ...`). nlohmann/json keeps
  the last of two equal keys without complaint, so a first pass with a SAX handler looks for
  repeats and bounds the nesting depth.
* **Typed units are reused for validation.** The parser builds `Spot`, `Rate`, `Volatility`,
  `Strike`, `Years`, `Notional` and `HestonParams`; their own domain checks (for example a rate of
  3 instead of 0.03) produce the error, prefixed with the field path.
* **Versioned format.** `format_version` is required and must equal 1. A future change that is not
  backward compatible gets a new number; the program refuses numbers it does not know.
* **A failure in one option does not discard the others.** The report holds every option with a
  status; the exit code (3) tells scripts that at least one failed. A configuration that is not
  valid prices nothing (exit 2).
* **Exit codes:** 0 success, 1 bad command line, 2 invalid configuration, 3 partial pricing
  failure (report still written), 4 file error, 5 internal error. Documented in `--help` and in
  `docs/cli.md`; the help text is tested.
* **Run metadata on every report** (JSON `metadata`, CSV `# key: value` lines): library version,
  git commit (with `-dirty` when tracked files differ from HEAD), compiler, build type, SHA-256 of
  the exact bytes of the configuration, UTC timestamp. The commit is recorded at build time, not
  configure time, so it is the commit that was built. The timestamp comes from the system clock,
  or from `SOURCE_DATE_EPOCH` when set (the reproducible-builds convention); with the variable
  set, the same input gives a byte-identical report. The report also echoes the validated inputs
  in normalised form, so an output file describes itself.
* **Reports are written to a temporary file and renamed**, so a failed run never leaves a
  half-written report under the final name.
* **Logging:** a 40-line levelled logger writing to standard error. The program logs a handful
  of lines per run, never inside a loop, and the library does not log at all (it returns errors).
  `spdlog` was named in the plan and left out: it would be a second dependency to carry for one
  line of output per option. Revisit if the apps start to log inside long-running calibrations.
* **A replaceable pricer exists for tests only.** The library does not fail on any valid
  configuration we could find, so the failure paths (per-option error, exit codes 3 and 5) could
  not be reached with real input. `run_price` takes an optional function that prices one option;
  tests inject one that fails or throws. Production code never sets it.

## Consequences

* A report can be traced to code, input and seed; a reviewer can recompute the hash of the
  configuration with any SHA-256 tool (the end-to-end test does, with CMake's own).
* Adding a model means one more alternative in `ModelSpec`, one reader function and one runner
  function; the report writers and the CLI do not change.
* The JSON Schema in `configs/schema/` is documentation for editors and CI. The C++ parser is the
  authority and is stricter (repeated keys, duplicate ids). A Python test keeps the schema from
  drifting by checking the examples against it and checking that it rejects damaged copies.
* The git commit is `unknown` when built from a source archive, and `-dirty` for a working tree
  with uncommitted tracked changes. Both are visible in the output rather than hidden.

## Not done in this ADR

Fuzzing the parser with libFuzzer (done: the `price_config` target, see `docs/fuzzing.md`; a randomized damage test also runs in
every build), Greeks for Heston, and calibration, VaR and CVA programs.
