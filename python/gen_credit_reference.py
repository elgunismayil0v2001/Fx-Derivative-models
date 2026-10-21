"""Write the counterparty-credit reference table for CREDIT-REF, V-19 and V-20.

Usage: python python/gen_credit_reference.py tests/validation/data

Writes credit_reference.csv with columns kind,a,b,c,d,e,value,value2. Nothing here uses the C++
library: closed forms with scipy, and the CVA discretised by an explicit loop over intervals (the
C++ code uses one weight per date, a different arrangement of the same sum). Seed 20261101.

Kinds (spot 1.10, rd 3%, rf 1%, sigma 10%, forward maturity 1 year, 1e6 foreign units):
  fwd   a = side (+1 receive foreign, -1 the opposite), b = strike, c = drift mu (growth of E[S]),
        d = t, e = PFE 99% of max(V, 0), value = EE(t), value2 = PFE 95%.
  optq  a = type (1 call, -1 put), b = strike, c = expiry, value = Garman-Kohlhagen price per unit
        foreign, value2 = exact CVA of the long option for the dates 0.2, 0.4, 0.6, 0.8 with
        lambda 3% and R 40%: (1 - R) price (1 - exp(-lambda 0.8)). Under the risk-neutral measure
        DF(t) E[V_t] is the price at every date, so the sum telescopes.
  cva   a = dates joined by ';' (starting at 0), b = EE joined by ';', c = lambda, d = R, e = rd,
        value = CVA by the interval formula.
"""

from __future__ import annotations

import csv
import math
import sys
from pathlib import Path

import numpy as np
from scipy.stats import norm

S0, RD, RF, SIGMA = 1.10, 0.03, 0.01, 0.10
NOTIONAL = 1.0e6
MATURITY = 1.0
STRIKES = (1.00, 1.10, 1.25)
DRIFTS = (RD - RF, 0.05)
DATES = (0.1, 0.3, 0.5, 0.7, 0.9)


def black(f: float, k: float, s: float, call: bool) -> float:
    """Undiscounted Black formula for a lognormal with mean f and total volatility s."""
    d1 = (math.log(f / k) + 0.5 * s * s) / s
    d2 = d1 - s
    if call:
        return f * norm.cdf(d1) - k * norm.cdf(d2)
    return k * norm.cdf(-d2) - f * norm.cdf(-d1)


def forward_ee_pfe(side: int, strike: float, mu: float, t: float) -> tuple[float, float, float]:
    tau = MATURITY - t
    a = math.exp(-RF * tau)  # V = side N (a S_t - K e^{-rd tau}) = side N a (S_t - K')
    k_prime = strike * math.exp(-(RD - RF) * tau)
    mean_s = S0 * math.exp(mu * t)
    s_tot = SIGMA * math.sqrt(t)
    ee = NOTIONAL * a * black(mean_s, k_prime, s_tot, call=side > 0)
    pfes = []
    for level in (0.95, 0.99):
        z = norm.ppf(level) if side > 0 else norm.ppf(1.0 - level)
        s_q = S0 * math.exp((mu - 0.5 * SIGMA**2) * t + s_tot * z)
        v = side * NOTIONAL * (a * s_q - strike * math.exp(-RD * tau))
        pfes.append(max(v, 0.0))
    return ee, pfes[0], pfes[1]


def gk_price(call: bool, k: float, t: float) -> float:
    f = S0 * math.exp((RD - RF) * t)
    return math.exp(-RD * t) * black(f, k, SIGMA * math.sqrt(t), call)


def cva_by_intervals(dates, ee, lam: float, rec: float, rd: float) -> float:
    total = 0.0
    for i in range(1, len(dates)):
        left = math.exp(-rd * dates[i - 1]) * ee[i - 1]
        right = math.exp(-rd * dates[i]) * ee[i]
        dpd = math.exp(-lam * dates[i - 1]) - math.exp(-lam * dates[i])
        total += 0.5 * (left + right) * dpd
    return (1.0 - rec) * total


def main(out_dir: str) -> None:
    out = Path(out_dir)
    rows: list[list[str]] = []
    for side in (1, -1):
        for strike in STRIKES:
            for mu in DRIFTS:
                for t in DATES:
                    ee, p95, p99 = forward_ee_pfe(side, strike, mu, t)
                    rows.append(["fwd", repr(float(side)), repr(strike), repr(mu), repr(t),
                                 repr(float(p99)), repr(float(ee)), repr(float(p95))])  # fmt: skip
    for call in (True, False):
        for strike in STRIKES:
            price = gk_price(call, strike, 1.0)
            exact = (1.0 - 0.4) * price * (1.0 - math.exp(-0.03 * 0.8))
            rows.append(["optq", repr(1.0 if call else -1.0), repr(strike), repr(1.0), "", "",
                         repr(float(price)), repr(float(exact))])  # fmt: skip
    rng = np.random.default_rng(20261101)
    for i in range(14):
        n = int(rng.integers(3, 14))
        dates = [0.0, *np.cumsum(rng.uniform(0.05, 0.6, n)).tolist()]
        ee = rng.uniform(0.0, 5.0e4, n + 1).tolist()
        lam = [0.0, 0.005, 0.02, 0.08, 0.3][i % 5]
        rec = [0.0, 0.4, 0.8][i % 3]
        rd = [0.0, 0.03, 0.06, -0.01][i % 4]
        rows.append(["cva", ";".join(repr(x) for x in dates), ";".join(repr(x) for x in ee),
                     repr(lam), repr(rec), repr(rd),
                     repr(float(cva_by_intervals(dates, ee, lam, rec, rd))), ""])  # fmt: skip
    cols = ["kind", "a", "b", "c", "d", "e", "value", "value2"]
    with (out / "credit_reference.csv").open("w", newline="", encoding="utf-8") as handle:
        w = csv.writer(handle, lineterminator="\n")
        w.writerow(cols)
        w.writerows(rows)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
