"""Compare two CSV tables cell by cell, numbers within a tolerance.

`cmp` is too strict for a reference table written by NumPy and SciPy: another NumPy build can change
the last binary digit of a value without any change in meaning (found in CI: numpy 2.5.4 against the
table frozen with 2.5.3). Headers and text cells must match exactly, the shape must match, and
numbers must agree to `rtol` relative or `atol` absolute, so real drift is still caught.

    python python/compare_csv.py new.csv frozen.csv [--rtol 1e-10] [--atol 1e-15]

Exit status 0 if the tables agree, 1 if they differ, 2 on a usage or read error.
"""

from __future__ import annotations

import argparse
import csv
import math
import sys
from pathlib import Path


def _read(path: Path) -> list[list[str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.reader(handle))


def _as_float(text: str) -> float | None:
    try:
        return float(text)
    except ValueError:
        return None


def differences(a: list[list[str]], b: list[list[str]], rtol: float, atol: float) -> list[str]:
    """Human-readable differences (at most a handful); an empty list means the tables agree."""
    if len(a) != len(b):
        return [f"row counts differ: {len(a)} against {len(b)}"]
    found: list[str] = []
    for r, (row_a, row_b) in enumerate(zip(a, b, strict=True), start=1):
        if len(row_a) != len(row_b):
            return [f"row {r}: {len(row_a)} columns against {len(row_b)}"]
        for c, (x, y) in enumerate(zip(row_a, row_b, strict=True), start=1):
            if x == y:
                continue
            fx, fy = _as_float(x), _as_float(y)
            if r == 1 or fx is None or fy is None:
                found.append(f"row {r}, column {c}: {x!r} against {y!r}")
            elif not math.isclose(fx, fy, rel_tol=rtol, abs_tol=atol):
                found.append(f"row {r}, column {c}: {x} against {y}")
            if len(found) >= 5:
                return found
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("new", type=Path)
    parser.add_argument("frozen", type=Path)
    parser.add_argument("--rtol", type=float, default=1e-10)
    parser.add_argument("--atol", type=float, default=1e-15)
    args = parser.parse_args()
    try:
        new, frozen = _read(args.new), _read(args.frozen)
    except OSError as exc:
        print(f"cannot read: {exc}", file=sys.stderr)
        return 2
    found = differences(new, frozen, args.rtol, args.atol)
    if found:
        print("tables differ:\n  " + "\n  ".join(found), file=sys.stderr)
        return 1
    print(f"tables agree ({len(new) - 1} data rows, rtol {args.rtol:g}, atol {args.atol:g})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
