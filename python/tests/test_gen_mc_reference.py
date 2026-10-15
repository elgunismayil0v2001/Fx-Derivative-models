"""Tests for the Monte Carlo reference generator.

The C++ test `test_mc_path_reference.cpp` trusts the table, so its source is tested too: the
integer generator must reproduce the published Philox known answers, the normals must behave like
normals, a path must obey identities that hold exactly, and the committed table must be
reproducible from the script. Needs no third-party package.
"""

import csv
import math
from pathlib import Path

import gen_mc_reference as gen
import pytest

TABLE = (
    Path(__file__).resolve().parents[2] / "tests" / "validation" / "data" / "mc_path_reference.csv"
)


def test_philox_known_answer_vectors_from_the_reference_implementation():
    # Random123, kat_vectors, philox4x32 with 10 rounds.
    assert gen.philox4x32_10((0, 0, 0, 0), (0, 0)) == (
        0x6627E8D5,
        0xE169C58D,
        0xBC57AC4C,
        0x9B00DBD8,
    )
    assert gen.philox4x32_10((0xFFFFFFFF,) * 4, (0xFFFFFFFF, 0xFFFFFFFF)) == (
        0x408F276D,
        0x41C83B0E,
        0xA20BC7C6,
        0x6D5451FD,
    )
    assert gen.philox4x32_10(
        (0x243F6A88, 0x85A308D3, 0x13198A2E, 0x03707344), (0xA4093822, 0x299F31D0)
    ) == (0xD16CFE09, 0x94FDCCEB, 0x5001E420, 0x24126EA1)


def test_normals_have_the_moments_of_a_standard_normal():
    n = 20_000
    values = []
    for block in range(n):
        values.extend(gen.normal_pair(12345, 6, block))
    count = len(values)
    mean = sum(values) / count
    variance = sum((v - mean) ** 2 for v in values) / count
    assert abs(mean) < 4.5 / math.sqrt(count)
    assert abs(variance - 1.0) < 4.5 * math.sqrt(2.0 / count)


def test_a_path_with_no_vol_of_vol_and_constant_variance_is_lognormal_in_form():
    # xi = 0 and v0 = theta: the variance never moves, so after one step
    # ln(S_T / S_0) = (rd - rf - v/2) T + sqrt(v T) * z_spot, with z_spot built from the first pair.
    spot, rd, rf, t, v, rho = 1.2, 0.03, 0.01, 0.5, 0.04, 0.3
    s_t, v_t = gen.heston_euler_path(9, 4, 1, spot, rd, rf, t, v, 2.0, v, 0.0, rho)
    z_v, z_p = gen.normal_pair(9, 4, 0)
    z_s = rho * z_v + math.sqrt(1 - rho * rho) * z_p
    expected = spot * math.exp((rd - rf - 0.5 * v) * t + math.sqrt(v * t) * z_s)
    assert s_t == pytest.approx(expected, rel=1e-14)
    assert v_t == v


def test_variance_can_go_negative_but_the_spot_stays_positive():
    seen_negative = False
    for stream in range(200):
        s_t, v_t = gen.heston_euler_path(
            1, stream, 16, 1.0, 0.0, 0.0, 2.0, 0.04, 0.5, 0.04, 1.0, -0.9
        )
        assert s_t > 0.0
        seen_negative = seen_negative or v_t < 0.0
    assert seen_negative


def test_committed_table_is_reproducible_from_the_script():
    rows = gen.build_rows()
    with TABLE.open(newline="") as handle:
        committed = list(csv.DictReader(handle))
    assert len(committed) == len(rows) >= 100
    for expected, got in zip(rows, committed, strict=True):
        for column in ("seed", "stream", "steps"):
            assert int(got[column]) == expected[column]
        for column in gen.COLUMNS[3:]:
            # Integers are exact; floats may differ in the last digits between libm versions.
            assert float(got[column]) == pytest.approx(expected[column], rel=1e-12, abs=1e-14)


def test_table_covers_the_boundary_cases():
    with TABLE.open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    assert any(int(r["seed"]) >= 2**32 for r in rows)
    assert any(int(r["stream"]) >= 2**32 for r in rows)
    assert any(float(r["xi"]) == 0.0 for r in rows)
    assert any(abs(float(r["rho"])) == 1.0 for r in rows)
    assert any(int(r["steps"]) == 1 for r in rows)
