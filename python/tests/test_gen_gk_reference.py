"""Tests for the Garman-Kohlhagen reference generator.

The C++ validation tests trust the reference table, so the table's source is tested too:
it must be deterministic, satisfy parity, reproduce two textbook values, and its Greeks must agree
with finite differences of its own price.
"""

import math

import pytest

pytest.importorskip("numpy")
pytest.importorskip("scipy")

import gen_gk_reference as gen  # noqa: E402


def test_textbook_black_scholes_values():
    # S = K = 100, r = 5%, sigma = 20%, T = 1, no foreign rate: the standard textbook example.
    call = gen.gk_reference("call", 100.0, 100.0, 0.05, 0.0, 0.20, 1.0)["price"]
    put = gen.gk_reference("put", 100.0, 100.0, 0.05, 0.0, 0.20, 1.0)["price"]
    assert call == pytest.approx(10.450583572185565, abs=1e-9)
    assert put == pytest.approx(5.573526022256971, abs=1e-9)


def test_put_call_parity_in_the_reference():
    for case in gen.build_cases()[:50]:
        s, k, rd, rf, _, t = case
        call = gen.gk_reference("call", *case)["price"]
        put = gen.gk_reference("put", *case)["price"]
        assert call - put == pytest.approx(s * math.exp(-rf * t) - k * math.exp(-rd * t), abs=1e-12)


def test_reference_greeks_agree_with_finite_differences_of_its_own_price():
    s, k, rd, rf, sigma, t = 1.10, 1.12, 0.03, 0.01, 0.12, 0.5
    ref = gen.gk_reference("call", s, k, rd, rf, sigma, t)

    def price(**kw):
        args = {"s": s, "k": k, "rd": rd, "rf": rf, "sigma": sigma, "t": t, **kw}
        return gen.gk_reference(
            "call", args["s"], args["k"], args["rd"], args["rf"], args["sigma"], args["t"]
        )["price"]

    h = 1e-5
    assert ref["delta_spot"] == pytest.approx((price(s=s + h) - price(s=s - h)) / (2 * h), rel=1e-7)
    assert ref["vega"] == pytest.approx(
        (price(sigma=sigma + h) - price(sigma=sigma - h)) / (2 * h), rel=1e-7
    )
    assert ref["rho_d"] == pytest.approx((price(rd=rd + h) - price(rd=rd - h)) / (2 * h), rel=1e-7)
    assert ref["rho_f"] == pytest.approx((price(rf=rf + h) - price(rf=rf - h)) / (2 * h), rel=1e-7)
    assert ref["theta"] == pytest.approx(-(price(t=t + h) - price(t=t - h)) / (2 * h), rel=1e-7)


def test_table_is_deterministic_and_has_the_expected_shape(tmp_path):
    a, b = tmp_path / "a.csv", tmp_path / "b.csv"
    rows_a, rows_b = gen.write_table(a), gen.write_table(b)
    assert rows_a == rows_b == 2 * (len(gen.BOUNDARY_CASES) + gen.RANDOM_CASES)
    assert a.read_bytes() == b.read_bytes()
    assert "np.float64" not in a.read_text()
    assert a.read_text().splitlines()[0] == ",".join(gen.COLUMNS)
