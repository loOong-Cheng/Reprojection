"""Misregistration over time per method (small multiples, M0 baseline in grey behind each), from replay_metrics.py.

Usage: python replay_plot.py <run_dir> <out.png> [--metric reg_deg]
"""

import argparse
import csv
import math
import os
import sys
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from replay_trajectory import read_segments  # noqa: E402

ORDER = ["M0", "M1", "M2", "M3", "M4f0", "M4f1", "M4f2"]
TITLE = {"M0": "M0 head-locked (baseline)", "M1": "M1 sphere, R 1 m", "M2": "M2 sphere, adaptive R", "M3": "M3 depth mesh",
         "M4f0": "M4 + sphere fill", "M4f1": "M4 + nearest background", "M4f2": "M4 + keyframe memory"}
SHORT_SEG = ["", "turn R", "grab", "carry", "drop", "lean R", "look L", ""]
SURFACE, INK, INK2, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"
SERIES, BASE = "#2a78d6", "#c3c2b7"
YLABEL = {"reg_deg": "misregistration (deg)", "slip_dps": "slip (deg/s)", "flicker": "flicker (0-255)", "lpips": "LPIPS"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run")
    ap.add_argument("out")
    ap.add_argument("--metric", default="reg_deg")
    a = ap.parse_args()
    SEGMENTS = read_segments(os.path.join(a.run, "trajectory.csv"))
    short = SHORT_SEG if len(SHORT_SEG) == len(SEGMENTS) else [""] + [n.split(" ")[0] for n, _, _ in SEGMENTS[1:-1]] + [""]

    series = defaultdict(list)
    with open(os.path.join(a.run, "metrics_frames.csv"), newline="") as f:
        for r in csv.DictReader(f):
            if r[a.metric]:
                series[r["method"]].append((float(r["t"]), float(r[a.metric])))
    head = []
    with open(os.path.join(a.run, "GT", "frames.csv"), newline="") as f:
        rows = list(csv.DictReader(f))
    for p, q in zip(rows, rows[1:]):
        dt = float(q["t"]) - float(p["t"])
        d = math.sqrt(sum((float(q[k]) - float(p[k])) ** 2 for k in ("head_yaw", "head_pitch", "head_roll")))
        head.append((float(q["t"]), d / dt))

    plt.rcParams.update({"font.family": "Segoe UI", "font.size": 9, "axes.edgecolor": GRID, "axes.labelcolor": INK2,
                         "xtick.color": INK2, "ytick.color": INK2, "axes.titlecolor": INK, "axes.titlesize": 10})
    fig, axes = plt.subplots(2, 4, figsize=(12, 5.2), sharex=True, facecolor=SURFACE)
    ymax = max(v for m in ORDER if m in series for _, v in series[m]) * 1.05
    t0, t1 = SEGMENTS[0][1], SEGMENTS[-1][2]

    def frame(ax):
        ax.set_facecolor(SURFACE)
        ax.grid(axis="y", color=GRID, linewidth=0.8)
        ax.set_axisbelow(True)
        for s in ("top", "right"):
            ax.spines[s].set_visible(False)
        for _, s0, _ in SEGMENTS[1:]:
            ax.axvline(s0, color=GRID, linewidth=0.8, zorder=0)
        ax.set_xlim(t0, t1)

    ax = axes[0][0]
    frame(ax)
    ax.fill_between([t for t, _ in head], [v for _, v in head], color=BASE, linewidth=0)
    ax.plot([t for t, _ in head], [v for _, v in head], color=INK2, linewidth=1.2)
    ax.set_title("Operator: head turn speed (deg/s)", loc="left")
    top = max(v for _, v in head)
    for k, ((name, s0, s1), short) in enumerate(zip(SEGMENTS, short)):
        if short:  # staggered so neighbours do not collide
            ax.text((s0 + s1) / 2, top * (1.03 if k % 2 else 1.16), short, ha="center", va="bottom", fontsize=7.5, color=INK2)
    ax.set_ylim(0, top * 1.3)

    for k, m in enumerate(ORDER):
        ax = axes[(k + 1) // 4][(k + 1) % 4]
        frame(ax)
        if m != "M0" and "M0" in series:
            ax.plot([t for t, _ in series["M0"]], [v for _, v in series["M0"]], color=BASE, linewidth=1.5, label="M0 baseline")
        if m in series:
            vals = [v for _, v in series[m]]
            ax.plot([t for t, _ in series[m]], vals, color=SERIES, linewidth=2, label=m)
            ax.set_title("%s   mean %.2f" % (TITLE[m], sum(vals) / len(vals)), loc="left")
        ax.set_ylim(0, ymax)
        if k + 1 == 1:
            ax.legend(handles=[plt.Line2D([], [], color=SERIES, linewidth=2, label="method"),
                               plt.Line2D([], [], color=BASE, linewidth=1.5, label="M0 baseline (other panels)")],
                      frameon=False, loc="upper left", fontsize=8)
    for ax in axes[1]:
        ax.set_xlabel("time (s)")
    axes[0][1].set_ylabel(YLABEL.get(a.metric, a.metric))
    axes[1][0].set_ylabel(YLABEL.get(a.metric, a.metric))
    fig.tight_layout()
    fig.savefig(a.out, dpi=110, facecolor=SURFACE)
    print(a.out)


if __name__ == "__main__":
    main()
