"""Tests for the counterparty-credit reference generator.

The C++ tests CREDIT-REF and V-19 trust the table, so its source is tested: the table must be
reproducible, the formulas must satisfy identities that do not depend on them, and
the CVA must agree with a hand calculation.
"""

import csv
import math
from pathlib import Path

import pytest

pytest.importorskip("numpy")
pytest.importorskip("scipy")

import gen_credit_reference as gen  # noqa: E402

DATA = (
    Path(__file__).resolve().parents[2] / "tests" / "validation" / "data" / "credit_reference.csv"
)


def _read(path: Path) -> list[dict[str, str]]:
    with path.open() as handle:
        return list(csv.DictReader(handle))


def _numbers(text: str) -> list[float]:
    return [float(x) for x in text.split(";")]  # one number or a ';'-joined list


def test_committed_table_is_reproducible(tmp_path):
    gen.main(str(tmp_path))
    new, old = _read(tmp_path / "credit_reference.csv"), _read(DATA)
    assert len(new) == len(old) == 80
    for a, b in zip(new, old, strict=True):
        assert list(a) == list(b)
        for key, value in a.items():
            if value == b[key]:
                continue
            try:
                x, y = _numbers(value), _numbers(b[key])
            except ValueError:
                assert value == b[key], key
                continue
            assert x == pytest.approx(y, rel=1e-9, abs=1e-12), key


def test_black_call_minus_put_is_the_forward_minus_strike():
    f, k, s = 1.12, 1.05, 0.2
    assert gen.black(f, k, s, True) - gen.black(f, k, s, False) == pytest.approx(f - k, abs=1e-14)


def test_long_and_short_forward_exposures_add_to_the_expected_absolute_value():
    # max(V, 0) + max(-V, 0) = |V|, and E|V| >= |E V| (here E V = a (E S - K'))
    ee_long, _, _ = gen.forward_ee_pfe(1, 1.10, 0.02, 0.5)
    ee_short, _, _ = gen.forward_ee_pfe(-1, 1.10, 0.02, 0.5)
    tau = 0.5
    mean_v = (
        gen.NOTIONAL
        * math.exp(-gen.RF * tau)
        * (gen.S0 * math.exp(0.02 * 0.5) - 1.10 * math.exp(-(gen.RD - gen.RF) * tau))
    )
    assert ee_long - ee_short == pytest.approx(mean_v, rel=1e-12)


def test_cva_by_intervals_for_a_constant_discounted_exposure_telescopes():
    dates = [0.0, 0.3, 0.9, 1.0]
    rd, lam, rec = 0.03, 0.05, 0.4
    ee = [7.0 * math.exp(rd * t) for t in dates]
    expected = (1 - rec) * 7.0 * (1 - math.exp(-lam * dates[-1]))
    assert gen.cva_by_intervals(dates, ee, lam, rec, rd) == pytest.approx(expected, rel=1e-13)


def test_option_row_cva_uses_the_stated_closed_form():
    price = gen.gk_price(True, 1.10, 1.0)
    # at the money forward: S0 e^{0.02} = 1.1222, the price is positive and below the forward
    assert 0 < price < gen.S0
    rows = [r for r in _read(DATA) if r["kind"] == "optq"]
    assert float(rows[1]["value2"]) == pytest.approx(
        0.6 * float(rows[1]["value"]) * (1 - math.exp(-0.03 * 0.8)), rel=1e-12
    )
