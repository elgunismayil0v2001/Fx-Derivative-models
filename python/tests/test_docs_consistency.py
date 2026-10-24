"""The documentation is part of the evidence, so it is tested for internal consistency.

These checks catch the drift that builds up when a report is edited again and again: a finding cited
but never defined, a traceability matrix that no longer matches the catalogue, a version number
changed in one place only.
"""

import json
import re
from pathlib import Path

import build_traceability as bt

ROOT = Path(__file__).resolve().parents[2]
REPORT = (ROOT / "docs" / "validation_report.md").read_text(encoding="utf-8")
CATALOGUE = (ROOT / "docs" / "test_catalogue.md").read_text(encoding="utf-8")
CITING = [
    REPORT,
    CATALOGUE,
    (ROOT / "docs" / "model_documentation.md").read_text(encoding="utf-8"),
    (ROOT / "README.md").read_text(encoding="utf-8"),
]


def _findings() -> dict[str, list[str]]:
    rows = {}
    for line in REPORT.splitlines():
        if re.match(r"\| F-\d\d \|", line):
            cells = [c.strip() for c in line.strip().strip("|").split(" | ")]
            rows[cells[0]] = cells
    return rows


def _numbers(ids) -> list[int]:
    return sorted(int(i.split("-")[1]) for i in ids)


def test_findings_are_contiguous_and_every_citation_is_defined():
    defined = set(_findings())
    assert _numbers(defined) == list(range(1, len(defined) + 1))
    for text in CITING:
        for cited in set(re.findall(r"\bF-\d\d\b", text)):
            assert cited in defined, f"{cited} is cited but not defined in the report"


def test_limitations_are_contiguous_and_every_citation_is_defined():
    defined = set(re.findall(r"^\* \*\*(L-\d\d) \(", REPORT, flags=re.MULTILINE))
    assert _numbers(defined) == list(range(1, len(defined) + 1))
    for text in CITING:
        for cited in set(re.findall(r"\bL-\d\d\b", text)):
            assert cited in defined, f"{cited} is cited but not defined in the report"


def test_executive_summary_counts_match_the_findings_table():
    rows = _findings()
    severity = {}
    for cells in rows.values():
        severity[cells[1]] = severity.get(cells[1], 0) + 1
    assert f"{len(rows)} findings ({severity['medium']} medium, {severity['low']} low" in REPORT
    assert f"{severity['info']} informational)" in REPORT
    open_ids = {fid for fid, cells in rows.items() if cells[-1].startswith("Open")}
    listed = set(
        re.findall(
            r"^\| (F-\d\d) \| \w+ \| [^|]+ \| [^|]+ \|$", REPORT.split("## 1. Summary")[0], re.M
        )
    )
    assert listed == open_ids


def test_traceability_matrix_equals_the_one_built_from_the_catalogue():
    block = REPORT.split("<!-- traceability:start -->\n")[1].split("\n<!-- traceability:end -->")[0]
    assert block == bt.matrix(CATALOGUE, REPORT)


def test_every_validation_test_passes_and_cites_an_existing_file():
    rows = bt.catalogue_rows(CATALOGUE)
    assert [r["id"] for r in rows] == [f"V-{i:02d}" for i in range(1, 22)]
    for r in rows:
        assert r["status"] == "passing", r["id"]
        assert r["path"] and (ROOT / r["path"]).is_file(), f"{r['id']}: {r['path']}"


def test_the_version_is_the_same_everywhere():
    cmake = re.search(r"VERSION (\d+\.\d+\.\d+)", (ROOT / "CMakeLists.txt").read_text()).group(1)
    vcpkg = json.loads((ROOT / "vcpkg.json").read_text())["version"]
    consumer = (ROOT / "examples" / "consumer" / "CMakeLists.txt").read_text()
    major_minor = ".".join(cmake.split(".")[:2])
    readme = (ROOT / "README.md").read_text()
    assert vcpkg == cmake
    assert f"find_package(fxval {major_minor} " in consumer
    assert f"find_package(fxval {major_minor} " in readme
    assert f"library version {cmake}" in readme
    assert f"library {cmake}" in REPORT
    assert REPORT.startswith(
        f"# Validation report: FX derivative pricing and risk library, version {major_minor}"
    )
    assert f"## [{cmake}]" in (ROOT / "CHANGELOG.md").read_text()


def test_mutant_manifests_still_match_the_sources():
    # A mutant whose pattern no longer occurs exactly once silently tests nothing.
    manifests = sorted((ROOT / "scripts" / "mutants").glob("*.json"))
    assert len(manifests) >= 3
    for path in manifests:
        data = json.loads(path.read_text(encoding="utf-8"))
        assert data["mutants"], path.name
        for i, m in enumerate(data["mutants"]):
            text = (ROOT / m["file"]).read_text(encoding="utf-8")
            assert text.count(m["old"]) == 1, f"{path.name} #{i} ({m['label']}) is stale"
            assert m["old"] != m["new"], f"{path.name} #{i} changes nothing"


def test_every_fuzz_target_has_an_entry_point_a_corpus_and_a_mention_in_the_docs():
    doc = (ROOT / "docs" / "fuzzing.md").read_text(encoding="utf-8")
    targets = ["vanilla", "barrier", "delta", "risk", "credit", "sha256", "price_config"]
    for name in targets:
        assert (ROOT / "tests" / "fuzz" / f"fuzz_{name}.cpp").is_file(), name
        assert any((ROOT / "tests" / "fuzz" / "corpus" / name).glob("*")), f"no corpus for {name}"
        assert f"`{name}`" in doc, f"{name} is not described in docs/fuzzing.md"
