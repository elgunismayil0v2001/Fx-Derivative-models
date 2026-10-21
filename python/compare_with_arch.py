"""Optional: compare the C++ GARCH estimates with the Python `arch` package.

Usage: python python/compare_with_arch.py tests/validation/data

NOT RUN where it was written: `arch` could not be installed there (no network). It is a plain
script, not part of the test suite, and it exits quietly when `arch` is missing. The reference
used by test V-16 is python/gen_garch_reference.py (own SLSQP fit), not `arch`.

Conventions differ and are handled here: `arch` works best on percent returns, so the series are
multiplied by 100 and omega is divided by 1e4 afterwards; its mean is "Zero"; its variance
initialisation is a backcast (exponentially weighted, not the sample mean of r^2 that this
project uses), so the likelihoods differ slightly and the comparison is to a tolerance, printed
and not asserted. `arch` GJR uses `o=1` and its gamma[1] is the coefficient on r^2 1{r<0}, the
same as gamma here.
"""

from __future__ import annotations

import csv
import sys
from pathlib import Path


def main(directory: str) -> int:
    try:
        from arch import arch_model
    except ImportError:
        print("the `arch` package is not installed; nothing to compare")
        return 0
    import numpy as np

    series: dict[str, list[float]] = {}
    with (Path(directory) / "garch_series.csv").open() as handle:
        for row in csv.DictReader(handle):
            series.setdefault(row["id"], []).append(float(row["return"]))
    with (Path(directory) / "garch_reference.csv").open() as handle:
        table = list(csv.DictReader(handle))
    print("id  model  d_loglik(arch-ours)  max |dparam|/ours")
    for row in table:
        r = np.array(series[row["id"]]) * 100.0
        gjr = row["model"] == "gjr"
        fit = arch_model(
            r, mean="Zero", vol="GARCH", p=1, o=1 if gjr else 0, q=1, dist="normal"
        ).fit(disp="off")
        p = fit.params
        theirs = {
            "omega": float(p["omega"]) / 1e4,
            "alpha": float(p["alpha[1]"]),
            "gamma": float(p["gamma[1]"]) if gjr else 0.0,
            "beta": float(p["beta[1]"]),
        }
        worst = max(
            abs(theirs[k] - float(row["ref_" + k])) / max(abs(float(row["ref_" + k])), 1e-12)
            for k in theirs
            if gjr or k != "gamma"
        )
        d_ll = float(fit.loglikelihood) + len(r) * np.log(100.0) - float(row["ref_ll"])
        print(f"{row['id']}  {row['model']:5}  {d_ll:+.4f}  {worst:.3g}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1]))
