"""Draw the smile scenarios written by the C++ example `fxval-example-smile`.

    fxval-example-smile configs/scenarios/smile_scenarios.csv docs/results/smile_data
    python python/plot_smiles.py docs/results/smile_data docs/results

One PNG per scenario: volatility against standardised log-moneyness for each tenor (with the three
market pivots), and the butterfly density factor g, where g < 0 means negative implied density.
Quotes are ILLUSTRATIVE and SYNTHETIC (see configs/scenarios/smile_scenarios.csv).
"""

from __future__ import annotations

import csv
import math
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

# Categorical slots 1 to 6 of the reference palette, light surface, in fixed order.
SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300"]
SURFACE, INK, MUTED, GRID = "#fcfcfb", "#0b0b0b", "#898781", "#e1e0d9"
G_CLIP = (-1.0, 2.0)


def read(path: Path) -> list[dict[str, str]]:
    with path.open() as handle:
        return list(csv.DictReader(handle))


def style(ax) -> None:
    ax.set_facecolor(SURFACE)
    ax.grid(True, color=GRID, linewidth=0.6)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(MUTED)
    ax.tick_params(colors=MUTED, labelsize=8)


def plot_scenario(name, curves, pivots, checks, out: Path) -> None:
    tenors = list(dict.fromkeys(row["tenor"] for row in curves))
    fig, (ax_vol, ax_g) = plt.subplots(1, 2, figsize=(11, 4.2), facecolor=SURFACE)
    for ax in (ax_vol, ax_g):
        style(ax)
    for idx, tenor in enumerate(tenors):
        colour = SERIES[idx % len(SERIES)]
        rows = [r for r in curves if r["tenor"] == tenor]
        xs_v = [float(r["x"]) for r in rows if r["vol"]]
        vols = [100 * float(r["vol"]) for r in rows if r["vol"]]
        ax_vol.plot(xs_v, vols, color=colour, linewidth=2, label=tenor)
        xs_g = [float(r["x"]) for r in rows if r["density_factor"]]
        gs = [float(r["density_factor"]) for r in rows if r["density_factor"]]
        ax_g.plot(xs_g, gs, color=colour, linewidth=2, label=tenor)
        piv = next(p for p in pivots if p["tenor"] == tenor)
        # Pivot points at their standardised log-moneyness. The curve table has the strike at
        # x = 0 (the forward) and at x = 1, which gives the unit of x without recomputing it.
        by_x = {round(float(r["x"]), 6): float(r["strike"]) for r in rows}
        forward, one_sigma = by_x[0.0], by_x[1.0]
        scale = math.log(one_sigma / forward)
        for key_k, key_v in (
            ("strike_put", "vol_put"),
            ("strike_atm", "vol_atm"),
            ("strike_call", "vol_call"),
        ):
            x = math.log(float(piv[key_k]) / forward) / scale
            ax_vol.plot(
                x,
                100 * float(piv[key_v]),
                "o",
                color=colour,
                markersize=6,
                markeredgecolor=SURFACE,
                markeredgewidth=1.5,
            )
    ax_g.axhline(0.0, color=INK, linewidth=1)
    ax_g.set_ylim(*G_CLIP)
    ax_g.fill_between([-4, 4], G_CLIP[0], 0, color="#e34948", alpha=0.08, linewidth=0)
    ax_g.text(-3.9, -0.9, "g < 0: negative implied density", color=MUTED, fontsize=8)
    ax_vol.set_xlabel(
        "log-moneyness in standard deviations, ln(K/F) / (ATM vol x sqrt(T))",
        color=MUTED,
        fontsize=8,
    )
    ax_g.set_xlabel("same coordinate", color=MUTED, fontsize=8)
    ax_vol.set_ylabel("implied volatility, %", color=MUTED, fontsize=9)
    ax_g.set_ylabel("butterfly density factor g (clipped to [-1, 2])", color=MUTED, fontsize=9)
    ax_vol.set_title(
        f"{name}: smile (dots = 25-delta put, ATM, 25-delta call)",
        color=INK,
        fontsize=10,
        loc="left",
    )
    ax_g.set_title("no-arbitrage condition g >= 0", color=INK, fontsize=10, loc="left")
    ax_vol.legend(frameon=False, fontsize=8, ncol=2, labelcolor=INK)
    bad = [c for c in checks if c["kind"] == "butterfly" and int(c["violations"]) > 0]
    cal = [c for c in checks if c["kind"] == "calendar" and int(c["violations"]) > 0]
    note = []
    if bad:
        note.append("butterfly violations: " + ", ".join(c["tenor"] for c in bad))
    if cal:
        note.append("calendar violations at: " + ", ".join(c["tenor"] for c in cal))
    fig.suptitle(
        "ILLUSTRATIVE SYNTHETIC QUOTES. " + ("; ".join(note) if note else "no violations"),
        color=MUTED,
        fontsize=8,
        x=0.01,
        ha="left",
        y=0.01,
        va="bottom",
    )
    fig.tight_layout(rect=(0, 0.04, 1, 1))
    fig.savefig(out, dpi=130, facecolor=SURFACE)
    plt.close(fig)


def main(data_dir: str, out_dir: str) -> None:
    data, out = Path(data_dir), Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    curves, pivots, checks = (read(data / f"smile_{n}.csv") for n in ("curves", "pivots", "checks"))
    grouped: dict[str, dict[str, list]] = defaultdict(lambda: {"c": [], "p": [], "k": []})
    for row in curves:
        grouped[row["scenario"]]["c"].append(row)
    for row in pivots:
        grouped[row["scenario"]]["p"].append(row)
    for row in checks:
        grouped[row["scenario"]]["k"].append(row)
    for name, parts in grouped.items():
        plot_scenario(name, parts["c"], parts["p"], parts["k"], out / f"smile_{name}.png")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
