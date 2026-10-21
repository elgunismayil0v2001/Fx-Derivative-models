"""Write the market-risk reference tables for RISK-REF (VaR methods and backtest statistics).

Usage: python python/gen_risk_reference.py tests/validation/data

Writes:

* risk_hits_reference.csv  id,level,n,hits,exceptions,kupiec_lr,kupiec_p,ind_lr,ind_p,
  cc_lr,cc_p,cdf,zone
* risk_var_reference.csv   kind,a,b,c,d,level,var,es   (columns are explained per kind below)

Independence: nothing here uses the C++ library. The statistics are computed from the
definitions in include/fxval/risk/backtest.hpp with numpy and scipy (chi2.sf, binom.cdf); the
VaR numbers use numpy sorting, scipy's normal quantile, Gauss-Hermite quadrature for the moments
of a quadratic form and a separate Garman-Kohlhagen pricer. Seed 20261025.

Kinds in risk_var_reference.csv (a..d hold, for each kind):
  hist  a = P&L scenarios joined by ';'     -> var, es (k = ceil(n q), k-th worst)
  norm  a = mean, b = sd                    -> var, es
  cf    a = mean, b = sd, c = skewness, d = excess kurtosis -> var (Cornish-Fisher)
  dg    a = portfolio, b = sigma            -> var (delta-gamma: Cornish-Fisher on the
                                               quadratic form), es (quantile grid)
  full  a = portfolio, b = sigma            -> var, es (full revaluation, 100000-point
                                               quantile grid)
  portfolio text: "S0|rd|rf|spot_units|type:strike:T:vol:qty;type:strike:T:vol:qty;..."
"""

from __future__ import annotations

import csv
import math
import sys
from pathlib import Path

import numpy as np
from scipy.stats import binom, chi2, norm

SEED = 20261025
STAT_KEYS = ("kupiec_lr", "kupiec_p", "ind_lr", "ind_p", "cc_lr", "cc_p", "cdf")


# ----------------------------------------------------------------------------- backtest statistics
def xlogy(x: float, y: float) -> float:
    return 0.0 if x == 0 else x * math.log(y)


def backtest(hits: list[int], level: float) -> dict[str, float | str]:
    n, x = len(hits), sum(hits)
    p = 1.0 - level
    phat = x / n
    lr_pof = -2.0 * (xlogy(n - x, 1 - p) + xlogy(x, p)) + 2.0 * (
        xlogy(n - x, 1 - phat) + xlogy(x, phat)
    )
    lr_pof = max(lr_pof, 0.0)
    n00 = n01 = n10 = n11 = 0
    for a, b in zip(hits[:-1], hits[1:], strict=True):
        if a == 0 and b == 0:
            n00 += 1
        elif a == 0 and b == 1:
            n01 += 1
        elif a == 1 and b == 0:
            n10 += 1
        else:
            n11 += 1
    t = n00 + n01 + n10 + n11
    lr_ind = 0.0
    if t > 0:
        pi = (n01 + n11) / t
        pi01 = n01 / (n00 + n01) if n00 + n01 > 0 else 0.0
        pi11 = n11 / (n10 + n11) if n10 + n11 > 0 else 0.0
        lr_ind = -2.0 * (xlogy(n00 + n10, 1 - pi) + xlogy(n01 + n11, pi)) + 2.0 * (
            xlogy(n00, 1 - pi01) + xlogy(n01, pi01) + xlogy(n10, 1 - pi11) + xlogy(n11, pi11)
        )
        lr_ind = max(lr_ind, 0.0)
    cdf = float(binom.cdf(x, n, p))
    zone = "green" if cdf < 0.95 else ("yellow" if cdf < 0.9999 else "red")
    return {
        "exceptions": x,
        "kupiec_lr": lr_pof,
        "kupiec_p": float(chi2.sf(lr_pof, 1)),
        "ind_lr": lr_ind,
        "ind_p": float(chi2.sf(lr_ind, 1)),
        "cc_lr": lr_pof + lr_ind,
        "cc_p": float(chi2.sf(lr_pof + lr_ind, 2)),
        "cdf": cdf,
        "zone": zone,
    }


def hit_sequences(rng: np.random.Generator) -> list[tuple[str, float, list[int]]]:
    out: list[tuple[str, float, list[int]]] = []
    k = 0
    for level in (0.99, 0.975, 0.95):
        for n in (250, 500, 1000, 2500):
            for kind in ("iid", "clustered", "heavy"):
                p = 1 - level
                if kind == "iid":
                    h = (rng.random(n) < p).astype(int)
                elif kind == "clustered":  # exceptions arrive in bursts
                    h = np.zeros(n, dtype=int)
                    i = 0
                    while i < n:
                        if rng.random() < p / 3.0:
                            run = int(rng.integers(2, 6))
                            h[i : i + run] = 1
                            i += run
                        else:
                            i += 1
                else:  # too many exceptions, independent
                    h = (rng.random(n) < 2.5 * p).astype(int)
                out.append((f"h{k:02d}", level, [int(v) for v in h]))
                k += 1
    # edge cases: no exception, one exception, all exceptions, alternating
    out.append((f"h{k:02d}", 0.99, [0] * 250))
    out.append((f"h{k + 1:02d}", 0.99, [0] * 100 + [1] + [0] * 149))
    out.append((f"h{k + 2:02d}", 0.99, [1] * 30))
    out.append((f"h{k + 3:02d}", 0.95, [0, 1] * 100))
    return out


# ----------------------------------------------------------------------------- VaR methods
def gk_price(kind: str, s, k: float, rd: float, rf: float, t: float, vol: float):
    """Garman-Kohlhagen price; `s` may be a numpy array."""
    d1 = (np.log(s / k) + (rd - rf + 0.5 * vol * vol) * t) / (vol * math.sqrt(t))
    d2 = d1 - vol * math.sqrt(t)
    if kind == "call":
        return s * math.exp(-rf * t) * norm.cdf(d1) - k * math.exp(-rd * t) * norm.cdf(d2)
    return k * math.exp(-rd * t) * norm.cdf(-d2) - s * math.exp(-rf * t) * norm.cdf(-d1)


def parse_portfolio(text: str):
    s0, rd, rf, units, legs = text.split("|")
    parsed = []
    for leg in legs.split(";"):
        kind, k, t, vol, qty = leg.split(":")
        parsed.append((kind, float(k), float(t), float(vol), float(qty)))
    return float(s0), float(rd), float(rf), float(units), parsed


def value(pf, s):
    _, rd, rf, units, legs = pf
    return units * s + sum(q * gk_price(kd, s, k, rd, rf, t, v) for kd, k, t, v, q in legs)


def delta_gamma(pf) -> tuple[float, float]:
    """Delta and gamma by closed-form Black-Scholes sensitivities (own formulas)."""
    s0, rd, rf, units, legs = pf
    delta, gamma = units, 0.0
    for kd, k, t, v, q in legs:
        d1 = (math.log(s0 / k) + (rd - rf + 0.5 * v * v) * t) / (v * math.sqrt(t))
        e = math.exp(-rf * t)
        delta += q * e * (norm.cdf(d1) if kd == "call" else norm.cdf(d1) - 1.0)
        gamma += q * e * norm.pdf(d1) / (s0 * v * math.sqrt(t))
    return delta, gamma


def hermite_moments(c1: float, c2: float) -> tuple[float, float, float, float]:
    """Mean, sd, skewness, excess kurtosis of Q = c1 w + c2 w^2, w ~ N(0,1), by Gauss-Hermite."""
    nodes, weights = np.polynomial.hermite_e.hermegauss(80)
    weights = weights / weights.sum()
    q = c1 * nodes + c2 * nodes**2
    mean = float(np.sum(weights * q))
    var = float(np.sum(weights * (q - mean) ** 2))
    m3 = float(np.sum(weights * (q - mean) ** 3))
    m4 = float(np.sum(weights * (q - mean) ** 4))
    sd = math.sqrt(var)
    return mean, sd, m3 / sd**3, m4 / var**2 - 3.0


def cornish_fisher(mean: float, sd: float, s: float, k: float, level: float) -> float:
    z = float(norm.ppf(1.0 - level))
    zcf = z + (z * z - 1) * s / 6 + (z**3 - 3 * z) * k / 24 - (2 * z**3 - 5 * z) * s * s / 36
    return -(mean + sd * zcf)


def hist_var_es(pnl: np.ndarray, level: float) -> tuple[float, float]:
    n = len(pnl)
    k = min(max(math.ceil(n * (1.0 - level) - 1e-9), 1), n)
    srt = np.sort(pnl)
    return float(-srt[k - 1]), float(-srt[:k].mean())


def grid_pnl(pf, sigma: float, n: int = 100000) -> np.ndarray:
    s0 = pf[0]
    z = norm.ppf((np.arange(n) + 0.5) / n)
    return value(pf, s0 * (1.0 + sigma * z)) - value(pf, s0)


PORTFOLIOS = [
    "1.10|0.03|0.01|0|call:1.10:0.25:0.08:1000000;put:1.10:0.25:0.08:1000000",
    "1.10|0.03|0.01|0|call:1.10:0.25:0.08:-1000000;put:1.10:0.25:0.08:-1000000",
    "1.10|0.03|0.01|0|call:1.15:0.5:0.09:1000000;put:1.05:0.5:0.09:-1000000",
    "1.10|0.03|0.01|500000|put:1.05:0.08:0.10:-1000000",
    "1.10|0.03|0.01|-300000|call:1.12:0.08:0.10:1000000",
    "4.30|0.055|0.025|0|call:4.30:1.0:0.12:1000000",
]
SIGMAS = [0.004, 0.008, 0.016, 0.032]


def main(directory: str) -> None:
    out = Path(directory)
    out.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(SEED)

    with (out / "risk_hits_reference.csv").open("w", newline="", encoding="utf-8") as handle:
        w = csv.writer(handle, lineterminator="\n")
        w.writerow(
            ["id", "level", "n", "hits", "exceptions", "kupiec_lr", "kupiec_p", "ind_lr", "ind_p",
             "cc_lr", "cc_p", "cdf", "zone"]
        )  # fmt: skip
        for sid, level, hits in hit_sequences(rng):
            r = backtest(hits, level)
            w.writerow(
                [sid, repr(level), len(hits), "".join(map(str, hits)), r["exceptions"],
                 *(repr(float(r[k])) for k in STAT_KEYS),
                 r["zone"]]
            )  # fmt: skip

    rows: list[list[str]] = []
    cols = ["kind", "a", "b", "c", "d", "level", "var", "es"]
    for i in range(30):  # historical
        n = int(rng.integers(100, 400))
        scale = float(rng.uniform(0.5, 3.0))
        pnl = scale * (rng.standard_t(5, n) + 0.2)
        level = [0.99, 0.975, 0.95, 0.9][i % 4]
        var, es = hist_var_es(pnl, level)
        rows.append(
            [
                "hist",
                ";".join(repr(float(x)) for x in pnl),
                "",
                "",
                "",
                repr(level),
                repr(var),
                repr(es),
            ]
        )
    for i in range(30):  # normal
        mean, sd = float(rng.normal(0, 0.5)), float(rng.uniform(0.1, 3.0))
        level = [0.99, 0.975, 0.95, 0.9, 0.999][i % 5]
        z = float(norm.ppf(1 - level))
        rows.append(["norm", repr(mean), repr(sd), "", "", repr(level), repr(-(mean + sd * z)),
                     repr(-mean + sd * float(norm.pdf(z)) / (1 - level))])  # fmt: skip
    for i in range(30):  # Cornish-Fisher
        mean, sd = float(rng.normal(0, 0.5)), float(rng.uniform(0.1, 3.0))
        s, k = float(rng.uniform(-0.8, 0.8)), float(rng.uniform(0.0, 3.0))
        level = [0.99, 0.975, 0.95][i % 3]
        rows.append(["cf", repr(mean), repr(sd), repr(s), repr(k), repr(level),
                     repr(cornish_fisher(mean, sd, s, k, level)), ""])  # fmt: skip
    for text in PORTFOLIOS:
        pf = parse_portfolio(text)
        delta, gamma = delta_gamma(pf)
        for sigma in SIGMAS:
            pnl = grid_pnl(pf, sigma)
            for level in (0.99, 0.95):
                c1 = delta * pf[0] * sigma
                c2 = 0.5 * gamma * pf[0] ** 2 * sigma**2
                mean, sd, s, k = hermite_moments(c1, c2)
                dg_var = cornish_fisher(mean, sd, s, k, level)
                zq = norm.ppf((np.arange(100000) + 0.5) / 100000)
                _, dg_es = hist_var_es(c1 * zq + c2 * zq**2, level)
                rows.append(
                    ["dg", text, repr(sigma), "", "", repr(level), repr(dg_var), repr(dg_es)]
                )
                var, es = hist_var_es(pnl, level)
                rows.append(["full", text, repr(sigma), "", "", repr(level), repr(var), repr(es)])
    with (out / "risk_var_reference.csv").open("w", newline="", encoding="utf-8") as handle:
        w = csv.writer(handle, lineterminator="\n")
        w.writerow(cols)
        w.writerows(rows)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
