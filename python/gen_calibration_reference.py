"""Generate independent reference tables for the Heston calibration code.

Two tables are written, both from Heston prices computed the TEXTBOOK way (the two Gil-Pelaez
probabilities P1 and P2 of Heston 1993, via python/gen_heston_reference.py and scipy QUADPACK),
not with the single Lewis integral used by the C++ pricer:

  heston_iv_reference.csv    Black-Scholes implied volatility of the Heston price at one strike.
                             Checks the whole price -> implied volatility chain of the C++ code
                             (pricer, out-of-the-money selection, Brent inversion).
  calibration_reference.csv  For random "true" Heston parameter sets and five expiries, the market
                             quotes (ATM, 25-delta risk reversal and butterfly) that the model
                             implies. The delta strikes are found by NESTED ROOT FINDING with
                             scipy brentq on the spot-form delta (python/gen_smile_reference.py),
                             not with the closed-form inverse used by the C++ code. The C++ test
                             calibrates to these quotes and must get the true parameters back.

Everything the C++ side computes is therefore checked against code that shares none of its
numerics: different integral, different integrator, different root finder, different language.

Design record (kept here because it explains a visible property of the table):
  * Seed 20261019, fixed before any calibration was run on the table.
  * The first version of the table drew every parameter independently and only 1 of 24 sets
    satisfied the Feller condition 2 kappa theta >= xi^2. A calibration test that almost never
    meets Feller says nothing about that regime, so the sampling was changed (before the C++ test
    was run on the table) to force the condition on half the sets: those with index % 4 in (0, 1).
    The others are unrestricted. In the committed table 16 of the 24 sets satisfy Feller and 8
    violate it, which is where FX smiles with large curvature tend to live.
  * Spot/forward delta alternates with the set index, so both conventions are exercised.

Usage:
    python python/gen_calibration_reference.py tests/validation/data

The prices come from numerical quadrature, so they can differ in the last digits between SciPy
versions: compare the tables numerically, not byte by byte. Generation takes about a minute.
"""

from __future__ import annotations

import csv
import math
import sys
from pathlib import Path

import numpy as np
from gen_heston_reference import heston_call_put
from gen_smile_reference import atm_by_root_find, strike_by_root_find
from scipy.optimize import brentq
from scipy.stats import norm

SEED = 20261019
SETS = 24
IV_CASES = 160
EXPIRIES = (1 / 12, 0.25, 0.5, 1.0, 2.0)
DELTA = 0.25

IV_COLUMNS = ["spot", "strike", "rd", "rf", "t", "v0", "kappa", "theta", "xi", "rho", "iv"]
CAL_COLUMNS = [
    "set",
    "convention",
    "spot",
    "rd",
    "rf",
    "v0",
    "kappa",
    "theta",
    "xi",
    "rho",
    "t",
    "atm",
    "rr",
    "bf",
]


def bs_price(kind: str, s: float, k: float, rd: float, rf: float, sigma: float, t: float) -> float:
    d1 = (math.log(s / k) + (rd - rf + 0.5 * sigma * sigma) * t) / (sigma * math.sqrt(t))
    d2 = d1 - sigma * math.sqrt(t)
    df_d, df_f = math.exp(-rd * t), math.exp(-rf * t)
    if kind == "call":
        return s * df_f * norm.cdf(d1) - k * df_d * norm.cdf(d2)
    return k * df_d * norm.cdf(-d2) - s * df_f * norm.cdf(-d1)


def heston_iv(spot, strike, rd, rf, t, v0, kappa, theta, xi, rho) -> float:
    """Implied volatility of the Heston price; the out-of-the-money option is inverted."""
    call, put, _ = heston_call_put(spot, strike, rd, rf, t, v0, kappa, theta, xi, rho)
    forward = spot * math.exp((rd - rf) * t)
    kind, price = ("call", call) if strike >= forward else ("put", put)
    return brentq(
        lambda sig: bs_price(kind, spot, strike, rd, rf, sig, t) - price,
        1e-4,
        4.0,
        xtol=1e-15,
        rtol=1e-14,
    )


def model_level(v0: float, kappa: float, theta: float, t: float) -> float:
    total = theta * t + (v0 - theta) * (1.0 - math.exp(-kappa * t)) / kappa
    return math.sqrt(total / t)


def _fixed_point(strike_of, spot, rd, rf, t, params) -> float:
    """sigma = IV(K(sigma)): the delta strike depends on the volatility at that strike."""
    level = model_level(params[0], params[1], params[2], t)

    def f(sig: float) -> float:
        return heston_iv(spot, strike_of(sig), rd, rf, t, *params) - sig

    return brentq(f, 0.2 * level, 5.0 * level, xtol=1e-14, rtol=1e-14)


def delta_quotes(convention, spot, rd, rf, t, params, delta=DELTA):
    """(atm, rr, bf) implied by the Heston parameters, or None if the strikes are not ordered."""
    atm = _fixed_point(
        lambda sig: atm_by_root_find(convention, spot, rd, rf, sig, t), spot, rd, rf, t, params
    )
    call = _fixed_point(
        lambda sig: strike_by_root_find("call", convention, delta, spot, rd, rf, sig, t),
        spot,
        rd,
        rf,
        t,
        params,
    )
    put = _fixed_point(
        lambda sig: strike_by_root_find("put", convention, delta, spot, rd, rf, sig, t),
        spot,
        rd,
        rf,
        t,
        params,
    )
    k_c = strike_by_root_find("call", convention, delta, spot, rd, rf, call, t)
    k_p = strike_by_root_find("put", convention, delta, spot, rd, rf, put, t)
    k_a = atm_by_root_find(convention, spot, rd, rf, atm, t)
    if not k_p < k_a < k_c:
        return None
    return atm, call - put, 0.5 * (call + put) - atm


def draw_set(rng: np.random.Generator, force_feller: bool):
    """One random 'true' parameter set and market. Resamples until Feller is as requested."""
    while True:
        spot = float(rng.uniform(0.8, 3.0))
        rd, rf = (float(v) for v in rng.uniform(-0.005, 0.06, size=2))
        v0 = float(rng.uniform(0.004, 0.06))
        theta = float(rng.uniform(0.004, 0.06))
        kappa = float(rng.uniform(0.5, 4.0))
        xi = float(rng.uniform(0.2, 1.0))
        rho = float(rng.uniform(-0.7, 0.7))
        if force_feller and not 2.0 * kappa * theta >= xi * xi:
            continue
        return spot, rd, rf, (v0, kappa, theta, xi, rho)


def build_iv_rows(rng: np.random.Generator, count: int) -> list[list[float]]:
    rows = []
    while len(rows) < count:
        spot, rd, rf, params = draw_set(rng, force_feller=False)
        t = float(rng.choice(EXPIRIES))
        forward = spot * math.exp((rd - rf) * t)
        level = model_level(params[0], params[1], params[2], t)
        strike = forward * math.exp(float(rng.uniform(-2.5, 2.5)) * level * math.sqrt(t))
        try:
            iv = heston_iv(spot, strike, rd, rf, t, *params)
        except ValueError:  # the price is outside what a volatility can express: skip, never fix
            continue
        rows.append([spot, strike, rd, rf, t, *params, iv])
    return rows


def build_calibration_rows(rng: np.random.Generator, sets: int) -> tuple[list[list], int]:
    rows: list[list] = []
    produced = 0
    skipped = 0
    while produced < sets:
        force = produced % 4 in (0, 1)
        spot, rd, rf, params = draw_set(rng, force_feller=force)
        convention = "spot" if produced % 2 == 0 else "forward"
        set_rows = []
        usable = True
        for t in EXPIRIES:
            try:
                quotes = delta_quotes(convention, spot, rd, rf, t, params)
            except ValueError:
                quotes = None
            if quotes is None:
                usable = False
                break
            set_rows.append([produced, convention, spot, rd, rf, *params, t, *quotes])
        if not usable:
            skipped += 1
            continue
        rows.extend(set_rows)
        produced += 1
    return rows, skipped


def _write(path: Path, columns: list[str], rows: list[list]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(columns)
        for row in rows:
            writer.writerow([x if isinstance(x, str | int) else repr(float(x)) for x in row])


def main(out_dir: str, sets: int = SETS, iv_cases: int = IV_CASES) -> None:
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(SEED)
    iv_rows = build_iv_rows(rng, iv_cases)
    cal_rows, skipped = build_calibration_rows(rng, sets)
    _write(out / "heston_iv_reference.csv", IV_COLUMNS, iv_rows)
    _write(out / "calibration_reference.csv", CAL_COLUMNS, cal_rows)
    print(f"wrote {len(iv_rows)} IV rows and {sets} calibration sets ({skipped} sets skipped)")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
