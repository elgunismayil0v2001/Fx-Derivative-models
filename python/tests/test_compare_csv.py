"""The CSV comparison CI uses instead of `cmp`: tolerant of last-digit noise, strict otherwise."""

from pathlib import Path

import compare_csv as cc

ROOT = Path(__file__).resolve().parents[2]

BASE = [["type", "x", "y"], ["call", "1.0", "0.039877611676744970"], ["put", "2.0", "3.6e-9"]]


def _copy(rows):
    return [list(r) for r in rows]


def test_identical_tables_agree():
    assert cc.differences(BASE, _copy(BASE), 1e-10, 1e-15) == []


def test_last_digit_noise_is_accepted():
    noisy = _copy(BASE)
    noisy[1][2] = "0.03987761167674498"  # one unit in the last place
    noisy[2][2] = "3.6000000000000004e-9"
    assert cc.differences(noisy, BASE, 1e-10, 1e-15) == []


def test_real_drift_is_caught():
    drifted = _copy(BASE)
    drifted[1][2] = (
        "0.0398776117"  # 1e-10 away is still fine; 4e-11 absolute is not for the tight rtol
    )
    assert cc.differences(drifted, BASE, 1e-12, 1e-15) != []
    drifted[1][2] = "0.0399"
    assert cc.differences(drifted, BASE, 1e-10, 1e-15) != []


def test_header_text_and_shape_must_match_exactly():
    renamed = _copy(BASE)
    renamed[0][1] = "spot"
    assert cc.differences(renamed, BASE, 1e-10, 1e-15) != []
    text = _copy(BASE)
    text[1][0] = "put"
    assert cc.differences(text, BASE, 1e-10, 1e-15) != []
    assert cc.differences(BASE[:2], BASE, 1e-10, 1e-15) != []
    wide = _copy(BASE)
    wide[1].append("1")
    assert cc.differences(wide, BASE, 1e-10, 1e-15) != []


def test_the_frozen_gk_table_is_a_well_formed_table_that_agrees_with_itself():
    frozen = ROOT / "tests" / "validation" / "data" / "gk_reference.csv"
    rows = cc._read(frozen)
    assert len(rows) == 617  # header and 616 cases
    assert cc.differences(rows, cc._read(frozen), 1e-10, 1e-15) == []
