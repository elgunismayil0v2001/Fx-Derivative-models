# fxval-price: command-line reference

`fxval-price` prices European FX options from a JSON file and writes a report that records how it
was made. It is built with the project (`cmake --preset release && cmake --build --preset release`)
and installed with `cmake --install`. The program is a front end to the library; nothing in it
adds financial logic.

```sh
build/release/apps/fxval-price --config configs/examples/gk_vanillas.json
build/release/apps/fxval-price --config configs/examples/heston_mc_qe.json --format csv --output out.csv
cat config.json | build/release/apps/fxval-price --config - --quiet
```

## Options

| Option | Meaning |
|---|---|
| `--config FILE` | The configuration (required). `-` reads standard input. Up to 16 MiB. |
| `--output FILE` | Write the report to FILE (through a temporary file and a rename). Default: standard output. |
| `--format json\|csv` | Report format. Default `json`. |
| `-v`, `--verbose` | Log each option to standard error. |
| `-q`, `--quiet` | Log nothing. |
| `--version`, `--help` | Print and exit with code 0. |

Standard output carries only the report. Log lines go to standard error. `--option=value` is
accepted as well as `--option value`.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Every option priced, report written. |
| 1 | Bad command line (usage is printed). Also: `SOURCE_DATE_EPOCH` is not whole seconds. |
| 2 | The configuration is not valid. Nothing was priced and no report was written. |
| 3 | The report was written, but at least one option could not be priced (its row says why). |
| 4 | A file could not be read or written. |
| 5 | Internal error (an unexpected exception). This is a bug; please report it. |

## Environment

`SOURCE_DATE_EPOCH`: seconds since 1970-01-01 UTC to record as the timestamp instead of the
current time. With it set, the same configuration and build give a byte-identical report.

## Configuration, format_version 1

Rates and volatilities are decimals (`0.03` is 3%, `0.08` is 8%); the parser rejects a rate above
100% and a volatility above 500%, which catches a percent typed as a decimal. Spot and strike are
domestic currency per one unit of foreign currency (EURUSD 1.10 means 1.10 USD per EUR). Expiries
are year fractions (ACT/365 fixed by convention: 30 days is `30/365`). Unknown keys, repeated keys
and missing fields are errors. A machine-readable summary is in `configs/schema/price_config.schema.json`;
the parser in `apps/lib/price_config.cpp` is the authority.

```json
{
  "format_version": 1,
  "market":  { "spot": 1.10, "domestic_rate": 0.03, "foreign_rate": 0.01 },
  "model":   { "type": "garman_kohlhagen", "volatility": 0.08 },
  "options": [
    { "id": "EURUSD-1Y-ATM-C", "type": "call", "strike": 1.10, "expiry_years": 1.0, "notional": 1000000 }
  ]
}
```

### `model`

| `type` | Further fields |
|---|---|
| `garman_kohlhagen` | `volatility` |
| `heston_semi_analytic` | `v0`, `kappa`, `theta`, `xi`, `rho` (variance, not volatility: `v0 = 0.04` is a 20% volatility) |
| `gbm_monte_carlo` | `volatility`, `monte_carlo` |
| `heston_monte_carlo` | `v0`, `kappa`, `theta`, `xi`, `rho`, `scheme` (`euler` or `qe`), `monte_carlo` |

`monte_carlo` fields:

| Field | Required | Meaning |
|---|---|---|
| `seed` | yes | Unsigned 64-bit integer. There is no default: a run must be reproducible. |
| `paths` | yes | Number of paths (pairs, when `antithetic` is true). 1 to 2,000,000,000. |
| `steps` | Heston only | Time steps per path. The GBM scheme is exact and takes no steps. |
| `threads` | no | Default 1; 0 means all hardware threads. **The result does not depend on it** (test V-07). |
| `block_size` | no | Paths per block (default 4096). Changes rounding in the last digits, not the law. |
| `antithetic` | Heston only | Average each path with its mirror image (default false). |
| `control_variate` | Heston only | Use the terminal spot as a control with its exact mean (default false). |
| `pilot_paths` | Heston only | Paths used to estimate the control coefficient (default 20,000). |

The `qe` scheme needs `xi > 0`. Every option in a run uses the same seed, so the options share
random numbers; differences between them are smoother than independent runs would give.

### `options`

At least one, at most 100,000. Each has `id`, `type` (`call` or `put`), `strike`, `expiry_years`
and optionally `notional` (units of foreign currency, default 1). An `id` is 1 to 64 characters
from `A-Z a-z 0-9 . _ : -`, starts with a letter or digit, and is unique within the file.

## Report

JSON (field order fixed): `format_version`, `metadata`, `inputs` (the validated configuration,
normalised), `summary` (`options`, `failed`), `results`.

Each result has `id`, `type`, `strike`, `expiry_years`, `notional`, `status` (`ok` or `error`) and
then either `price` (domestic currency per one unit of foreign), `present_value` (price times
notional), `standard_error` (Monte Carlo, per unit of foreign), `greeks` (Garman-Kohlhagen only)
and possibly a `note`, or an `error` message. Numbers are written with the shortest text that reads
back to exactly the same double.

CSV: `# key: value` comment lines with the metadata, a header row, one row per option; empty cells
mean "not applicable"; free text is quoted (RFC 4180).

### Metadata

| Field | Meaning |
|---|---|
| `fxval_version` | Library version. |
| `git_commit` | 12 hex digits of the commit that was built; `-dirty` if tracked files differed from it; `unknown` outside a git work tree. |
| `compiler`, `build_type` | How the program was built. |
| `config_file` | The path as the user gave it (`-` for standard input). |
| `config_sha256` | SHA-256 of the exact bytes of the configuration. `sha256sum config.json` gives the same value. |
| `timestamp_utc` | ISO 8601, UTC. |

For a Monte Carlo model the seed, paths, steps and threads are in `inputs.model.monte_carlo`.

## Examples

`configs/examples/` holds three configurations. They are also the inputs of the regression tests
(`tests/regression/expected/` holds the frozen results) and are checked against the JSON Schema.
