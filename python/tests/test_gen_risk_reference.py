"""Tests for the market-risk reference generators (risk and GARCH).

The C++ tests RISK-REF and V-16 trust the tables, so their sources are tested: the tables must be
reproducible, the statistics must match textbook cases by hand, and the generator's simulated
series must have the stated properties.
"""

import csv
import math
from pathlib import Path

import pytest

pytest.importorskip("numpy")
pytest.importorskip("scipy")

import gen_garch_reference as garch  # noqa: E402
import gen_risk_reference as risk  # noqa: E402
import numpy as np  # noqa: E402

DATA = Path(__file__).resolve().parents[2] / "tests" / "validation" / "data"


def _read(path: Path) -> list[dict[str, str]]:
    with path.open() as handle:
        return list(csv.DictReader(handle))


# Columns that are optimiser output: SLSQP stops at a platform-dependent point along flat directions
# of the likelihood (the log-likelihood is much tighter than the parameters); the standard errors
# inherit that. The C++ test V-16 reads the committed table, so these only need to show that the
# generator still finds the same optimum.
PARAMS = {"ref_omega", "ref_alpha", "ref_gamma", "ref_beta"}
ERRORS = {"se_omega", "se_alpha", "se_gamma", "se_beta"}


def _numbers(text: str) -> list[float]:
    return [float(x) for x in text.split(";")]  # a cell is one number or a ';'-joined list


def _same(new: list[dict[str, str]], old: list[dict[str, str]]) -> None:
    assert len(new) == len(old)
    for a, b in zip(new, old, strict=True):
        assert list(a) == list(b)
        for key, value in a.items():
            if value == b[key]:
                continue  # identical text; long 0/1 strings would overflow float
            try:
                x, y = _numbers(value), _numbers(b[key])
            except ValueError:
                assert value == b[key], key
                continue
            assert len(x) == len(y), key
            rel = (
                1e-2
                if key in PARAMS
                else 1e-1
                if key in ERRORS
                else 1e-8
                if key == "ref_ll"
                else 1e-9
            )
            assert x == pytest.approx(y, rel=rel, abs=1e-12), key


def test_risk_tables_are_reproducible(tmp_path):
    risk.main(str(tmp_path))
    for name in ("risk_hits_reference.csv", "risk_var_reference.csv"):
        _same(_read(tmp_path / name), _read(DATA / name))


def test_garch_tables_are_reproducible(tmp_path):
    garch.main(str(tmp_path))
    for name in ("garch_series.csv", "garch_reference.csv"):
        _same(_read(tmp_path / name), _read(DATA / name))


def test_kupiec_by_hand():
    hits = [0] * 245 + [1] * 5
    got = risk.backtest(hits, 0.99)
    expected = -2.0 * (245 * math.log(0.99) + 5 * math.log(0.01)) + 2.0 * (
        245 * math.log(0.98) + 5 * math.log(0.02)
    )
    assert got["kupiec_lr"] == pytest.approx(expected, rel=1e-12)


def test_basel_zones_for_250_days():
    zones = {x: risk.backtest([1] * x + [0] * (250 - x), 0.99)["zone"] for x in (0, 4, 5, 9, 10)}
    assert zones == {0: "green", 4: "green", 5: "yellow", 9: "yellow", 10: "red"}


def test_historical_var_uses_the_kth_worst():
    pnl = np.array([5, -3, 2, -10, 7, -1, 4, -6, 0, 3], dtype=float)
    assert risk.hist_var_es(pnl, 0.8) == (6.0, 8.0)


def test_simulated_garch_series_has_roughly_the_stated_variance():
    rng = np.random.default_rng(1)
    spec = ("x", "garch", 20000, 4e-7, 0.07, 0.0, 0.91, "normal")
    r = garch.simulate(rng, spec)
    assert np.var(r) == pytest.approx(4e-7 / (1 - 0.98), rel=0.25)
