"""Generate an independent reference table for the FX smile code (delta quotes to strikes, quadratic
smile, butterfly density factor) for the C++ validation tests.

Why this file exists: the C++ code turns a delta into a strike with the closed form
K = F exp(-sigma sqrt(T) d1 + sigma^2 T / 2), d1 = N^-1(delta). This script does NOT use that
form. It finds each strike by ROOT FINDING on the spot-form Garman-Kohlhagen delta (scipy brentq),
fits the smile by solving a 3x3 Vandermonde system with numpy (the C++ uses divided differences),
and differentiates the polynomial with numpy.polyder. Only the Gatheral density-factor formula is
shared with the C++ code (same paper); the pytest file checks that formula against the second
difference of option prices, which does not depend on it.

Usage:
    python python/gen_smile_reference.py tests/validation/data/smile_reference.csv
"""

from __future__ import annotations

import csv
import math
import sys
from pathlib import Path

import numpy as np
from scipy.optimize import brentq
from scipy.stats import norm

SEED = 20261018
RANDOM_CASES = 250
PROBES = (-2.5, -1.0, 0.0, 1.0, 2.5)  # standardised log-moneyness x = k / (sigma_atm sqrt(T))

COLUMNS = (
    ["convention", "spot", "rd", "rf", "t", "atm", "rr", "bf", "delta"]
    + ["sigma_put", "sigma_call", "strike_put", "strike_atm", "strike_call"]
    + [f"vol_{j}" for j in range(len(PROBES))]
    + [f"g_{j}" for j in range(len(PROBES))]
)

# (convention, spot, rd, rf, t, atm, rr, bf, delta)
BOUNDARY_CASES = [
    ("spot", 1.10, 0.04, 0.025, 1.0, 0.08, 0.0, 0.0, 0.25),  # flat smile
    ("forward", 1.10, 0.04, 0.025, 1.0, 0.08, 0.0, 0.0, 0.25),
    ("spot", 1.10, 0.04, 0.025, 7 / 365, 0.065, -0.0035, 0.0012, 0.25),  # one week
    ("spot", 1.10, 0.04, 0.025, 2.0, 0.08, -0.0085, 0.003, 0.25),  # two years
    ("forward", 4.3, 0.055, 0.025, 0.5, 0.11, 0.012, 0.005, 0.10),  # 10-delta quotes, USD/PLN-like
    ("spot", 150.0, 0.0, 0.05, 1.0, 0.10, -0.02, 0.004, 0.25),  # high foreign rate
]


def gk_delta_spot_form(kind: str, convention: str, s, k, rd, rf, sigma, t) -> float:
    """Delta from the spot form of Garman-Kohlhagen (no forward is used)."""
    d1 = (math.log(s / k) + (rd - rf + 0.5 * sigma**2) * t) / (sigma * math.sqrt(t))
    discount = math.exp(-rf * t) if convention == "spot" else 1.0
    return discount * norm.cdf(d1) if kind == "call" else -discount * norm.cdf(-d1)


def strike_by_root_find(kind, convention, delta_abs, s, rd, rf, sigma, t) -> float:
    """Strike with the requested absolute delta, found numerically."""
    target = delta_abs if kind == "call" else -delta_abs
    fwd = s * math.exp((rd - rf) * t)
    width = 12.0 * sigma * math.sqrt(t) + 1.0

    def f(log_k: float) -> float:
        return gk_delta_spot_form(kind, convention, s, math.exp(log_k), rd, rf, sigma, t) - target

    return math.exp(brentq(f, math.log(fwd) - width, math.log(fwd) + width, xtol=1e-15, rtol=1e-15))


def atm_by_root_find(convention, s, rd, rf, sigma, t) -> float:
    """Delta-neutral straddle strike: call delta + put delta = 0."""
    fwd = s * math.exp((rd - rf) * t)
    width = 12.0 * sigma * math.sqrt(t) + 1.0

    def f(log_k: float) -> float:
        k = math.exp(log_k)
        return gk_delta_spot_form("call", convention, s, k, rd, rf, sigma, t) + gk_delta_spot_form(
            "put", convention, s, k, rd, rf, sigma, t
        )

    return math.exp(brentq(f, math.log(fwd) - width, math.log(fwd) + width, xtol=1e-15, rtol=1e-15))


def smile_polynomial(ks: list[float], vols: list[float]) -> np.ndarray:
    """Coefficients (highest power first) of the quadratic through three points."""
    vandermonde = np.vander(np.array(ks), 3)
    return np.linalg.solve(vandermonde, np.array(vols))


def density_factor(coeffs: np.ndarray, k: float, t: float) -> float | None:
    """Gatheral's g(k); None where the smile polynomial is not positive."""
    sig = float(np.polyval(coeffs, k))
    if sig <= 0.0:
        return None
    dsig = float(np.polyval(np.polyder(coeffs), k))
    d2sig = float(np.polyval(np.polyder(coeffs, 2), k))
    w = sig * sig * t
    dw = 2.0 * sig * dsig * t
    d2w = 2.0 * t * (dsig * dsig + sig * d2sig)
    return (1.0 - k * dw / (2.0 * w)) ** 2 - 0.25 * dw * dw * (1.0 / w + 0.25) + 0.5 * d2w


def smile_case(convention, s, rd, rf, t, atm, rr, bf, delta):
    """All reference quantities for one case, or None if the quotes are not usable."""
    sig_c = atm + bf + 0.5 * rr
    sig_p = atm + bf - 0.5 * rr
    if sig_c <= 0.0 or sig_p <= 0.0:
        return None
    k_p = strike_by_root_find("put", convention, delta, s, rd, rf, sig_p, t)
    k_a = atm_by_root_find(convention, s, rd, rf, atm, t)
    k_c = strike_by_root_find("call", convention, delta, s, rd, rf, sig_c, t)
    if not k_p < k_a < k_c:
        return None
    fwd = s * math.exp((rd - rf) * t)
    log_ks = [math.log(k / fwd) for k in (k_p, k_a, k_c)]
    coeffs = smile_polynomial(log_ks, [sig_p, atm, sig_c])
    scale = atm * math.sqrt(t)
    vols, gs = [], []
    for x in PROBES:
        k = x * scale
        vol = float(np.polyval(coeffs, k))
        g = density_factor(coeffs, k, t)
        if g is None:
            return None
        vols.append(vol)
        gs.append(g)
    return [sig_p, sig_c, k_p, k_a, k_c, *vols, *gs]


def build_cases() -> list[tuple]:
    rng = np.random.default_rng(SEED)
    cases = list(BOUNDARY_CASES)
    while len(cases) < len(BOUNDARY_CASES) + RANDOM_CASES:
        convention = "spot" if rng.random() < 0.5 else "forward"
        s = float(math.exp(rng.uniform(math.log(0.5), math.log(150.0))))
        rd, rf = (float(v) for v in rng.uniform(-0.01, 0.08, size=2))
        t = float(math.exp(rng.uniform(math.log(7 / 365), math.log(5.0))))
        atm = float(rng.uniform(0.04, 0.35))
        rr = float(rng.uniform(-0.25, 0.25) * atm)
        bf = float(rng.uniform(0.0, 0.1) * atm)
        delta = 0.25 if rng.random() < 0.6 else 0.10
        case = (convention, s, rd, rf, t, atm, rr, bf, delta)
        if smile_case(*case) is not None:
            cases.append(case)
    return cases


def main(path: str) -> None:
    out = Path(path)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(COLUMNS)
        for case in build_cases():
            row = smile_case(*case)
            assert row is not None  # noqa: S101 - build_cases only keeps usable cases
            writer.writerow([case[0], *(repr(float(v)) for v in case[1:]), *(repr(v) for v in row)])


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
