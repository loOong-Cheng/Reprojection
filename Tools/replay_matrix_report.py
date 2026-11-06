"""Summary of a replay matrix (Tools/replay_matrix.py): tables (markdown), one CSV of every value, charts.

Usage: python replay_matrix_report.py <matrix_dir> <out_dir>
Writes <out_dir>/tables.md (included into the write-up by hand), matrix.csv, latency.png, gap.png.
"""

import argparse
import csv
import json
import math
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from replay_matrix import LATENCIES, MOTIONS, STATIONS  # noqa: E402

METHODS = ["M0", "M1", "M2", "M3", "M4f0", "M4f1", "M4f2"]
NAMES = {"M0": "M0 head-locked", "M1": "M1 sphere 1 m", "M2": "M2 sphere adaptive", "M3": "M3 mesh",
         "M4f0": "M4 + sphere", "M4f1": "M4 + nearest bg", "M4f2": "M4 + memory"}
METRICS = [("reg_deg", "Misregistration (deg)", 2), ("slip_dps", "Slip (deg/s)", 1), ("flicker", "Flicker", 2),
           ("lpips", "LPIPS", 3), ("psnr", "PSNR (dB)", 1)]
LAT_P50 = {"ideal": 28, "wifi": 54, "remote": 94, "bad": 281}
# Categorical slots 1-7 of the reference palette (dataviz skill), fixed order.
COLORS = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6"]


def load(root):
    rows, gaps = [], []
    for m in MOTIONS:
        for s in STATIONS:
            for l in LATENCIES:
                d = os.path.join(root, m, s, l)
                mj = os.path.join(d, "metrics.json")
                if not os.path.exists(mj):
                    continue
                j = json.load(open(mj))
                for meth in METHODS:
                    o = j["overall"].get(meth)
                    if not o:
                        continue
                    r = {"motion": m, "station": s, "latency": l, "method": meth}
                    for k, _, _ in METRICS:
                        r[k] = o[k]["mean"]
                        r[k + "_p95"] = o[k]["p95"]
                    # Share of frames where under 5% of GT's view is in the method's image (misregistration not measurable).
                    r["unmatched"] = o.get("unmatched_share")
                    rows.append(r)
                # The gap the shown frame leaves: left eye vs the shown frame's left camera (same in every method's run).
                fc = os.path.join(d, "GT", "frames.csv")
                if os.path.exists(fc):
                    with open(fc, newline="") as f:
                        fr = list(csv.DictReader(f))
                    off = [float(x["off_cm"]) for x in fr]
                    rot = [float(x["rot_deg"]) for x in fr]
                    age = [float(x["age_ms"]) for x in fr]
                    gaps.append({"motion": m, "station": s, "latency": l, "off_mean": sum(off) / len(off), "off_max": max(off),
                                 "rot_mean": sum(rot) / len(rot), "rot_max": max(rot), "age_mean": sum(age) / len(age),
                                 "age_max": max(age)})
    return rows, gaps


def mean(v):
    v = [x for x in v if x is not None and not (isinstance(x, float) and math.isnan(x))]
    return sum(v) / len(v) if v else None


def table(rows, axis, values, metric, prec, label):
    """Rows: methods; columns: the values of `axis`; cells: mean of `metric` over everything else."""
    out = ["| Method | " + " | ".join(label(v) for v in values) + " |", "|---|" + "---|" * len(values)]
    for meth in METHODS:
        cells = []
        for v in values:
            x = mean([r[metric] for r in rows if r["method"] == meth and r[axis] == v])
            cells.append("-" if x is None else ("%." + str(prec) + "f") % x)
        out.append("| %s | %s |" % (NAMES[meth], " | ".join(cells)))
    return "\n".join(out)


def best_counts(rows, metric, higher_better=False):
    """How often each method is best over all configurations."""
    by_cfg = defaultdict(dict)
    for r in rows:
        by_cfg[(r["motion"], r["station"], r["latency"])][r["method"]] = r[metric]
    wins = defaultdict(int)
    for cfg, v in by_cfg.items():
        v = {k: x for k, x in v.items() if x is not None}
        if v:
            wins[(max if higher_better else min)(v, key=v.get)] += 1
    return wins, len(by_cfg)


def plot_latency(rows, out, metric, ylabel, log=True):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams.update({"font.family": "Segoe UI", "font.size": 9, "axes.edgecolor": "#e4e3df", "axes.labelcolor": "#52514e",
                         "xtick.color": "#52514e", "ytick.color": "#52514e", "axes.titlesize": 10})
    motions = [m for m in MOTIONS if any(r["motion"] == m for r in rows)]
    cols = 4
    nrow = (len(motions) + cols - 1) // cols
    fig, axes = plt.subplots(nrow, cols, figsize=(12, 2.9 * nrow + 0.6), sharex=True, sharey=True, facecolor="#fcfcfb", squeeze=False)
    lats = list(LATENCIES)
    xs = [LAT_P50[l] for l in lats]
    for k, m in enumerate(motions):
        ax = axes[k // cols][k % cols]
        ax.set_facecolor("#fcfcfb")
        ax.grid(axis="y", color="#e4e3df", linewidth=0.8)
        for s in ("top", "right"):
            ax.spines[s].set_visible(False)
        for c, meth in enumerate(METHODS):
            ys = [mean([r[metric] for r in rows if r["motion"] == m and r["latency"] == l and r["method"] == meth]) for l in lats]
            pts = [(x, y) for x, y in zip(xs, ys) if y is not None]
            if pts:
                ax.plot([p[0] for p in pts], [p[1] for p in pts], color=COLORS[c], linewidth=2, marker="o", markersize=4.5,
                        markeredgecolor="#fcfcfb", markeredgewidth=1.2, label=NAMES[meth])
        ax.set_title(m, loc="left", color="#0b0b0b")
        ax.set_xscale("log")
        ax.set_xticks(xs)
        ax.set_xticklabels([str(x) for x in xs])
        ax.minorticks_off()
        if log:
            ax.set_yscale("log")
    for k in range(len(motions), nrow * cols):
        axes[k // cols][k % cols].axis("off")
    for ax in axes[-1]:
        ax.set_xlabel("latency p50 (ms)")
    for row in axes:
        row[0].set_ylabel(ylabel)
    handles, labels = axes[0][0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper center", ncol=len(METHODS), frameon=False, fontsize=8.5)
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    fig.savefig(out, dpi=110, facecolor="#fcfcfb")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("matrix")
    ap.add_argument("out")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    rows, gaps = load(a.matrix)
    keys = ["motion", "station", "latency", "method"] + [k for m, _, _ in METRICS for k in (m, m + "_p95")] + ["unmatched"]
    with open(os.path.join(a.out, "matrix.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=keys)
        w.writeheader()
        for r in rows:
            w.writerow({k: (round(r[k], 4) if isinstance(r[k], float) else r[k]) for k in keys})
    with open(os.path.join(a.out, "gaps.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(gaps[0]) if gaps else ["motion"])
        w.writeheader()
        for g in gaps:
            w.writerow({k: (round(v, 3) if isinstance(v, float) else v) for k, v in g.items()})

    n_cfg = len({(r["motion"], r["station"], r["latency"]) for r in rows})
    md = ["<!-- generated by Tools/replay_matrix_report.py -->", "%d configurations, %d method runs scored." % (n_cfg, len(rows)), ""]
    lat_label = lambda l: "%s (%d ms)" % (l, LAT_P50[l])
    for metric, title, prec in METRICS:
        md += ["#### %s by latency (mean over motions and locations)" % title, "",
               table(rows, "latency", list(LATENCIES), metric, prec, lat_label), ""]
    for metric, title, prec in METRICS[:4]:
        md += ["#### %s by motion (mean over locations and latencies)" % title, "",
               table(rows, "motion", [m for m in MOTIONS if any(r["motion"] == m for r in rows)], metric, prec, str), ""]
    for metric, title, prec in METRICS[:4]:
        md += ["#### %s by robot location (mean over motions and latencies)" % title, "",
               table(rows, "station", list(STATIONS), metric, prec, str), ""]
    md += ["#### Frames where the view is lost (%): under 5% of GT's view found in the method's image (mean over locations)", "",
           "| Method | " + " | ".join("%s %s" % (m, l) for m in ("fastturn", "turn", "desk") for l in ("remote", "bad")) + " |",
           "|---|" + "---|" * 6]
    for meth in METHODS:
        cells = []
        for m in ("fastturn", "turn", "desk"):
            for l in ("remote", "bad"):
                x = mean([r["unmatched"] for r in rows if r["method"] == meth and r["motion"] == m and r["latency"] == l])
                cells.append("-" if x is None else "%.1f" % (100 * x))
        md.append("| %s | %s |" % (NAMES[meth], " | ".join(cells)))
    md += ["", "Elsewhere every method stays under 1% lost frames." if not any(
        (r["unmatched"] or 0) > 0.01 for r in rows if r["motion"] not in ("fastturn", "turn", "desk") or r["latency"] in ("ideal", "wifi")) else
           "Other configurations with more than 1% lost frames: " + ", ".join(sorted({"%s/%s/%s %s" % (r["motion"], r["station"], r["latency"], r["method"])
            for r in rows if (r["unmatched"] or 0) > 0.01 and (r["motion"] not in ("fastturn", "turn", "desk") or r["latency"] in ("ideal", "wifi"))})), ""]
    md += ["#### Best method per configuration", "", "| Metric | " + " | ".join(NAMES[m] for m in METHODS) + " |",
           "|---|" + "---|" * len(METHODS)]
    for metric, title, prec in METRICS:
        wins, n = best_counts(rows, metric, higher_better=metric == "psnr")
        md.append("| %s | %s |" % (title, " | ".join(str(wins.get(m, 0)) for m in METHODS)))
    md += ["", "#### Gap between the eye and the shown frame's camera (mean / max over the motion, mean over locations)", "",
           "| Motion | " + " | ".join("%s cm" % l for l in LATENCIES) + " | " + " | ".join("%s deg" % l for l in LATENCIES) + " |",
           "|---|" + "---|" * (2 * len(LATENCIES))]
    for m in MOTIONS:
        g = [x for x in gaps if x["motion"] == m]
        if not g:
            continue
        cm = ["%.1f / %.1f" % (mean([x["off_mean"] for x in g if x["latency"] == l]) or 0, max([x["off_max"] for x in g if x["latency"] == l] or [0])) for l in LATENCIES]
        dg = ["%.1f / %.1f" % (mean([x["rot_mean"] for x in g if x["latency"] == l]) or 0, max([x["rot_max"] for x in g if x["latency"] == l] or [0])) for l in LATENCIES]
        md.append("| %s | %s | %s |" % (m, " | ".join(cm), " | ".join(dg)))
    md += ["", "#### Misregistration (deg) per configuration", ""]
    for m in MOTIONS:
        rm = [r for r in rows if r["motion"] == m]
        if not rm:
            continue
        md += ["**%s**" % m, "", "| Location | Latency | " + " | ".join(NAMES[x] for x in METHODS) + " |", "|---|---|" + "---|" * len(METHODS)]
        for s in STATIONS:
            for l in LATENCIES:
                cells = [next(("%.2f" % r["reg_deg"] for r in rm if r["station"] == s and r["latency"] == l and r["method"] == x and r["reg_deg"] is not None), "-") for x in METHODS]
                if any(c != "-" for c in cells):
                    md.append("| %s | %s | %s |" % (s, l, " | ".join(cells)))
        md.append("")
    open(os.path.join(a.out, "tables.md"), "w", encoding="utf-8").write("\n".join(md))
    if rows:
        plot_latency(rows, os.path.join(a.out, "misregistration_latency.png"), "reg_deg", "misregistration (deg)")
        plot_latency(rows, os.path.join(a.out, "lpips_latency.png"), "lpips", "LPIPS", log=False)
    print("configs", n_cfg, "rows", len(rows), "->", a.out)


if __name__ == "__main__":
    main()
