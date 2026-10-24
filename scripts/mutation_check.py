#!/usr/bin/env python3
"""Mutation check: plant one small change at a time and confirm the test suite notices.

A mutant is a literal source replacement (file, old text, new text, label). For each one the
script copies the repository to a scratch directory, applies the change, builds, runs ctest and
records whether any test failed ("killed"), none did ("SURVIVES", a gap in the tests) or the
change did not compile. The working tree is never modified.

  python3 scripts/mutation_check.py scripts/mutants/credit.json
  python3 scripts/mutation_check.py scripts/mutants/*.json --preset release --jobs 4
  python3 scripts/mutation_check.py scripts/mutants/credit.json --only 0 3 7

Exit status: 0 if every mutant was killed, 1 if any survived, 2 on a bad manifest or
a pattern that no longer matches exactly once (the manifest is then out of date).

Manifest (JSON): {"name": ..., "ctest_regex": "...", "ctest_exclude": "...", "mutants":
[{"file": "src/x.cpp", "old": "...", "new": "...", "label": "..."}]}
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKIP = {".git", "build", "build-asan", ".venv", "__pycache__"}


def copy_tree(dst: Path) -> None:
    def ignore(_dir: str, names: list[str]) -> list[str]:
        return [n for n in names if n in SKIP]

    shutil.copytree(ROOT, dst, ignore=ignore, dirs_exist_ok=True)


def run(cmd: list[str], cwd: Path, timeout: int) -> subprocess.CompletedProcess[str]:
    return subprocess.run(  # noqa: S603 - fixed argument lists, no shell
        cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout
    )


def check_one(work: Path, mutant: dict, manifest: dict, preset: str, jobs: int) -> tuple[str, str]:
    path = work / mutant["file"]
    original = path.read_text()
    count = original.count(mutant["old"])
    if count != 1:
        return "STALE", f"pattern occurs {count} times (must be exactly once)"
    path.write_text(original.replace(mutant["old"], mutant["new"]))
    try:
        build = run(["cmake", "--build", "--preset", preset, "-j", str(jobs)], work, timeout=3600)
        if build.returncode != 0:
            return "NO-COMPILE", "the mutant does not compile (not counted)"
        ctest = ["ctest", "--preset", preset, "-j", str(jobs), "--timeout", "300"]
        if manifest.get("ctest_regex"):
            ctest += ["-R", manifest["ctest_regex"]]
        if manifest.get("ctest_exclude"):
            ctest += ["-E", manifest["ctest_exclude"]]
        result = run(ctest, work, timeout=3600)
        failed = sorted(
            set(re.findall(r"\d+ - (\S+) \((?:Failed|Timeout|Subprocess aborted)", result.stdout))
        )
        if failed:
            return "KILLED", f"{len(failed)} failing: " + ", ".join(failed[:4])
        return "SURVIVES", "all selected tests still pass"
    finally:
        path.write_text(original)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("manifests", nargs="+", type=Path)
    parser.add_argument("--preset", default="release")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--only", type=int, nargs="*", help="mutant indices to run")
    parser.add_argument("--keep", action="store_true", help="keep the scratch copy")
    args = parser.parse_args()

    manifests = []
    for p in args.manifests:
        try:
            manifests.append(json.loads(p.read_text()))
        except (OSError, json.JSONDecodeError) as exc:
            print(f"cannot read {p}: {exc}", file=sys.stderr)
            return 2

    work = Path(tempfile.mkdtemp(prefix="fxval-mutation-"))
    try:
        copy_tree(work)
        cfg = run(["cmake", "--preset", args.preset], work, timeout=1800)
        if cfg.returncode != 0:
            print(cfg.stdout + cfg.stderr, file=sys.stderr)
            return 2
        survivors = stale = total = 0
        for manifest in manifests:
            print(f"== {manifest['name']}: {len(manifest['mutants'])} mutants", flush=True)
            for i, mutant in enumerate(manifest["mutants"]):
                if args.only and i not in args.only:
                    continue
                status, detail = check_one(work, mutant, manifest, args.preset, args.jobs)
                print(f"{i:3d} {status:10s} {mutant['label']}  [{detail}]", flush=True)
                total += status in ("KILLED", "SURVIVES")
                survivors += status == "SURVIVES"
                stale += status == "STALE"
        print(f"{total - survivors}/{total} killed, {survivors} survived, {stale} stale")
        if stale:
            return 2
        return 1 if survivors else 0
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
