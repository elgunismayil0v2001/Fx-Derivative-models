"""Tests for the Heston reference generator.

The C++ validation tests trust the reference table, so its source is tested too: it must reduce
to Black-Scholes where theory says so, satisfy no-arbitrage identities, be deterministic in its
inputs, and the committed table must be reproducible from the script.
"""

import csv
import math
from pathlib import Path

import pytest

pytest.importorskip("numpy")
pytest.importorskip("scipy")

import gen_heston_reference as gen  # noqa: E402
import numpy as np  # noqa: E402
from scipy.stats import norm  # noqa: E402

TABLE = (
    Path(__file__).resolve().parents[2] / "tests" / "validation" / "data" / "heston_reference.csv"
)


def _black_scholes_call(spot, strike, rd, rf, tau, total_variance):
    forward = spot * math.exp((rd - rf) * tau)
    sd = math.sqrt(total_variance)
    d1 = (math.log(forward / strike) + 0.5 * total_variance) / sd
    return math.exp(-rd * tau) * (forward * norm.cdf(d1) - strike * norm.cdf(d1 - sd))


def test_reduces_to_black_scholes_for_small_vol_of_vol_and_zero_correlation():
    # With rho = 0 the gap to Black-Scholes is second order in xi: 0.05^2 times a small constant.
    spot, strike, rd, rf, tau = 1.1, 1.05, 0.03, 0.01, 1.0
    v0, kappa, theta = 0.04, 2.0, 0.05
    total_variance = theta * tau + (v0 - theta) * (1.0 - math.exp(-kappa * tau)) / kappa
    call, _, _ = gen.heston_call_put(spot, strike, rd, rf, tau, v0, kappa, theta, 0.05, 0.0)
    assert call == pytest.approx(
        _black_scholes_call(spot, strike, rd, rf, tau, total_variance), abs=2e-4
    )


def test_put_call_parity_and_bounds_hold_in_the_reference():
    for row in gen.make_inputs(np.random.default_rng(1))[:25]:
        spot, strike, rd, rf, tau = row[:5]
        call, put, _ = gen.heston_call_put(*row)
        parity = spot * math.exp(-rf * tau) - strike * math.exp(-rd * tau)
        assert call - put == pytest.approx(parity, abs=1e-9)
        assert 0.0 <= call <= spot * math.exp(-rf * tau)


def test_inputs_are_deterministic():
    first = gen.make_inputs(np.random.default_rng(gen.SEED))
    second = gen.make_inputs(np.random.default_rng(gen.SEED))
    assert first == second
    assert len(first) == gen.RANDOM_CASES


def test_committed_table_is_reproducible(tmp_path):
    # Neither inputs nor prices are compared as text. Prices come from quadrature, and inputs from
    # numpy's random generator plus exp/sqrt: the last digit can differ between platforms (fused
    # multiply-add on Apple silicon, different libm). Both are compared to 1e-12 relative, which
    # still catches any real change of the generator, the seed or the formulas.
    fresh = tmp_path / "heston_reference.csv"
    gen.write_table(fresh)
    with fresh.open() as a, TABLE.open() as b:
        new_rows, old_rows = list(csv.reader(a)), list(csv.reader(b))
    assert new_rows[0] == old_rows[0] == gen.COLUMNS
    assert len(new_rows) == len(old_rows)
    for new, old in zip(new_rows[1:], old_rows[1:], strict=True):
        assert [float(x) for x in new[:10]] == pytest.approx(
            [float(x) for x in old[:10]], rel=1e-12, abs=0.0
        )
        assert [float(x) for x in new[10:]] == pytest.approx(
            [float(x) for x in old[10:]], abs=1e-10
        )
