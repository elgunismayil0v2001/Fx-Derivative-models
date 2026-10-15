"""Generate an independent reference for the C++ Monte Carlo random numbers and Heston paths.

Why this file exists: a Monte Carlo price that agrees with the semi-analytic price (test V-05)
shows the simulation is right ON AVERAGE. It does not show that the random numbers are the ones
the design says they are, or that a single path follows the scheme exactly as written (a wrong
sign on one term can hide inside the statistical noise at low path counts). This script re-writes
the whole chain in plain Python integers and floats, with no code shared with C++:

  1. Philox4x32-10 (pure integer arithmetic),
  2. the mapping of 64 random bits (top 53 used) to a uniform in (0, 1), and Box-Muller,
  3. one Heston path under Euler with full truncation (Lord, Koekkoek, van Dijk 2010).

The C++ test `tests/validation/test_mc_path_reference.cpp` replays the same (seed, stream)
pairs and compares: the normals to 1e-14 relative, the paths to 1e-9 relative. (Integer
arithmetic is exact; only libm's log/cos/sin/exp can differ in the last digits between machines.)

Usage:
    python python/gen_mc_reference.py tests/validation/data/mc_path_reference.csv
"""

from __future__ import annotations

import csv
import math
import random
import sys
from pathlib import Path

MASK32 = 0xFFFFFFFF
M0, M1 = 0xD2511F53, 0xCD9E8D57
W0, W1 = 0x9E3779B9, 0xBB67AE85
SEED = 20261014
COLUMNS = [
    "seed",
    "stream",
    "steps",
    "spot",
    "rd",
    "rf",
    "t",
    "v0",
    "kappa",
    "theta",
    "xi",
    "rho",
    "z1_first",
    "z2_first",
    "final_spot",
    "final_variance",
]


def philox4x32_10(counter: tuple[int, int, int, int], key: tuple[int, int]) -> tuple[int, ...]:
    """Philox4x32 with 10 rounds, as in the Random123 paper (Salmon et al., SC11)."""
    c0, c1, c2, c3 = counter
    k0, k1 = key
    for _ in range(10):
        p0 = M0 * c0
        p1 = M1 * c2
        c0, c1, c2, c3 = (
            (p1 >> 32) ^ c1 ^ k0,
            p1 & MASK32,
            (p0 >> 32) ^ c3 ^ k1,
            p0 & MASK32,
        )
        k0 = (k0 + W0) & MASK32
        k1 = (k1 + W1) & MASK32
    return c0, c1, c2, c3


def _unit(low: int, high: int) -> float:
    word = (high << 32) | low
    return ((word >> 12) + 0.5) * 2.0**-52


def normal_pair(seed: int, stream: int, block: int) -> tuple[float, float]:
    """The two normals of block number `block` of stream `stream` (Box-Muller)."""
    counter = (block & MASK32, block >> 32, stream & MASK32, stream >> 32)
    bits = philox4x32_10(counter, (seed & MASK32, seed >> 32))
    u1 = _unit(bits[0], bits[1])
    u2 = _unit(bits[2], bits[3])
    radius = math.sqrt(-2.0 * math.log(u1))
    angle = 2.0 * math.pi * u2
    return radius * math.cos(angle), radius * math.sin(angle)


def heston_euler_path(
    seed: int,
    stream: int,
    steps: int,
    spot: float,
    rd: float,
    rf: float,
    t: float,
    v0: float,
    kappa: float,
    theta: float,
    xi: float,
    rho: float,
) -> tuple[float, float]:
    """Terminal (spot, variance) of one path, Euler full truncation, log-Euler for the spot."""
    dt = t / steps
    sqrt_dt = math.sqrt(dt)
    drift = (rd - rf) * dt
    rho_complement = math.sqrt(1.0 - rho * rho)
    log_spot = math.log(spot)
    variance = v0
    for n in range(steps):
        z_variance, z_independent = normal_pair(seed, stream, n)
        v_plus = max(variance, 0.0)
        root = math.sqrt(v_plus) * sqrt_dt
        z_spot = rho * z_variance + rho_complement * z_independent
        log_spot += drift - 0.5 * v_plus * dt + root * z_spot
        variance += kappa * (theta - v_plus) * dt + xi * root * z_variance
    return math.exp(log_spot), variance


def build_rows() -> list[dict[str, object]]:
    rng = random.Random(SEED)  # noqa: S311  (reproducible test inputs, not cryptography)
    rows: list[dict[str, object]] = []

    def add(seed, stream, steps, spot, rd, rf, t, v0, kappa, theta, xi, rho):
        z1, z2 = normal_pair(seed, stream, 0)
        s_t, v_t = heston_euler_path(
            seed, stream, steps, spot, rd, rf, t, v0, kappa, theta, xi, rho
        )
        rows.append(
            {
                "seed": seed,
                "stream": stream,
                "steps": steps,
                "spot": spot,
                "rd": rd,
                "rf": rf,
                "t": t,
                "v0": v0,
                "kappa": kappa,
                "theta": theta,
                "xi": xi,
                "rho": rho,
                "z1_first": z1,
                "z2_first": z2,
                "final_spot": s_t,
                "final_variance": v_t,
            }
        )

    # Boundary cases: 64-bit seeds and streams (both 32-bit halves in use), one step, v0 = 0,
    # xi = 0, rho = +-1 (no independent component), and a Feller-violating set that drives the
    # raw variance negative (the truncation branch).
    big_seed = 0xDEADBEEFCAFEF00D
    add(big_seed, (1 << 33) + 5, 1, 1.1, 0.02, 0.01, 1.0, 0.04, 2.0, 0.04, 0.3, -0.5)
    add(0, 0, 10, 1.1, 0.0, 0.0, 1.0, 0.0, 1.0, 0.04, 0.3, 0.0)
    add(7, 3, 50, 1.3, 0.03, 0.01, 2.0, 0.04, 2.0, 0.04, 0.0, -0.7)
    add(7, 4, 50, 1.3, 0.03, 0.01, 2.0, 0.04, 2.0, 0.04, 0.5, 1.0)
    add(7, 5, 50, 1.3, 0.03, 0.01, 2.0, 0.04, 2.0, 0.04, 0.5, -1.0)
    add(big_seed, 2**64 - 1, 200, 1.0, 0.01, 0.0, 1.0, 0.09, 0.3, 0.09, 3.0, -0.9)
    # Random cases over the usual FX range, including Feller violations.
    while len(rows) < 120:
        seed = rng.getrandbits(64)
        stream = rng.getrandbits(rng.choice([8, 40, 64]))
        steps = rng.choice([1, 2, 5, 20, 64, 250])
        add(
            seed,
            stream,
            steps,
            round(rng.uniform(0.5, 2.0), 4),
            round(rng.uniform(-0.01, 0.08), 4),
            round(rng.uniform(-0.01, 0.08), 4),
            round(rng.uniform(0.05, 5.0), 4),
            round(rng.uniform(0.0, 0.25), 4),
            round(rng.uniform(0.1, 6.0), 4),
            round(rng.uniform(0.005, 0.25), 4),
            round(rng.uniform(0.0, 2.5), 4),
            round(rng.uniform(-0.95, 0.95), 3),
        )
    return rows


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__)
        return 2
    path = Path(argv[1])
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(COLUMNS)
        for row in build_rows():
            writer.writerow(
                [repr(row[c]) if isinstance(row[c], float) else row[c] for c in COLUMNS]
            )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
