"""Build the traceability matrix of the validation report from the test catalogue.

Usage: python python/build_traceability.py          (prints the Markdown table)

The matrix lists every validation test V-01 .. V-21 with its source file, the week it was built, its
status and the findings of the validation report that it produced. The catalogue
(docs/test_catalogue.md) and the findings table (docs/validation_report.md, section 6) are the only
inputs, so the matrix cannot drift from them: python/tests/test_docs_consistency.py compares the
table printed here with the one committed in the report.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOGUE = ROOT / "docs" / "test_catalogue.md"
REPORT = ROOT / "docs" / "validation_report.md"


def catalogue_rows(text: str) -> list[dict[str, str]]:
    """Rows of the catalogue table (V-01 .. V-21)."""
    rows = []
    for line in text.splitlines():
        if not re.match(r"\| V-\d\d \|", line):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split(" | ", 5)]
        path = re.search(r"`(tests/[^`]+)`", cells[5])
        rows.append(
            {
                "id": cells[0],
                "test": cells[1],
                "week": cells[3],
                "status": cells[4].split()[0],
                "path": path.group(1) if path else "",
            }
        )
    return rows


def findings_by_test(text: str) -> dict[str, list[str]]:
    """Map V-xx to the findings whose 'found by' cell names it."""
    found: dict[str, list[str]] = {}
    for line in text.splitlines():
        m = re.match(r"\| (F-\d+) \|", line)
        if not m:
            continue
        cells = [c.strip() for c in line.strip().strip("|").split(" | ")]
        for vid in re.findall(r"V-\d\d", cells[-2]):
            found.setdefault(vid, []).append(m.group(1))
    return found


def matrix(catalogue: str, report: str) -> str:
    found = findings_by_test(report)
    out = [
        "| ID | Test | Source file | Week | Status | Findings produced |",
        "|---|---|---|---|---|---|",
    ]
    for r in catalogue_rows(catalogue):
        path = f"`{r['path']}`" if r["path"] else "(none)"
        produced = ", ".join(found.get(r["id"], [])) or "none"
        out.append(
            f"| {r['id']} | {r['test']} | {path} | {r['week']} | {r['status']} | {produced} |"
        )
    return "\n".join(out)


def main() -> None:
    print(matrix(CATALOGUE.read_text(encoding="utf-8"), REPORT.read_text(encoding="utf-8")))


if __name__ == "__main__":
    if len(sys.argv) != 1:
        sys.exit(__doc__)
    main()
