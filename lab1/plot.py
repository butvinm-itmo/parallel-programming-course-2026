"""Builds the summary throughput chart (Mops/sec vs number of threads) from benchmark CSV.

Expected CSV columns: threads,mops and optionally impl. Without the impl column the file name is used, so results/mutex.csv is read as impl "mutex".
Several rows for the same (impl, threads) pair, across all given files, are reduced to their median.

Usage: python plot.py results/*.csv -o throughput.png [--linear]
"""

import argparse
import csv
import statistics
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter

SURFACE = "#fcfcfb"
TEXT_PRIMARY = "#0b0b0b"
TEXT_SECONDARY = "#52514e"
MUTED = "#898781"
GRID = "#e1e0d9"
BASELINE = "#c3c2b7"

# Fixed order: color follows the implementation, not its rank on the chart.
IMPLS = {
    "empty_lock": ("Этап 1: пустой лок", "#2a78d6", "o"),
    "mutex": ("Этап 1: единый лок", "#eb6834", "s"),
    "striped": ("Этап 2: шардированный лок", "#1baf7a", "D"),
    "thread_local": ("Этап 3: thread-local", "#eda100", "^"),
    "double_buffer": ("Этап 4: двойная буферизация", "#e87ba4", "v"),
}
BASELINE_IMPL = "single_thread"


def load(paths):
    runs = defaultdict(lambda: defaultdict(list))
    for path in paths:
        default_impl = Path(path).stem
        with open(path, newline="") as f:
            for row in csv.DictReader(f):
                impl = (row.get("impl") or default_impl).strip()
                runs[impl][int(row["threads"])].append(float(row["mops"]))
    return {impl: {t: statistics.median(v) for t, v in sorted(by_t.items())} for impl, by_t in runs.items()}


def plot(data, out, linear):
    fig, ax = plt.subplots(figsize=(9, 5.5), dpi=150)
    fig.patch.set_facecolor(SURFACE)
    ax.set_facecolor(SURFACE)

    threads = sorted({t for by_t in data.values() for t in by_t})

    unknown = set(data) - set(IMPLS) - {BASELINE_IMPL}
    if unknown:
        print(f"skipping unknown impl(s): {', '.join(sorted(unknown))}")

    for impl, (label, color, marker) in IMPLS.items():
        if impl not in data:
            continue
        xs = list(data[impl])
        ys = list(data[impl].values())
        ax.plot(
            xs, ys, label=label, color=color, linewidth=2, marker=marker, markersize=7,
            markeredgecolor=SURFACE, markeredgewidth=2, solid_joinstyle="round", solid_capstyle="round",
        )

    if BASELINE_IMPL in data:
        base = data[BASELINE_IMPL][min(data[BASELINE_IMPL])]
        ax.axhline(base, color=MUTED, linewidth=1, zorder=0, label=f"Этап 0: однопоточный baseline ({base:.0f})")

    ax.set_xscale("log", base=2)
    ax.set_xticks(threads)
    ax.xaxis.set_major_formatter(FuncFormatter(lambda x, _: f"{x:g}"))
    ax.xaxis.set_minor_locator(plt.NullLocator())
    if not linear:
        ax.set_yscale("log")
    ax.yaxis.set_major_formatter(FuncFormatter(lambda y, _: f"{y:,.0f}".replace(",", " ") if y >= 1 else f"{y:g}"))

    ax.set_xlabel("Число потоков", color=TEXT_SECONDARY)
    ax.set_ylabel("Млн оп/сек" + ("" if linear else " (логарифмическая шкала)"), color=TEXT_SECONDARY)
    ax.set_title("Пропускная способность record()", loc="left", color=TEXT_PRIMARY, fontsize=13)

    ax.grid(True, which="major", color=GRID, linewidth=1)
    ax.set_axisbelow(True)
    for side in ("top", "right", "left"):
        ax.spines[side].set_visible(False)
    ax.spines["bottom"].set_color(BASELINE)
    ax.tick_params(which="both", colors=MUTED, length=0)

    legend = ax.legend(loc="upper left", bbox_to_anchor=(1.01, 1), frameon=False, labelcolor=TEXT_PRIMARY)
    fig.tight_layout()
    fig.savefig(out, facecolor=SURFACE, bbox_extra_artists=[legend], bbox_inches="tight")
    print(f"saved {out}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", nargs="+", help="benchmark results: impl,threads,mops (one or more files)")
    parser.add_argument("-o", "--output", default="throughput.png")
    parser.add_argument("--linear", action="store_true", help="linear Y axis instead of logarithmic")
    args = parser.parse_args()
    plot(load(args.csv), args.output, args.linear)


if __name__ == "__main__":
    main()
