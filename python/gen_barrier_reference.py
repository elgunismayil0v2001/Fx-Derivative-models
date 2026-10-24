"""Generate an independent reference table for the barrier closed forms.

The C++ code uses the Reiner-Rubinstein formulas (blocks A, B, C, D), the first-passage probability
formula for touch options, and a sine-series for the double no-touch. This script does NOT use any
of those. Every price is a NUMERICAL INTEGRAL (scipy QUADPACK) of a quantity derived from the
transition law of Brownian motion with drift between absorbing barriers:

  * single-barrier vanilla: integral of the payoff against the killed density of ln S_T, which by
    the method of images is
        f(y) = g(y - x0 - nu T) - exp(2 nu (b - x0) / sigma^2) g(y - 2b + x0 - nu T),
    g the N(0, sigma^2 T) density, b the log barrier. Knock-in is vanilla minus knock-out here
    (integrated against the full density), so the C++ knock-in formulas, which are separate
    expressions, are checked against it and not against themselves;
  * one-touch: integral over [0, T] of the inverse-Gaussian first-passage density;
  * double no-touch: integral over the interval of the killed density written as a sum of images
    (a different representation from the eigenfunction series used in C++).

Usage:
    python python/gen_barrier_reference.py tests/validation/data/barrier_reference.csv

Seed 20261021. The values come from quadrature, so they can differ in the last digits between
SciPy versions: compare numerically.
"""

from __future__ import annotations

import csv
import math
import sys
from pathlib import Path

import numpy as np
from scipy import integrate

SEED = 20261021
VANILLA_CASES = 480  # 30 per (type, direction, effect) combination
TOUCH_CASES = 120
DNT_CASES = 100
COLUMNS = [
    "kind",
    "type",
    "direction",
    "effect",
    "spot",
    "strike",
    "barrier",
    "barrier2",
    "rd",
    "rf",
    "t",
    "vol",
    "price",
]


def _gauss(x: float, var: float) -> float:
    return math.exp(-0.5 * x * x / var) / math.sqrt(2.0 * math.pi * var)


def _quad(f, lo, hi, points=()):
    value, _ = integrate.quad(
        f,
        lo,
        hi,
        points=[p for p in points if lo < p < hi] or None,
        limit=500,
        epsabs=1e-14,
        epsrel=1e-12,
    )
    return value


def vanilla_payoff(kind: str, y: float, strike: float) -> float:
    s = math.exp(y)
    return max(s - strike, 0.0) if kind == "call" else max(strike - s, 0.0)


def vanilla_full(kind, s, k, rd, rf, t, vol) -> float:
    """Vanilla price by integrating the payoff against the lognormal density (no barrier)."""
    nu = rd - rf - 0.5 * vol * vol
    x0 = math.log(s)
    var = vol * vol * t
    width = 12.0 * math.sqrt(var)
    centre = x0 + nu * t
    f = lambda y: vanilla_payoff(kind, y, k) * _gauss(y - centre, var)  # noqa: E731
    return math.exp(-rd * t) * _quad(f, centre - width, centre + width, [math.log(k)])


def knock_out(kind, direction, s, k, b_level, rd, rf, t, vol) -> float:
    nu = rd - rf - 0.5 * vol * vol
    x0, b = math.log(s), math.log(b_level)
    var = vol * vol * t
    image_weight = math.exp(2.0 * nu * (b - x0) / (vol * vol))

    def density(y: float) -> float:
        return _gauss(y - x0 - nu * t, var) - image_weight * _gauss(y - 2.0 * b + x0 - nu * t, var)

    width = 12.0 * math.sqrt(var)
    centre = x0 + nu * t
    if direction == "down":
        lo, hi = b, max(centre, b) + width
    else:
        lo, hi = min(centre, b) - width, b
    f = lambda y: vanilla_payoff(kind, y, k) * density(y)  # noqa: E731
    return math.exp(-rd * t) * _quad(f, lo, hi, [math.log(k)])


def vanilla_barrier(kind, direction, effect, s, k, b_level, rd, rf, t, vol) -> float:
    ko = knock_out(kind, direction, s, k, b_level, rd, rf, t, vol)
    return ko if effect == "knock_out" else vanilla_full(kind, s, k, rd, rf, t, vol) - ko


def one_touch(direction, s, b_level, rd, rf, t, vol) -> float:
    nu = rd - rf - 0.5 * vol * vol
    x = math.log(s / b_level) if direction == "down" else math.log(b_level / s)
    sign = 1.0 if direction == "down" else -1.0

    def first_passage(u: float) -> float:
        return (
            x
            / (vol * math.sqrt(2.0 * math.pi * u**3))
            * math.exp(-((x + sign * nu * u) ** 2) / (2.0 * vol * vol * u))
        )

    return math.exp(-rd * t) * _quad(first_passage, 0.0, t)


def double_no_touch(s, lower, upper, rd, rf, t, vol) -> float:
    nu = rd - rf - 0.5 * vol * vol
    w = math.log(upper / lower)
    x0 = math.log(s / lower)
    var = vol * vol * t
    reach = 60

    def density(y: float) -> float:
        images = 0.0
        for k in range(-reach, reach + 1):
            images += _gauss(y - x0 + 2.0 * k * w, var) - _gauss(y + x0 + 2.0 * k * w, var)
        girsanov = math.exp(nu * (y - x0) / (vol * vol) - nu * nu * t / (2.0 * vol * vol))
        return girsanov * images

    return math.exp(-rd * t) * _quad(density, 0.0, w, [x0])


def make_market(rng: np.random.Generator):
    s = float(rng.uniform(0.8, 3.0))
    rd, rf = (float(v) for v in rng.uniform(-0.005, 0.06, size=2))
    t = float(rng.uniform(0.08, 2.0))
    vol = float(rng.uniform(0.05, 0.35))
    return s, rd, rf, t, vol


def build_rows() -> list[list]:
    rng = np.random.default_rng(SEED)
    rows: list[list] = []
    combos = [
        (k, d, e)
        for k in ("call", "put")
        for d in ("down", "up")
        for e in ("knock_out", "knock_in")
    ]
    per = VANILLA_CASES // len(combos)
    for kind, direction, effect in combos:
        for _ in range(per):
            s, rd, rf, t, vol = make_market(rng)
            sd = vol * math.sqrt(t)
            u = float(rng.uniform(0.3, 2.5))
            b = s * math.exp((-u if direction == "down" else u) * sd)
            fwd = s * math.exp((rd - rf) * t)
            k = fwd * math.exp(float(rng.uniform(-1.5, 1.5)) * sd)
            price = vanilla_barrier(kind, direction, effect, s, k, b, rd, rf, t, vol)
            rows.append(["vanilla", kind, direction, effect, s, k, b, 0.0, rd, rf, t, vol, price])
    for i in range(TOUCH_CASES):
        s, rd, rf, t, vol = make_market(rng)
        direction = "down" if i % 2 == 0 else "up"
        u = float(rng.uniform(0.3, 2.5))
        b = s * math.exp((-u if direction == "down" else u) * vol * math.sqrt(t))
        rows.append(
            [
                "touch",
                "",
                direction,
                "",
                s,
                0.0,
                b,
                0.0,
                rd,
                rf,
                t,
                vol,
                one_touch(direction, s, b, rd, rf, t, vol),
            ]
        )
    for _ in range(DNT_CASES):
        s, rd, rf, t, vol = make_market(rng)
        sd = vol * math.sqrt(t)
        lo = s * math.exp(-float(rng.uniform(0.3, 2.5)) * sd)
        hi = s * math.exp(float(rng.uniform(0.3, 2.5)) * sd)
        rows.append(
            [
                "dnt",
                "",
                "",
                "",
                s,
                0.0,
                lo,
                hi,
                rd,
                rf,
                t,
                vol,
                double_no_touch(s, lo, hi, rd, rf, t, vol),
            ]
        )
    return rows


def main(path: str) -> None:
    out = Path(path)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(COLUMNS)
        for row in build_rows():
            writer.writerow([x if isinstance(x, str) else repr(float(x)) for x in row])


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
