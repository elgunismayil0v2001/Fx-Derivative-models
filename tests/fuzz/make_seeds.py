"""Write the seed corpus tests/fuzz/corpus/<target>/ (deterministic; run from the repository root).

Usage: python tests/fuzz/make_seeds.py

Byte-driven targets get "sane" seeds (first byte odd: values scaled into ranges) and "wild" seeds
(first byte even: raw doubles) that start near plausible values. The configuration parser gets the
shipped example configurations and a few hand-written fragments.
"""

import shutil
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "tests" / "fuzz" / "corpus"


def doubles(values: list[float]) -> bytes:
    return b"".join(struct.pack("<d", v) for v in values)


def sane_seeds() -> list[bytes]:
    return [
        bytes([1]) + bytes([128] * 60),
        bytes([1]) + bytes([0] * 60),
        bytes([1]) + bytes([255] * 60),
        bytes([1]) + bytes(range(60)),
        bytes([1]) + bytes(range(255, 195, -1)),
        bytes([3]) + bytes([(37 * i) % 256 for i in range(80)]),
    ]


def wild_seeds() -> list[bytes]:
    near = [1.1, 1.05, 0.03, 0.01, 0.5, 0.1, 0.25, 0.9, 1.3, 0.0, 1.0, 0.4, 0.02, 2.0]
    return [
        bytes([0]) + doubles(near),
        bytes([0]) + doubles([float("nan")] + near[1:]),
        bytes([0]) + doubles([float("inf")] * 3 + near[3:]),
        bytes([0]) + doubles([-1.0] * 14),
        bytes([0]) + doubles([5e-324, 1e308, -1e308, 0.0, -0.0] + near[5:]),
    ]


def write(target: str, seeds: list[bytes]) -> None:
    folder = OUT / target
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    for i, seed in enumerate(seeds):
        (folder / f"seed_{i:02d}.bin").write_bytes(seed)


def main() -> None:
    for target in ("vanilla", "barrier", "delta", "risk", "credit"):
        write(target, sane_seeds() + wild_seeds())
    write("sha256", [b"", b"abc", b"a" * 55, b"a" * 56, b"a" * 64, bytes(range(256))])
    config_seeds = [p.read_bytes() for p in sorted((ROOT / "configs" / "examples").glob("*.json"))]
    config_seeds += [b"{}", b'{"format_version": 1}', b"[", b'{"market": {"spot": 1.1}}', b""]
    write("price_config", config_seeds)


if __name__ == "__main__":
    main()
