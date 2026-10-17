"""Tests for the QE reference generator.

The C++ test `test_mc_qe_path_reference.cpp` trusts the table, so its source is tested too:
properties that must hold for the scheme itself (positivity, branch coverage, conditional moments of
the variance step, the martingale property, the antithetic mirror) and reproducibility of the
committed table. Needs no third-party package.
"""

import csv
import math
from pathlib import Path

import gen_qe_reference as gen
import pytest

TABLE = (
    Path(__file__).resolve().parents[2]
    / "tests"
    / "validation"
    / "data"
    / "mc_qe_path_reference.csv"
)


def _variance_step(v, dt, z, kappa, theta, xi, psi_c=1.5):
    """The variance step alone, as in the paper (a one-step path with rho = 0 exposes v')."""
    path = gen.qe_path(1, 0, False, 1, 1.0, 0.0, 0.0, dt, v, kappa, theta, xi, 0.0, psi_c)
    return path.variance


def test_variance_step_matches_exact_conditional_mean_and_variance():
    # Monte Carlo over streams: the sample mean and variance of v' must match the exact CIR
    # moments m and s2, in both branches. 4 standard errors, fixed seed, so this is deterministic.
    kappa, theta, xi, dt = 1.5, 0.04, 0.6, 0.25
    e = math.exp(-kappa * dt)
    for v in (0.2, 0.04, 0.002):  # psi small (quadratic) .. psi large (exponential)
        m = theta + (v - theta) * e
        s2 = v * xi**2 * e * (1 - e) / kappa + theta * xi**2 * (1 - e) ** 2 / (2 * kappa)
        n = 40_000
        draws = [
            gen.qe_path(5, k, False, 1, 1.0, 0.0, 0.0, dt, v, kappa, theta, xi, 0.0, 1.5).variance
            for k in range(n)
        ]
        mean = sum(draws) / n
        var = sum((d - mean) ** 2 for d in draws) / (n - 1)
        assert abs(mean - m) < 4.0 * math.sqrt(s2 / n)
        # Standard error of a sample variance depends on the kurtosis; 10% is far outside noise
        # for a wrong formula and far inside it for the right one at n = 40000.
        assert var == pytest.approx(s2, rel=0.10)


def test_both_branches_are_exercised_by_the_table():
    with TABLE.open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    assert sum(int(r["quadratic_steps"]) for r in rows) > 500
    assert sum(int(r["exponential_steps"]) for r in rows) > 500
    assert sum(1 for r in rows if int(r["fallback_steps"]) > 0) >= 4


def test_spot_is_positive_and_variance_non_negative():
    for stream in range(300):
        p = gen.qe_path(3, stream, False, 8, 1.0, 0.0, 0.0, 2.0, 0.04, 0.5, 0.04, 1.0, -0.9, 1.5)
        assert p.spot > 0.0
        assert p.variance >= 0.0


def test_spot_is_a_martingale_to_within_sampling_error():
    # E[S_T] = forward, thanks to the martingale correction. Parameters where Euler is badly off.
    rd, rf, t = 0.03, 0.01, 2.0
    n = 20_000
    vals = [
        gen.qe_path(8, k, False, 8, 1.1, rd, rf, t, 0.04, 0.5, 0.04, 1.0, -0.9, 1.5).spot
        for k in range(n)
    ]
    mean = sum(vals) / n
    sd = math.sqrt(sum((x - mean) ** 2 for x in vals) / (n - 1))
    assert abs(mean - 1.1 * math.exp((rd - rf) * t)) < 4.0 * sd / math.sqrt(n)


def test_antithetic_path_uses_the_negated_normals():
    # One step, quadratic branch (v0 = theta = 0.04, xi = 0.3: psi is small): the new variance is
    # a (b + z)^2 for the ordinary path and a (b - z)^2 for the mirror path, with z the first
    # normal of the stream. Computed here from the formulas, not through the generator.
    v, kappa, theta, xi, dt = 0.04, 2.0, 0.04, 0.3, 1.0
    e = math.exp(-kappa * dt)
    m = theta + (v - theta) * e
    s2 = v * xi**2 * e * (1 - e) / kappa + theta * xi**2 * (1 - e) ** 2 / (2 * kappa)
    psi = s2 / m**2
    assert psi < 1.5
    b2 = 2 / psi - 1 + math.sqrt(2 / psi) * math.sqrt(2 / psi - 1)
    a = m / (1 + b2)
    z, _ = gen.normal_pair(4, 9, 0)
    args = (1, 1.0, 0.0, 0.0, dt, v, kappa, theta, xi, 0.0, 1.5)
    plus = gen.qe_path(4, 9, False, *args).variance
    minus = gen.qe_path(4, 9, True, *args).variance
    assert plus == pytest.approx(a * (math.sqrt(b2) + z) ** 2, rel=1e-13)
    assert minus == pytest.approx(a * (math.sqrt(b2) - z) ** 2, rel=1e-13)


def test_committed_table_is_reproducible_from_the_script():
    rows = gen.build_rows()
    with TABLE.open(newline="") as handle:
        committed = list(csv.DictReader(handle))
    assert len(committed) == len(rows) >= 150
    for expected, got in zip(rows, committed, strict=True):
        for column in ("seed", "stream", "antithetic", "steps"):
            assert int(got[column]) == expected[column]
        for column in gen.COLUMNS[4:16]:
            assert float(got[column]) == pytest.approx(expected[column], rel=1e-12, abs=1e-14)
        for column in ("quadratic_steps", "exponential_steps", "fallback_steps"):
            assert int(got[column]) == expected[column]
