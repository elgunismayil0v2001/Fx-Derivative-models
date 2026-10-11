"""Generate an independent Garman-Kohlhagen reference table for the C++ validation tests.

Why this file exists: a test that checks the C++ code against more C++ code written by the same
person can share the same mistake. This script computes the same quantities in a different
language, with a different library for the normal distribution (scipy) and a different form of the
formulas (spot form, not the forward form the C++ code uses). The C++ tests then compare against
the table it writes.

Usage:
    python python/gen_gk_reference.py tests/validation/data/gk_reference.csv

The output is deterministic: a fixed seed and a fixed set of hand-picked boundary cases.
"""

from __future__ import annotations

import csv
import math
import sys
from pathlib import Path

import numpy as np
from scipy.stats import norm

SEED = 20261010
RANDOM_CASES = 300
COLUMNS = [
    "type",
    "spot",
    "strike",
    "rd",
    "rf",
    "sigma",
    "t",
    "price",
    "delta_spot",
    "delta_forward",
    "gamma",
    "vega",
    "vanna",
    "volga",
    "theta",
    "rho_d",
    "rho_f",
]

# (spot, strike, rd, rf, sigma, t): short expiry, long expiry, negative rates, high vol,
# deep in and out of the money.
BOUNDARY_CASES = [
    (1.0, 1.0, 0.0, 0.0, 0.10, 1.0),
    (1.0, 1.0, 0.03, 0.01, 0.08, 1.0 / 365.0),
    (4.3, 4.5, 0.055, 0.025, 0.12, 0.5),
    (1.1, 1.1, -0.005, 0.02, 0.07, 10.0),
    (1.1, 0.7, 0.02, 0.02, 0.09, 1.0),
    (1.1, 1.6, 0.02, 0.02, 0.09, 1.0),
    (1.0, 1.0, 0.01, 0.01, 1.50, 0.25),
    (100.0, 100.0, 0.05, 0.0, 0.20, 1.0),  # textbook Black-Scholes case
]


def gk_reference(kind: str, s: float, k: float, rd: float, rf: float, sigma: float, t: float):
    """Spot-form Garman-Kohlhagen price and Greeks. w = +1 for a call, -1 for a put."""
    w = 1.0 if kind == "call" else -1.0
    sq = math.sqrt(t)
    d1 = (math.log(s / k) + (rd - rf + 0.5 * sigma**2) * t) / (sigma * sq)
    d2 = d1 - sigma * sq
    edf, edd = math.exp(-rf * t), math.exp(-rd * t)
    pdf1 = norm.pdf(d1)
    price = w * (s * edf * norm.cdf(w * d1) - k * edd * norm.cdf(w * d2))
    vega = s * edf * pdf1 * sq
    return {
        "price": price,
        "delta_spot": w * edf * norm.cdf(w * d1),
        "delta_forward": w * norm.cdf(w * d1),
        "gamma": edf * pdf1 / (s * sigma * sq),
        "vega": vega,
        "vanna": -edf * pdf1 * d2 / sigma,
        "volga": vega * d1 * d2 / sigma,
        "theta": -s * edf * pdf1 * sigma / (2 * sq)
        + w * (rf * s * edf * norm.cdf(w * d1) - rd * k * edd * norm.cdf(w * d2)),
        "rho_d": w * k * t * edd * norm.cdf(w * d2),
        "rho_f": -w * s * t * edf * norm.cdf(w * d1),
    }


def build_cases() -> list[tuple[float, float, float, float, float, float]]:
    rng = np.random.default_rng(SEED)
    cases = list(BOUNDARY_CASES)
    for _ in range(RANDOM_CASES):
        s = float(rng.uniform(0.5, 5.0))
        sigma = float(rng.uniform(0.03, 0.60))
        t = float(rng.choice([1 / 52, 1 / 12, 0.25, 0.5, 1.0, 2.0, 5.0]))
        # Strike within about +-2.5 standard deviations of the spot, so no price is zero to 1e-300.
        k = s * math.exp(float(rng.uniform(-2.5, 2.5)) * sigma * math.sqrt(t))
        rd = float(rng.uniform(-0.01, 0.08))
        rf = float(rng.uniform(-0.01, 0.08))
        cases.append((s, k, rd, rf, sigma, t))
    return cases


def write_table(path: Path) -> int:
    path.parent.mkdir(parents=True, exist_ok=True)
    rows = 0
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(COLUMNS)
        for case in build_cases():
            for kind in ("call", "put"):
                ref = gk_reference(kind, *case)
                # float() matters: numpy 2 floats print as "np.float64(...)" under repr.
                writer.writerow(
                    [
                        kind,
                        *(repr(float(x)) for x in case),
                        *(repr(float(ref[c])) for c in COLUMNS[7:]),
                    ]
                )
                rows += 1
    return rows


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: gen_gk_reference.py OUTPUT.csv", file=sys.stderr)
        return 2
    rows = write_table(Path(argv[1]))
    print(f"wrote {rows} rows to {argv[1]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
