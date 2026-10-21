"""Write the GARCH reference tables for test V-16 (GARCH estimation against independent code).

Usage: python python/gen_garch_reference.py tests/validation/data

Writes two files:

* garch_series.csv    id,t,return   - 12 simulated zero-mean return series (long format)
* garch_reference.csv one row per series: the data-generating parameters, the log-likelihood and
  variance-path summaries AT those parameters (checks the recursion alone), and the
  maximum-likelihood estimate found here (checks the optimiser).

Independence: this file shares no code with the C++ library. The series are simulated here (numpy
default_rng, seed below). The estimate is found with scipy's SLSQP on the constrained parameters
(omega, alpha, gamma, beta >= 0, persistence <= 1 - 1e-6) from several starts, a different
algorithm and parameterisation from the C++ simplex-on-softmax. The Python `arch` package could
not be installed where the tables were made (no network), so it is NOT the reference; the
optional script python/compare_with_arch.py compares the C++ estimates with it where `arch` is
available.

Model conventions (the same as docs/model_documentation.md section 9.2, chosen by this project):
zero mean, Gaussian quasi-likelihood, v[0] = mean of r^2 over the series, gamma >= 0, GJR shock
term (alpha + gamma 1{r < 0}) r^2, persistence alpha + beta + gamma / 2.
"""

from __future__ import annotations

import csv
import math
import sys
from pathlib import Path

import numpy as np
from scipy.optimize import minimize

SEED = 20261024
LOG_2PI = math.log(2.0 * math.pi)

# id, model, n, omega (daily variance units), alpha, gamma, beta, innovations
SPECS = [
    ("g01", "garch", 500, 2.0e-7, 0.05, 0.0, 0.93, "normal"),
    ("g02", "garch", 1000, 4.0e-7, 0.08, 0.0, 0.88, "normal"),
    ("g03", "garch", 2000, 1.0e-6, 0.06, 0.0, 0.90, "normal"),
    ("g04", "garch", 1000, 8.0e-7, 0.12, 0.0, 0.80, "normal"),
    ("g05", "garch", 1500, 3.0e-7, 0.07, 0.0, 0.91, "t6"),
    ("g06", "garch", 750, 1.5e-6, 0.10, 0.0, 0.85, "normal"),
    ("j01", "gjr", 1000, 3.0e-7, 0.02, 0.10, 0.90, "normal"),
    ("j02", "gjr", 2000, 5.0e-7, 0.03, 0.12, 0.88, "normal"),
    ("j03", "gjr", 1500, 4.0e-7, 0.01, 0.15, 0.89, "normal"),
    ("j04", "gjr", 1000, 6.0e-7, 0.04, 0.06, 0.90, "normal"),
    ("j05", "gjr", 1500, 2.0e-7, 0.02, 0.08, 0.92, "t6"),
    ("j06", "gjr", 750, 1.0e-6, 0.05, 0.10, 0.84, "normal"),
]


def recursion(r: np.ndarray, model: str, omega: float, alpha: float, gamma: float, beta: float):
    """Variance path (length n + 1) and Gaussian log-likelihood, plain loops."""
    v = float(np.mean(r * r))
    path = [v]
    ll = 0.0
    for x in r:
        ll += -0.5 * (LOG_2PI + math.log(v) + x * x / v)
        a = alpha + (gamma if (model == "gjr" and x < 0.0) else 0.0)
        v = omega + a * x * x + beta * v
        path.append(v)
    return np.array(path), ll


def simulate(rng: np.random.Generator, spec) -> np.ndarray:
    _, model, n, omega, alpha, gamma, beta, innov = spec
    burn = 500
    if innov == "normal":
        z = rng.standard_normal(n + burn)
    else:  # Student t with 6 degrees of freedom, scaled to unit variance
        z = rng.standard_t(6, n + burn) / math.sqrt(6.0 / 4.0)
    persistence = alpha + beta + 0.5 * gamma
    v = omega / (1.0 - persistence)
    out = np.empty(n + burn)
    for t in range(n + burn):
        r = math.sqrt(v) * z[t]
        out[t] = r
        a = alpha + (gamma if (model == "gjr" and r < 0.0) else 0.0)
        v = omega + a * r * r + beta * v
    return out[burn:]


def fit(r: np.ndarray, model: str):
    """Constrained maximum likelihood by SLSQP, best of several starts. Returns (params, ll)."""
    scale = 100.0  # work in percent returns for conditioning, convert back below
    rs = r * scale

    def unpack(x):
        omega, alpha, beta = x[0], x[1], x[2]
        gamma = x[3] if model == "gjr" else 0.0
        return omega, alpha, gamma, beta

    def negll(x):
        omega, alpha, gamma, beta = unpack(x)
        if omega <= 0.0:
            return 1e30
        _, ll = recursion(rs, model, omega, alpha, gamma, beta)
        return -ll

    def slack(x):
        omega, alpha, gamma, beta = unpack(x)
        return 1.0 - 1e-6 - (alpha + beta + 0.5 * gamma)

    var = float(np.mean(rs * rs))
    best = None
    for a0, g0, b0 in [
        (0.05, 0.05, 0.90),
        (0.10, 0.10, 0.75),
        (0.03, 0.02, 0.94),
        (0.15, 0.10, 0.60),
    ]:
        x0 = [var * (1.0 - a0 - b0 - 0.5 * g0), a0, b0] + ([g0] if model == "gjr" else [])
        bounds = [(1e-10, None), (0.0, 1.0), (0.0, 1.0)] + ([(0.0, 1.0)] if model == "gjr" else [])
        res = minimize(
            negll,
            x0,
            method="SLSQP",
            bounds=bounds,
            constraints=[{"type": "ineq", "fun": slack}],
            options={"maxiter": 500, "ftol": 1e-14},
        )
        if (res.success or np.isfinite(res.fun)) and (best is None or res.fun < best.fun):
            best = res
    omega, alpha, gamma, beta = unpack(best.x)
    omega = omega / scale**2
    _, ll = recursion(r, model, omega, alpha, gamma, beta)
    return (omega, alpha, gamma, beta), ll


def hessian_se(r: np.ndarray, model: str, params) -> tuple[float, float, float, float]:
    """Standard errors from a central-difference Hessian of the negative log-likelihood.

    The step is 2e-4 of each parameter. It matters: near the stationarity boundary the
    likelihood is far from quadratic, and the standard error of beta for series g01 is 0.0019
    at a step of 2e-2, 0.0124 at 2e-3 and 0.0143 at 2e-4 (it settles below 1e-3). The first
    version of this table used 2e-3 here and 1e-3 in the C++ code and the two disagreed by up
    to 14% (V-16 header).
    """
    omega, alpha, gamma, beta = params
    idx = [0, 1, 3] if model == "garch" else [0, 1, 2, 3]
    theta = np.array(params, dtype=float)

    def f(th):
        _, ll = recursion(r, model, th[0], th[1], th[2], th[3])
        return -ll

    h = np.maximum(np.abs(theta), 1e-8) * 2e-4
    k = len(idx)
    hess = np.zeros((k, k))
    for a, i in enumerate(idx):
        for b, j in enumerate(idx):
            tpp, tpm, tmp, tmm = (theta.copy() for _ in range(4))
            tpp[i] += h[i]
            tpp[j] += h[j]
            tpm[i] += h[i]
            tpm[j] -= h[j]
            tmp[i] -= h[i]
            tmp[j] += h[j]
            tmm[i] -= h[i]
            tmm[j] -= h[j]
            hess[a, b] = (f(tpp) - f(tpm) - f(tmp) + f(tmm)) / (4.0 * h[i] * h[j])
    cov = np.linalg.inv(hess)
    se = np.sqrt(np.diag(cov))
    out = [0.0, 0.0, 0.0, 0.0]
    for a, i in enumerate(idx):
        out[i] = float(se[a])
    return tuple(out)


COLUMNS = [
    "id", "model", "n", "true_omega", "true_alpha", "true_gamma", "true_beta",
    "ll_at_true", "v_last_at_true", "v_sum_at_true",
    "ref_omega", "ref_alpha", "ref_gamma", "ref_beta", "ref_ll",
    "se_omega", "se_alpha", "se_gamma", "se_beta",
]  # fmt: skip


def main(directory: str) -> None:
    out = Path(directory)
    out.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(SEED)
    series_rows = []
    table = []
    for spec in SPECS:
        sid, model, n, omega, alpha, gamma, beta, _ = spec
        r = simulate(rng, spec)
        for t, x in enumerate(r):
            series_rows.append([sid, t, repr(float(x))])
        path, ll_true = recursion(r, model, omega, alpha, gamma, beta)
        params, ll_ref = fit(r, model)
        se = hessian_se(r, model, params)
        table.append(
            [sid, model, n, omega, alpha, gamma, beta, ll_true, path[-1], float(path.sum()),
             *params, ll_ref, *se]
        )  # fmt: skip
    with (out / "garch_series.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(["id", "t", "return"])
        writer.writerows(series_rows)
    with (out / "garch_reference.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(COLUMNS)
        for row in table:
            writer.writerow([x if isinstance(x, str) else repr(float(x)) for x in row])


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
