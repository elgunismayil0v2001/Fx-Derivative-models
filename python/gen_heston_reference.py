"""Generate an independent Heston reference table for the C++ validation tests.

Why this file exists: the C++ pricer evaluates ONE Fourier integral (Lewis 2001) with its own
adaptive Gauss-Kronrod routine. This script prices the same options the textbook way instead:
the two probabilities P1 and P2 of Heston (1993), each a Gil-Pelaez integral, with the original
parametrisation of the characteristic function (of ln S, not ln(S/F)) and scipy's QUADPACK
integrator. Different integral, different parametrisation, different integrator, different
language. Agreement between the two is evidence that both compute the same mathematical object.

The table only holds well-behaved parameters (xi >= 0.05, moderate maturities) where the textbook
formula is itself accurate. Robustness at extreme parameters is tested separately in C++ (V-04).

Usage:
    python python/gen_heston_reference.py tests/validation/data/heston_reference.csv

The inputs are deterministic (fixed seed). The prices come from numerical quadrature, so they can
differ in the last digits between SciPy versions: compare the table numerically, not byte by byte.
"""

from __future__ import annotations

import csv
import math
import sys
import warnings
from pathlib import Path

import numpy as np
from scipy import integrate

SEED = 20261012
RANDOM_CASES = 400
STRESS_CASES = 200  # strong correlation and vol of vol: rho * xi > kappa, Feller often violated
MAX_QUAD_ERROR = 1e-11  # rows where scipy cannot certify its own integral are dropped
COLUMNS = ["spot", "strike", "rd", "rf", "t", "v0", "kappa", "theta", "xi", "rho", "call", "put"]


def _probability(
    j: int,
    x: float,
    ln_k: float,
    rd: float,
    rf: float,
    tau: float,
    v0: float,
    kappa: float,
    theta: float,
    xi: float,
    rho: float,
) -> tuple[float, float]:
    """P_j of Heston (1993), the 'little trap' form of the characteristic function.

    Returns the probability and scipy's own error estimate of the integral."""
    u_j = 0.5 if j == 1 else -0.5
    b_j = kappa - rho * xi if j == 1 else kappa

    def integrand(u: float) -> float:
        iu = 1j * u
        a = b_j - rho * xi * iu
        d = np.sqrt(a * a - xi * xi * (2.0 * u_j * iu - u * u))
        g = (a - d) / (a + d)
        e = np.exp(-d * tau)
        c = (rd - rf) * iu * tau + kappa * theta / xi**2 * (
            (a - d) * tau - 2.0 * np.log((1.0 - g * e) / (1.0 - g))
        )
        dd = (a - d) / xi**2 * (1.0 - e) / (1.0 - g * e)
        f = np.exp(c + dd * v0 + iu * x)
        return float(np.real(np.exp(-iu * ln_k) * f / iu))

    with warnings.catch_warnings():
        warnings.simplefilter("ignore", integrate.IntegrationWarning)
        value, error = integrate.quad(
            integrand, 0.0, np.inf, limit=1000, epsabs=1e-13, epsrel=1e-12
        )
    return 0.5 + value / math.pi, error / math.pi


def heston_call_put(spot, strike, rd, rf, tau, v0, kappa, theta, xi, rho):
    """Call, put and an error estimate from P1 and P2; the put uses its own formula, not parity."""
    x, ln_k = math.log(spot), math.log(strike)
    args = (x, ln_k, rd, rf, tau, v0, kappa, theta, xi, rho)
    p1, err1 = _probability(1, *args)
    p2, err2 = _probability(2, *args)
    df_f, df_d = math.exp(-rf * tau), math.exp(-rd * tau)
    call = spot * df_f * p1 - strike * df_d * p2
    put = strike * df_d * (1.0 - p2) - spot * df_f * (1.0 - p1)
    return call, put, max(err1, err2) * max(spot, strike)


def make_inputs(rng: np.random.Generator) -> list[tuple[float, ...]]:
    rows: list[tuple[float, ...]] = []
    for _ in range(RANDOM_CASES):
        spot = float(rng.uniform(0.8, 5.0))
        rd = float(rng.uniform(-0.01, 0.08))
        rf = float(rng.uniform(-0.01, 0.08))
        tau = float(rng.choice([1 / 12, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0]))
        v0 = float(rng.uniform(0.0025, 0.36))
        theta = float(rng.uniform(0.0025, 0.36))
        kappa = float(rng.uniform(0.3, 5.0))
        xi = float(rng.uniform(0.05, 1.5))
        rho = float(rng.uniform(-0.9, 0.9))
        sd = math.sqrt(0.5 * (v0 + theta) * tau)
        forward = spot * math.exp((rd - rf) * tau)
        strike = forward * math.exp(float(rng.uniform(-2.5, 2.5)) * sd)
        rows.append((spot, strike, rd, rf, tau, v0, kappa, theta, xi, rho))
    return rows


def make_stress_inputs(rng: np.random.Generator) -> list[tuple[float, ...]]:
    rows: list[tuple[float, ...]] = []
    for _ in range(STRESS_CASES):
        spot = float(rng.uniform(0.8, 5.0))
        rd = float(rng.uniform(-0.01, 0.08))
        rf = float(rng.uniform(-0.01, 0.08))
        tau = float(rng.choice([0.25, 1.0, 2.0, 5.0]))
        v0 = float(rng.uniform(0.01, 0.25))
        theta = float(rng.uniform(0.01, 0.25))
        kappa = float(rng.uniform(0.05, 0.4))
        xi = float(rng.uniform(1.0, 3.0))
        rho = float(rng.choice([-1.0, 1.0]) * rng.uniform(0.5, 0.95))
        sd = math.sqrt(0.5 * (v0 + theta) * tau)
        forward = spot * math.exp((rd - rf) * tau)
        strike = forward * math.exp(float(rng.uniform(-2.0, 2.0)) * sd)
        rows.append((spot, strike, rd, rf, tau, v0, kappa, theta, xi, rho))
    return rows


def write_table(path: Path) -> int:
    rng = np.random.default_rng(SEED)
    written = 0
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(COLUMNS)
        for row in make_inputs(rng) + make_stress_inputs(rng):
            call, put, quad_error = heston_call_put(*row)
            if quad_error > MAX_QUAD_ERROR:
                continue
            # Keep only rows where the reference is self-consistent: prices inside the
            # no-arbitrage bounds. A failing row is skipped, never silently "fixed".
            spot, strike, rd, rf, tau = row[:5]
            if not (
                0.0 <= call <= spot * math.exp(-rf * tau)
                and 0.0 <= put <= strike * math.exp(-rd * tau)
            ):
                continue
            writer.writerow([repr(x) for x in (*row, call, put)])
            written += 1
    return written


def main() -> None:
    if len(sys.argv) != 2:
        print("usage: gen_heston_reference.py OUTPUT.csv", file=sys.stderr)
        raise SystemExit(2)
    count = write_table(Path(sys.argv[1]))
    print(f"wrote {count} rows to {sys.argv[1]}")


if __name__ == "__main__":
    main()
