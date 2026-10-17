"""Independent checks of the fxval-price example configurations and their frozen outputs.

The C++ regression tests (tests/regression) only say "the output did not change". This file says
the frozen numbers are RIGHT, by pricing the same options with independent Python code:

* Garman-Kohlhagen: closed form with SciPy's normal distribution (python/gen_gk_reference.py).
* Heston semi-analytic: the textbook P1/P2 integrals with QUADPACK (python/gen_heston_reference.py).
* Heston Monte Carlo (QE, antithetic, control variate): the independent semi-analytic price,
  within 4 reported standard errors.

It also checks that the shipped configurations satisfy the JSON Schema, and that the schema
rejects damaged copies, so the schema cannot silently drift into accepting everything.
"""

import copy
import json
import math
from pathlib import Path

import pytest

pytest.importorskip("numpy")
pytest.importorskip("scipy")
jsonschema = pytest.importorskip("jsonschema")

import gen_gk_reference as gk  # noqa: E402
import gen_heston_reference as heston  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
EXAMPLES = ROOT / "configs" / "examples"
GOLDEN = ROOT / "tests" / "regression" / "expected"
SCHEMA = json.loads((ROOT / "configs" / "schema" / "price_config.schema.json").read_text())

NAMES = ["gk_vanillas", "heston_semi_analytic_smile", "heston_mc_qe"]


def load(name):
    return json.loads((EXAMPLES / f"{name}.json").read_text())


def golden(name):
    return json.loads((GOLDEN / f"{name}.results.json").read_text())


@pytest.mark.parametrize("name", NAMES)
def test_examples_satisfy_the_schema(name):
    jsonschema.validate(load(name), SCHEMA)


def damaged_copies():
    base = load("heston_mc_qe")
    cases = {}

    def damage(label, edit):
        config = copy.deepcopy(base)
        edit(config)
        cases[label] = config

    damage("unknown top-level key", lambda c: c.update(extra=1))
    damage("missing seed", lambda c: c["model"]["monte_carlo"].pop("seed"))
    damage("negative seed", lambda c: c["model"]["monte_carlo"].update(seed=-1))
    damage("fractional paths", lambda c: c["model"]["monte_carlo"].update(paths=10.5))
    damage("rate in percent", lambda c: c["market"].update(domestic_rate=3.0))
    damage("bad scheme", lambda c: c["model"].update(scheme="milstein"))
    damage("qe with xi zero", lambda c: c["model"].update(xi=0.0))
    damage("bad id", lambda c: c["options"][0].update(id="has space"))
    damage("id starts with formula char", lambda c: c["options"][0].update(id="=SUM(A1)"))
    damage("expiry zero", lambda c: c["options"][0].update(expiry_years=0))
    damage("empty options", lambda c: c.update(options=[]))
    damage("wrong version", lambda c: c.update(format_version=2))
    damage("antithetic as text", lambda c: c["model"]["monte_carlo"].update(antithetic="yes"))
    damage("unknown mc key", lambda c: c["model"]["monte_carlo"].update(gpu=True))
    return cases


@pytest.mark.parametrize("label", sorted(damaged_copies()))
def test_schema_rejects_damaged_configurations(label):
    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(damaged_copies()[label], SCHEMA)


def test_gk_results_match_scipy():
    config, results = load("gk_vanillas"), golden("gk_vanillas")
    m, sigma = config["market"], config["model"]["volatility"]
    assert len(results) == len(config["options"])
    for option, row in zip(config["options"], results, strict=True):
        ref = gk.gk_reference(
            option["type"],
            m["spot"],
            option["strike"],
            m["domestic_rate"],
            m["foreign_rate"],
            sigma,
            option["expiry_years"],
        )
        assert row["id"] == option["id"]
        assert row["price"] == pytest.approx(ref["price"], rel=1e-12)
        notional = option.get("notional", 1.0)
        assert row["present_value"] == pytest.approx(ref["price"] * notional, rel=1e-12)
        g = row["greeks"]
        assert g["spot_delta"] == pytest.approx(ref["delta_spot"], rel=1e-10)
        assert g["forward_delta"] == pytest.approx(ref["delta_forward"], rel=1e-10)
        assert g["gamma"] == pytest.approx(ref["gamma"], rel=1e-10)
        assert g["vega"] == pytest.approx(ref["vega"], rel=1e-10)
        assert g["vanna"] == pytest.approx(ref["vanna"], rel=1e-9)
        assert g["volga"] == pytest.approx(ref["volga"], rel=1e-9)
        assert g["theta"] == pytest.approx(ref["theta"], rel=1e-9)
        assert g["rho_domestic"] == pytest.approx(ref["rho_d"], rel=1e-10)
        assert g["rho_foreign"] == pytest.approx(ref["rho_f"], rel=1e-10)


def heston_reference_price(config, option):
    m, p = config["market"], config["model"]
    call, put, _ = heston.heston_call_put(
        m["spot"],
        option["strike"],
        m["domestic_rate"],
        m["foreign_rate"],
        option["expiry_years"],
        p["v0"],
        p["kappa"],
        p["theta"],
        p["xi"],
        p["rho"],
    )
    return call if option["type"] == "call" else put


def test_heston_semi_analytic_results_match_the_textbook_integrals():
    config, results = load("heston_semi_analytic_smile"), golden("heston_semi_analytic_smile")
    for option, row in zip(config["options"], results, strict=True):
        assert row["id"] == option["id"]
        assert row["price"] == pytest.approx(heston_reference_price(config, option), abs=1e-9)


def test_heston_monte_carlo_results_are_consistent_with_the_independent_price():
    config, results = load("heston_mc_qe"), golden("heston_mc_qe")
    for option, row in zip(config["options"], results, strict=True):
        exact = heston_reference_price(config, option)
        z = (row["price"] - exact) / row["standard_error"]
        assert abs(z) < 4.0, (option["id"], z)
        assert 0.0 < row["standard_error"] < 1e-3


def test_put_call_parity_in_the_gk_example():
    config, results = load("gk_vanillas"), golden("gk_vanillas")
    m = config["market"]
    by_id = {row["id"]: row for row in results}
    call, put = by_id["EURUSD-1Y-ATM-C"], by_id["EURUSD-1Y-ATM-P"]
    forward_value = m["spot"] * math.exp(-m["foreign_rate"]) - 1.10 * math.exp(-m["domestic_rate"])
    assert call["price"] - put["price"] == pytest.approx(forward_value, rel=1e-12)
