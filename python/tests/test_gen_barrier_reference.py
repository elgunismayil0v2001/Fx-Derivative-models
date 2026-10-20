"""Tests for the barrier reference generator.

The C++ test BARRIER-REF trusts the table, so its source is tested too: the table must be
reproducible, the numerical integrals must agree with textbook closed forms on easy cases,
and in-out parity and the one-touch limits must hold in the generator's own numbers.
"""

import csv
import math
from pathlib import Path

import pytest

pytest.importorskip("numpy")
pytest.importorskip("scipy")

import gen_barrier_reference as gen  # noqa: E402
from scipy.stats import norm  # noqa: E402

DATA = (
    Path(__file__).resolve().parents[2] / "tests" / "validation" / "data" / "barrier_reference.csv"
)

LABELS = ("kind", "type", "direction", "effect")


def test_committed_table_is_reproducible(tmp_path):
    target = tmp_path / "barrier_reference.csv"
    gen.main(str(target))
    with target.open() as a, DATA.open() as b:
        new, old = list(csv.DictReader(a)), list(csv.DictReader(b))
    assert len(new) == len(old) == 700
    for row_new, row_old in zip(new, old, strict=True):
        assert list(row_new) == list(row_old)
        for key, value in row_new.items():
            if key in LABELS:
                assert value == row_old[key]
            else:
                assert float(value) == pytest.approx(float(row_old[key]), abs=1e-12)


def test_far_barrier_knock_out_is_black_scholes():
    s, k, rd, rf, t, vol = 1.1, 1.1, 0.03, 0.01, 0.5, 0.1
    d1 = (math.log(s / k) + (rd - rf + 0.5 * vol**2) * t) / (vol * math.sqrt(t))
    d2 = d1 - vol * math.sqrt(t)
    bs = s * math.exp(-rf * t) * norm.cdf(d1) - k * math.exp(-rd * t) * norm.cdf(d2)
    ko = gen.knock_out("call", "down", s, k, 0.05, rd, rf, t, vol)
    assert ko == pytest.approx(bs, abs=1e-10)


def test_in_out_parity_in_the_generator():
    args = (1.1, 1.05, 1.0, 0.03, 0.01, 0.75, 0.12)
    full = gen.vanilla_full("call", 1.1, 1.05, 0.03, 0.01, 0.75, 0.12)
    ko = gen.vanilla_barrier("call", "down", "knock_out", *args)
    ki = gen.vanilla_barrier("call", "down", "knock_in", *args)
    assert ko + ki == pytest.approx(full, abs=1e-14)


def test_one_touch_with_zero_drift_follows_the_reflection_principle():
    vol, t, rd = 0.2, 1.0, 0.03
    rf = rd - 0.5 * vol**2  # zero drift of the log spot
    for direction, barrier in (("down", 1.0), ("up", 1.2)):
        x = abs(math.log(barrier / 1.1))
        expected = math.exp(-rd * t) * 2.0 * norm.cdf(-x / (vol * math.sqrt(t)))
        assert gen.one_touch(direction, 1.1, barrier, rd, rf, t, vol) == pytest.approx(
            expected, abs=1e-10
        )


def test_double_no_touch_is_smaller_than_either_single_no_touch():
    dnt = gen.double_no_touch(1.1, 1.0, 1.2, 0.03, 0.01, 0.5, 0.1)
    df = math.exp(-0.03 * 0.5)
    single_down = df - gen.one_touch("down", 1.1, 1.0, 0.03, 0.01, 0.5, 0.1)
    single_up = df - gen.one_touch("up", 1.1, 1.2, 0.03, 0.01, 0.5, 0.1)
    assert 0.0 < dnt < min(single_down, single_up)
