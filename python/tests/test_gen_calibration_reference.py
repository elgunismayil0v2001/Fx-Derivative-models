"""Tests for the calibration reference generator.

The C++ validation tests (CAL-REF, V-11) trust the tables, so their source is tested too: the
tables must be reproducible (to rounding), the Black-Scholes inversion must be right on a known
case, the delta strikes must carry the stated delta under the quoted volatilities, and the
composition of the table (Feller split, conventions) must be what the V-11 header claims.
"""

import csv
import math
from pathlib import Path

import pytest

pytest.importorskip("numpy")
pytest.importorskip("scipy")

import gen_calibration_reference as gen  # noqa: E402
from scipy.stats import norm  # noqa: E402

DATA = Path(__file__).resolve().parents[2] / "tests" / "validation" / "data"


def _read(path: Path) -> list[dict[str, str]]:
    with path.open() as handle:
        return list(csv.DictReader(handle))


def test_committed_tables_are_reproducible(tmp_path):
    gen.main(str(tmp_path))
    for name in ("heston_iv_reference.csv", "calibration_reference.csv"):
        new, old = _read(tmp_path / name), _read(DATA / name)
        assert len(new) == len(old)
        for row_new, row_old in zip(new, old, strict=True):
            assert list(row_new) == list(row_old)
            for key in row_new:
                if key == "convention":
                    assert row_new[key] == row_old[key]
                else:
                    assert float(row_new[key]) == pytest.approx(
                        float(row_old[key]), rel=1e-8, abs=1e-10
                    )


def test_table_composition_matches_the_documented_design():
    rows = _read(DATA / "calibration_reference.csv")
    sets = {int(r["set"]): r for r in rows}
    assert len(sets) == gen.SETS
    assert len(rows) == gen.SETS * len(gen.EXPIRIES)
    feller = [
        2 * float(r["kappa"]) * float(r["theta"]) >= float(r["xi"]) ** 2 for r in sets.values()
    ]
    assert sum(feller) == 16 and len(feller) - sum(feller) == 8
    # Sets with index % 4 in (0, 1) were forced to satisfy Feller.
    for index, ok in zip(sorted(sets), feller, strict=True):
        if index % 4 in (0, 1):
            assert ok
    assert sum(r["convention"] == "spot" for r in sets.values()) == gen.SETS // 2
    assert len(_read(DATA / "heston_iv_reference.csv")) == gen.IV_CASES


def test_black_scholes_price_known_value():
    # ATM forward call, S = K = 1, r = 0, sigma = 20%, T = 1: price = 2 N(0.1) - 1.
    assert gen.bs_price("call", 1.0, 1.0, 0.0, 0.0, 0.2, 1.0) == pytest.approx(
        2 * norm.cdf(0.1) - 1, rel=1e-14
    )


def test_put_call_parity_of_the_black_scholes_helper():
    s, k, rd, rf, sig, t = 1.1, 1.2, 0.04, 0.025, 0.12, 1.5
    call = gen.bs_price("call", s, k, rd, rf, sig, t)
    put = gen.bs_price("put", s, k, rd, rf, sig, t)
    assert call - put == pytest.approx(s * math.exp(-rf * t) - k * math.exp(-rd * t), rel=1e-13)


def test_heston_implied_vol_is_flat_when_vol_of_vol_vanishes():
    # xi -> 0 with v0 = theta: Heston is Black-Scholes with sigma = sqrt(theta).
    for strike in (0.9, 1.1, 1.3):
        iv = gen.heston_iv(1.1, strike, 0.03, 0.01, 1.0, 0.04, 2.0, 0.04, 1e-3, 0.0)
        assert iv == pytest.approx(0.2, abs=1e-5)


def test_model_level_is_the_average_variance_vol():
    # v0 = theta: the expected integrated variance is theta * t whatever kappa is.
    assert gen.model_level(0.04, 3.0, 0.04, 2.0) == pytest.approx(0.2, rel=1e-14)


@pytest.mark.parametrize("convention", ["spot", "forward"])
def test_delta_quotes_put_wings_at_the_stated_delta(convention):
    s, rd, rf, t = 1.1, 0.04, 0.025, 0.5
    params = (0.0225, 1.8, 0.0324, 0.55, -0.35)
    atm, rr, bf = gen.delta_quotes(convention, s, rd, rf, t, params)
    sig_c, sig_p = atm + bf + rr / 2, atm + bf - rr / 2
    k_c = gen.strike_by_root_find("call", convention, 0.25, s, rd, rf, sig_c, t)
    k_p = gen.strike_by_root_find("put", convention, 0.25, s, rd, rf, sig_p, t)
    # The Heston implied volatility at those strikes IS the quoted wing volatility.
    assert gen.heston_iv(s, k_c, rd, rf, t, *params) == pytest.approx(sig_c, abs=1e-10)
    assert gen.heston_iv(s, k_p, rd, rf, t, *params) == pytest.approx(sig_p, abs=1e-10)
    assert rr < 0  # negative correlation: puts are richer


def test_negative_correlation_gives_negative_risk_reversal_and_positive_butterfly():
    atm, rr, bf = gen.delta_quotes("spot", 1.1, 0.04, 0.025, 1.0, (0.04, 1.5, 0.04, 0.6, -0.5))
    assert rr < 0 < bf
    assert atm > 0


def test_draw_set_respects_the_feller_request():
    rng = gen.np.random.default_rng(1)
    for _ in range(50):
        _, _, _, (_, kappa, theta, xi, _) = gen.draw_set(rng, force_feller=True)
        assert 2 * kappa * theta >= xi * xi
