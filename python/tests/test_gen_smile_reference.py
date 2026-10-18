"""Tests for the smile reference generator.

The C++ validation tests trust the reference table, so its source is tested too: it must be
reproducible (to rounding, not byte for byte: root finders and numpy can differ in the last digit
between platforms), its strikes must have the stated delta under an independent formula, and the
density factor it uses must agree with the second difference of option prices.
"""

import csv
import math
from pathlib import Path

import pytest

pytest.importorskip("numpy")
pytest.importorskip("scipy")

import gen_smile_reference as gen  # noqa: E402
from scipy.stats import norm  # noqa: E402

TABLE = (
    Path(__file__).resolve().parents[2] / "tests" / "validation" / "data" / "smile_reference.csv"
)


def _d1(s, rd, rf, t, k, v):
    return (math.log(s / k) + (rd - rf + 0.5 * v * v) * t) / (v * math.sqrt(t))


def test_committed_table_is_reproducible(tmp_path):
    out = tmp_path / "t.csv"
    gen.main(str(out))
    with out.open() as a, TABLE.open() as b:
        new, old = list(csv.reader(a)), list(csv.reader(b))
    assert len(new) == len(old) == gen.RANDOM_CASES + len(gen.BOUNDARY_CASES) + 1
    assert new[0] == old[0]
    for row_new, row_old in zip(new[1:], old[1:], strict=True):
        assert row_new[0] == row_old[0]
        assert [float(x) for x in row_new[1:]] == pytest.approx(
            [float(x) for x in row_old[1:]], rel=1e-9, abs=1e-9
        )


def test_textbook_forward_delta_strike():
    # Forward 25-delta call, sigma 10%, T = 1, F = 1: d1 = N^-1(0.25) = -0.6744897501960817, so
    # K = exp(-0.1 * d1 + 0.005).
    k = gen.strike_by_root_find("call", "forward", 0.25, 1.0, 0.0, 0.0, 0.10, 1.0)
    assert k == pytest.approx(math.exp(0.1 * 0.6744897501960817 + 0.005), rel=1e-12)


def test_atm_strike_is_the_delta_neutral_straddle():
    s, rd, rf, sigma, t = 1.1, 0.04, 0.025, 0.08, 1.0
    k = gen.atm_by_root_find("spot", s, rd, rf, sigma, t)
    assert k == pytest.approx(s * math.exp((rd - rf) * t + 0.5 * sigma**2 * t), rel=1e-12)


def test_flat_smile_has_unit_density_factor():
    row = gen.smile_case("spot", 1.1, 0.04, 0.025, 1.0, 0.08, 0.0, 0.0, 0.25)
    assert row is not None
    assert row[-len(gen.PROBES) :] == pytest.approx([1.0] * len(gen.PROBES), abs=1e-10)


def test_pivots_have_the_quoted_delta_in_both_conventions():
    for convention in ("spot", "forward"):
        s, rd, rf, t, atm, rr, bf, delta = 1.1, 0.04, 0.025, 0.5, 0.09, -0.01, 0.003, 0.25
        row = gen.smile_case(convention, s, rd, rf, t, atm, rr, bf, delta)
        sig_p, sig_c, k_p, k_a, k_c = row[:5]
        disc = math.exp(-rf * t) if convention == "spot" else 1.0
        assert disc * norm.cdf(_d1(s, rd, rf, t, k_c, sig_c)) == pytest.approx(delta, abs=1e-12)
        assert disc * norm.cdf(-_d1(s, rd, rf, t, k_p, sig_p)) == pytest.approx(delta, abs=1e-12)
        assert norm.cdf(_d1(s, rd, rf, t, k_a, atm)) + norm.cdf(
            -_d1(s, rd, rf, t, k_a, atm)
        ) == pytest.approx(1.0)


def test_density_factor_agrees_with_option_price_convexity():
    """g(k) is defined through w(k); the price second difference does not use g at all."""
    s, rd, rf, t, atm, rr, bf, delta = 1.1, 0.04, 0.025, 1.0, 0.09, -0.012, 0.004, 0.25
    fwd = s * math.exp((rd - rf) * t)
    sig_c, sig_p = atm + bf + rr / 2, atm + bf - rr / 2
    ks = [
        gen.strike_by_root_find("put", "spot", delta, s, rd, rf, sig_p, t),
        gen.atm_by_root_find("spot", s, rd, rf, atm, t),
        gen.strike_by_root_find("call", "spot", delta, s, rd, rf, sig_c, t),
    ]
    coeffs = gen.smile_polynomial([math.log(k / fwd) for k in ks], [sig_p, atm, sig_c])

    def call(strike: float) -> float:
        sigma = float(gen.np.polyval(coeffs, math.log(strike / fwd)))
        d1 = (math.log(fwd / strike) + 0.5 * sigma**2 * t) / (sigma * math.sqrt(t))
        return math.exp(-rd * t) * (
            fwd * norm.cdf(d1) - strike * norm.cdf(d1 - sigma * math.sqrt(t))
        )

    for x in (-2.0, -0.5, 0.0, 0.7, 2.0):
        k_log = x * atm * math.sqrt(t)
        strike = fwd * math.exp(k_log)
        h = 1e-3 * strike
        # Five-point stencil: error O(h^4), so the comparison is limited by the formula, not by h.
        second = (
            -call(strike + 2 * h)
            + 16 * call(strike + h)
            - 30 * call(strike)
            + 16 * call(strike - h)
            - call(strike - 2 * h)
        ) / (12 * h**2)
        density_numeric = math.exp(rd * t) * second
        g = gen.density_factor(coeffs, k_log, t)
        sigma = float(gen.np.polyval(coeffs, k_log))
        w = sigma * sigma * t
        d2 = -k_log / math.sqrt(w) - 0.5 * math.sqrt(w)
        density_formula = g * math.exp(-0.5 * d2 * d2) / (strike * math.sqrt(2 * math.pi * w))
        assert density_numeric == pytest.approx(density_formula, rel=1e-5)
