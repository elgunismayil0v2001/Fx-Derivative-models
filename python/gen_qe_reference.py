"""Generate an independent reference for the C++ Heston QE paths.

The C++ class `HestonQuadraticExponential` (include/fxval/mc/heston_qe.hpp) implements Andersen's
Quadratic-Exponential scheme (Journal of Computational Finance 11(3), 2008) with the central
discretisation of ln S and the martingale correction. This script writes the same scheme again in
plain Python, in the arrangement of the paper (for example m = theta + (v - theta) e^{-kappa dt},
where C++ uses a sum of non-negative terms), and records terminal (spot, variance) for fixed
(seed, stream) pairs. `tests/validation/test_mc_qe_path_reference.cpp` compares.

What is shared with the C++ code: nothing in the scheme. The random numbers come from
`gen_mc_reference.normal_pair`, which is itself checked against the published Philox known answers
and against the C++ generator (MC-PATH-REF). What is NOT independent: both implementations were
written by the same author from the same paper, so a misreading of the paper would appear in both.
That is why V-06 also compares the scheme with the semi-analytic Heston price, and the unit tests
check the exact conditional mean and variance of the variance step.

Rows cover: both branches of the variance step (the table records how often each ran), the
fallback taken when the martingale-correction moment does not exist, antithetic paths (the
normals negated), psi_c at both ends of its range, 64-bit seeds and streams, one-step paths.

Usage:
    python python/gen_qe_reference.py tests/validation/data/mc_qe_path_reference.csv
"""

from __future__ import annotations

import csv
import math
import random
import sys
from dataclasses import dataclass
from pathlib import Path

from gen_mc_reference import normal_pair

SEED = 20261016
COLUMNS = [
    "seed",
    "stream",
    "antithetic",
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
    "psi_c",
    "final_spot",
    "final_variance",
    "quadratic_steps",
    "exponential_steps",
    "fallback_steps",
]


@dataclass
class QePath:
    spot: float
    variance: float
    quadratic_steps: int
    exponential_steps: int
    fallback_steps: int


def _phi_and_complement(z: float) -> tuple[float, float]:
    """Phi(z) and 1 - Phi(z), each accurate in its own tail."""
    return 0.5 * math.erfc(-z / math.sqrt(2.0)), 0.5 * math.erfc(z / math.sqrt(2.0))


def qe_path(
    seed: int,
    stream: int,
    antithetic: bool,
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
    psi_c: float,
) -> QePath:
    dt = t / steps
    e = math.exp(-kappa * dt)
    gamma1 = gamma2 = 0.5
    k0_plain = -rho * kappa * theta * dt / xi
    k1 = gamma1 * dt * (kappa * rho / xi - 0.5) - rho / xi
    k2 = gamma2 * dt * (kappa * rho / xi - 0.5) + rho / xi
    k3 = gamma1 * dt * (1.0 - rho * rho)
    k4 = gamma2 * dt * (1.0 - rho * rho)
    a_coef = k2 + 0.5 * k4
    sign = -1.0 if antithetic else 1.0

    log_s = math.log(spot)
    v = v0
    quad = expo = fallback = 0
    for n in range(steps):
        z1, z2 = normal_pair(seed, stream, n)
        z_v, z_perp = sign * z1, sign * z2

        m = theta + (v - theta) * e
        s2 = v * xi * xi * e * (1.0 - e) / kappa + theta * xi * xi * (1.0 - e) ** 2 / (2.0 * kappa)
        psi = s2 / (m * m)

        if psi <= psi_c:
            quad += 1
            b2 = 2.0 / psi - 1.0 + math.sqrt(2.0 / psi) * math.sqrt(2.0 / psi - 1.0)
            a = m / (1.0 + b2)
            v_next = a * (math.sqrt(b2) + z_v) ** 2
            d = 1.0 - 2.0 * a_coef * a
            log_m = None if d <= 0.0 else a_coef * b2 * a / d - 0.5 * math.log(d)
        else:
            expo += 1
            p = (psi - 1.0) / (psi + 1.0)
            beta = (1.0 - p) / m
            u, one_minus_u = _phi_and_complement(z_v)
            v_next = 0.0 if u <= p else math.log((1.0 - p) / one_minus_u) / beta
            log_m = None if a_coef >= beta else math.log(p + beta * (1.0 - p) / (beta - a_coef))

        if log_m is None:
            fallback += 1
            k0 = k0_plain
        else:
            k0 = -log_m - (k1 + 0.5 * k3) * v

        log_s += (
            (rd - rf) * dt + k0 + k1 * v + k2 * v_next + math.sqrt(k3 * v + k4 * v_next) * z_perp
        )
        v = v_next
    return QePath(math.exp(log_s), v, quad, expo, fallback)


def build_rows() -> list[dict[str, object]]:
    rng = random.Random(SEED)  # noqa: S311  (reproducible test inputs, not cryptography)
    rows: list[dict[str, object]] = []

    def add(seed, stream, antithetic, steps, spot, rd, rf, t, v0, kappa, theta, xi, rho, psi_c):
        path = qe_path(
            seed, stream, antithetic, steps, spot, rd, rf, t, v0, kappa, theta, xi, rho, psi_c
        )
        rows.append(
            {
                "seed": seed,
                "stream": stream,
                "antithetic": int(antithetic),
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
                "psi_c": psi_c,
                "final_spot": path.spot,
                "final_variance": path.variance,
                "quadratic_steps": path.quadratic_steps,
                "exponential_steps": path.exponential_steps,
                "fallback_steps": path.fallback_steps,
            }
        )

    big_seed = 0xDEADBEEFCAFEF00D
    # Boundary cases: big seed/stream, one step, v0 = 0 (starts in the exponential branch),
    # psi_c = 1 and 2, rho = +-1, a strongly positive rho with large steps (fallback), antithetic.
    add(big_seed, (1 << 33) + 5, False, 1, 1.1, 0.02, 0.01, 1.0, 0.04, 2.0, 0.04, 0.3, -0.5, 1.5)
    add(big_seed, (1 << 33) + 5, True, 1, 1.1, 0.02, 0.01, 1.0, 0.04, 2.0, 0.04, 0.3, -0.5, 1.5)
    add(0, 0, False, 10, 1.1, 0.0, 0.0, 1.0, 0.0, 1.0, 0.04, 0.3, 0.0, 1.5)
    add(7, 3, False, 8, 1.3, 0.03, 0.01, 2.0, 0.04, 0.5, 0.04, 1.0, -0.9, 1.0)
    add(7, 3, False, 8, 1.3, 0.03, 0.01, 2.0, 0.04, 0.5, 0.04, 1.0, -0.9, 2.0)
    add(7, 4, False, 20, 1.3, 0.03, 0.01, 2.0, 0.04, 2.0, 0.04, 0.5, 1.0, 1.5)
    add(7, 5, True, 20, 1.3, 0.03, 0.01, 2.0, 0.04, 2.0, 0.04, 0.5, -1.0, 1.5)
    add(11, 2, False, 2, 1.0, 0.0, 0.0, 5.0, 0.04, 1.0, 0.04, 0.2, 0.95, 1.5)
    add(big_seed, 2**64 - 1, False, 100, 1.0, 0.01, 0.0, 1.0, 0.09, 0.3, 0.09, 3.0, -0.9, 1.5)
    # Corner where the martingale-correction moment does not exist (long steps, huge xi, positive
    # rho): the scheme must take the uncorrected K0. Found by a random search; only these extreme
    # sets trigger it (time steps of about 4 years), so it never happens at realistic step sizes.
    for stream in range(4):
        add(1, stream, False, 2, 1.0, 0.0, 0.0, 8.84, 2.186, 5.098, 0.178, 1.667, 0.938, 1.5)
        add(
            2,
            stream,
            stream % 2 == 1,
            2,
            1.0,
            0.0,
            0.0,
            9.55,
            1.904,
            7.125,
            0.016,
            4.571,
            0.744,
            1.5,
        )
    while len(rows) < 160:
        seed = rng.getrandbits(64)
        stream = rng.getrandbits(rng.choice([8, 40, 64]))
        add(
            seed,
            stream,
            rng.random() < 0.3,
            rng.choice([1, 2, 4, 8, 16, 64]),
            round(rng.uniform(0.5, 2.0), 4),
            round(rng.uniform(-0.01, 0.08), 4),
            round(rng.uniform(-0.01, 0.08), 4),
            round(rng.uniform(0.05, 5.0), 4),
            round(rng.uniform(0.0, 0.25), 4),
            round(rng.uniform(0.1, 6.0), 4),
            round(rng.uniform(0.005, 0.25), 4),
            round(rng.uniform(0.05, 2.5), 4),
            round(rng.uniform(-0.95, 0.95), 3),
            rng.choice([1.0, 1.5, 1.5, 2.0]),
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
