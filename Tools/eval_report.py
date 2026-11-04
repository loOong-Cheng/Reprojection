"""Builds the evaluation report page (HTML + JPEG images) from teleop_eval runs and their metrics.json.

Usage:
    python eval_report.py --out <dir> --run zedlike=<run_dir> [--run depthgt=<run_dir>] [--vrfps <vr_fps.csv>] [--notes notes.json]
Writes <dir>/index.html and <dir>/img/<run>/<scene>_<method>.jpg (+ _regions.jpg, the class map of the scene).
"""

import argparse
import csv
import html
import json
import os

import numpy as np
from PIL import Image

METHOD_NAMES = {
    "GT": ("GT", "ground truth: the remote world seen from the eye now"),
    "M0": ("M0", "head-locked frame, no compensation"),
    "M1": ("M1", "sphere, fixed R = 1 m"),
    "M2": ("M2", "sphere, adaptive R (2 s median)"),
    "M3": ("M3", "depth mesh, holes left dark"),
    "M4f0s2": ("M4 f0", "mesh + sphere fill, adaptive R"),
    "M4f1s2": ("M4 f1", "mesh + nearest background, adaptive R"),
    "M4f2s2": ("M4 f2", "mesh + memory, then nearest bg, adaptive R"),
    "M4f0s1": ("M4 f0 R1", "mesh + sphere fill, R = 1 m"),
    "M4f1s1": ("M4 f1 R1", "mesh + nearest background, R = 1 m"),
    "M4f2s1": ("M4 f2 R1", "mesh + memory, then nearest bg, R = 1 m"),
}
# (key, label, unit, higher_is_better, decimals)
METRICS = [
    ("psnr", "PSNR", "dB", True, 1),
    ("ssim", "SSIM", "", True, 3),
    ("lpips", "LPIPS", "", False, 3),
    ("mae_V", "Err V", "", False, 1),
    ("mae_D", "Err D", "", False, 1),
    ("mae_N", "Err N", "", False, 1),
    ("shift_px", "Shift", "px", False, 1),
    ("ghost", "False edges", "%", False, 1),
    ("gpu_ms", "GPU", "ms", False, 2),
]
REGION_RGB = {"V": (150, 156, 160), "D": (232, 120, 40), "N": (130, 90, 200), "O": (30, 34, 38)}


def classes(path):
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.int32)
    return np.argmax(np.stack([a[..., 1], a[..., 0], a[..., 2]], -1), -1)


def regions_image(run_dir, sid):
    cg = classes(os.path.join(run_dir, "%s_maskGT.png" % sid))
    cd = classes(os.path.join(run_dir, "%s_maskDeg.png" % sid))
    out = np.zeros(cg.shape + (3,), np.uint8)
    out[(cg == 0) & (cd == 0)] = REGION_RGB["V"]
    out[(cd == 1) & (cg == 0)] = REGION_RGB["N"]
    out[cg == 1] = REGION_RGB["D"]
    out[cg == 2] = REGION_RGB["O"]
    return Image.fromarray(out)


def save_jpg(img, path, width=960):
    img = img.convert("RGB")
    h = round(img.height * width / img.width)
    img.resize((width, h), Image.LANCZOS).save(path, "JPEG", quality=82, optimize=True)


def fmt(v, d):
    return "&ndash;" if v is None else ("%.*f" % (d, v))


def mean(vals):
    vals = [v for v in vals if v is not None]
    return sum(vals) / len(vals) if vals else None


def tint_attr(v, lo, hi, higher_better):
    """data-t in [0, 1]: 1 = best of the column, 0 = worst."""
    if v is None or lo is None or hi is None or hi - lo < 1e-9:
        return ""
    t = (v - lo) / (hi - lo)
    if not higher_better:
        t = 1 - t
    return ' style="--t:%.2f"' % t


def build(args):
    runs = []
    for spec in args.run:
        name, d = spec.split("=", 1)
        m = json.load(open(os.path.join(d, "metrics.json")))
        sc = json.load(open(os.path.join(d, "scenes.json")))
        runs.append({"name": name, "dir": d, "metrics": m, "scenes": sc})
    notes = json.load(open(args.notes)) if args.notes else {}
    vr = {}
    if args.vrfps and os.path.exists(args.vrfps):
        for r in csv.DictReader(open(args.vrfps)):
            vr[r["config"]] = r

    os.makedirs(args.out, exist_ok=True)
    if not args.no_images:
        for r in runs:
            idir = os.path.join(args.out, "img", r["name"])
            os.makedirs(idir, exist_ok=True)
            for s in r["scenes"]["scenes"]:
                sid = s["id"]
                for m in ["GT"] + r["metrics"]["methods"]:
                    p = os.path.join(r["dir"], "%s_%s.png" % (sid, m))
                    if os.path.exists(p):
                        save_jpg(Image.open(p), os.path.join(idir, "%s_%s.jpg" % (sid, m)))
                save_jpg(regions_image(r["dir"], sid), os.path.join(idir, "%s_regions.jpg" % sid))

    methods = runs[0]["metrics"]["methods"]
    control = notes.get("control_scene", "S12")

    def summary_table(r):
        rows = r["metrics"]["rows"]
        means = {}
        for m in methods:
            mr = [x for x in rows if x["method"] == m and x["scene"] != control]
            means[m] = {k: mean([x[k] for x in mr]) for k, *_ in METRICS}
        out = ['<div class="tablewrap"><table class="summary"><thead><tr><th class="mcol">Method</th>']
        for k, label, unit, hb, d in METRICS:
            out.append('<th><span class="mh">%s</span><span class="dir">%s%s</span></th>' % (
                label, "&uarr;" if hb else "&darr;", (" " + unit) if unit else ""))
        if vr:
            out.append('<th><span class="mh">VR fps</span><span class="dir">&uarr; 72 Hz</span></th>'
                       '<th><span class="mh">VR GPU</span><span class="dir">&darr; ms</span></th>')
        out.append("</tr></thead><tbody>")
        cols = {}
        for k, *_ in METRICS:
            vals = [means[m][k] for m in methods if means[m][k] is not None]
            cols[k] = (min(vals), max(vals)) if vals else (None, None)
        for m in methods:
            short, long_ = METHOD_NAMES.get(m, (m, ""))
            out.append('<tr><th class="mcol"><span class="mk">%s</span><span class="ml">%s</span></th>' % (short, long_))
            for k, label, unit, hb, d in METRICS:
                v = means[m][k]
                out.append('<td class="num heat"%s>%s</td>' % (tint_attr(v, *cols[k], hb), fmt(v, d)))
            if vr:
                rr = vr.get(m, {})
                out.append('<td class="num">%s</td><td class="num">%s</td>' % (rr.get("fps_median") or "&ndash;", rr.get("gpu_median_ms") or "&ndash;"))
            out.append("</tr>")
        out.append("</tbody></table></div>")
        return "".join(out)

    def scene_table(r):
        info = {s["scene"]: s for s in r["metrics"]["scenes"]}
        out = ['<div class="tablewrap"><table class="scenes"><thead><tr><th>Scene</th><th>View</th><th>In view</th>'
               '<th>Eye vs camera<br><span class="dir">fwd, right, up cm</span></th><th>Turn<br><span class="dir">deg</span></th>'
               '<th>Frame age<br><span class="dir">ms</span></th><th>M2 R<br><span class="dir">cm</span></th>'
               '<th>Key&shy;frames</th><th>V / D / N / O<br><span class="dir">% of frame</span></th></tr></thead><tbody>']
        for s in r["scenes"]["scenes"]:
            i = info.get(s["id"], {})
            off = (i.get("offset_cm") or "").split(",")
            off_s = ", ".join("%+.1f" % float(x) for x in off) if len(off) == 3 else "&ndash;"
            out.append('<tr><td class="sid"><a href="#%s">%s</a></td><td>%s</td><td class="content">%s</td><td class="num">%s</td>'
                       '<td class="num">%s</td><td class="num">%s</td><td class="num">%s</td><td class="num">%s</td>'
                       '<td class="num regions"><span class="rv">%.0f</span> / <span class="rd">%.0f</span> / <span class="rn">%.0f</span> / <span class="ro">%.0f</span></td></tr>' % (
                           s["id"], s["id"], html.escape(s["name"]), html.escape(s.get("content", "")), off_s,
                           i.get("rot_deg", "&ndash;"), i.get("age_ms", "&ndash;"), i.get("R_adapt", "&ndash;"), i.get("keyframes", "&ndash;"),
                           i.get("V%", 0), i.get("D%", 0), i.get("N%", 0), i.get("O%", 0)))
        out.append("</tbody></table></div>")
        return "".join(out)

    def scene_block(r, s):
        sid = s["id"]
        rows = {x["method"]: x for x in r["metrics"]["rows"] if x["scene"] == sid}
        cols = {}
        for k, *_ in METRICS:
            vals = [rows[m][k] for m in methods if m in rows and rows[m][k] is not None]
            cols[k] = (min(vals), max(vals)) if vals else (None, None)
        img = "img/%s/%s_%%s.jpg" % (r["name"], sid)
        out = ['<div class="scene-run" data-run="%s">' % r["name"]]
        out.append('<div class="refs"><figure><button class="shot" data-src="%s" data-gt="%s" data-cap="%s · GT"><img loading="lazy" src="%s" alt="%s GT"></button>'
                   '<figcaption>GT</figcaption></figure>' % (img % "GT", img % "GT", sid, img % "GT", sid))
        out.append('<figure><button class="shot" data-src="%s" data-gt="%s" data-cap="%s · regions"><img loading="lazy" src="%s" alt="%s regions"></button>'
                   '<figcaption>Regions: <span class="rv">V</span> <span class="rd">D</span> <span class="rn">N</span> <span class="ro">O</span></figcaption></figure></div>' % (
                       img % "regions", img % "GT", sid, img % "regions", sid))
        out.append('<div class="tablewrap"><table class="per"><thead><tr><th class="tcol">Image</th><th class="mcol">Method</th>')
        for k, label, unit, hb, d in METRICS:
            out.append('<th><span class="mh">%s</span><span class="dir">%s%s</span></th>' % (label, "&uarr;" if hb else "&darr;", (" " + unit) if unit else ""))
        out.append("</tr></thead><tbody>")
        for m in methods:
            if m not in rows:
                continue
            short, long_ = METHOD_NAMES.get(m, (m, ""))
            out.append('<tr><td class="tcol"><button class="shot" data-src="%s" data-gt="%s" data-cap="%s · %s · %s"><img loading="lazy" src="%s" alt="%s %s"></button></td>'
                       '<th class="mcol"><span class="mk">%s</span><span class="ml">%s</span></th>' % (
                           img % m, img % "GT", sid, short, html.escape(long_), img % m, sid, m, short, long_))
            for k, label, unit, hb, d in METRICS:
                v = rows[m][k]
                out.append('<td class="num heat"%s>%s</td>' % (tint_attr(v, *cols[k], hb), fmt(v, d)))
            out.append("</tr>")
        out.append("</tbody></table></div></div>")
        return "".join(out)

    run_names = [r["name"] for r in runs]
    run_label = {"zedlike": "ZEDlike depth", "depthgt": "Perfect depth (DepthGT)"}
    tabs = "".join('<button class="tab" data-run="%s" aria-pressed="%s">%s</button>' % (n, "true" if i == 0 else "false", run_label.get(n, n))
                   for i, n in enumerate(run_names))

    parts = []
    parts.append(PAGE_HEAD)
    parts.append('<header class="top"><div class="eyebrow">Reprojection · remote VST teleoperation simulator</div>'
                 '<h1>Method Bench</h1><p class="lede">%s</p><dl class="facts">%s</dl></header>' % (
                     notes.get("lede", ""), "".join("<div><dt>%s</dt><dd>%s</dd></div>" % (k, v) for k, v in notes.get("facts", []))))
    if notes.get("findings"):
        parts.append('<section><h2>What the numbers say</h2><ul class="findings">%s</ul></section>' % "".join("<li>%s</li>" % f for f in notes["findings"]))
    parts.append('<section><h2>Mean over the %d offset scenes</h2><p class="note">%s</p>' % (
        len([s for s in runs[0]["scenes"]["scenes"] if s["id"] != control]), notes.get("summary_note", "")))
    parts.append('<div class="tabs" role="group" aria-label="Depth model">%s</div>' % tabs)
    for r in runs:
        parts.append('<div class="run-panel" data-run="%s">%s</div>' % (r["name"], summary_table(r)))
    parts.append("</section>")
    # Scene x method matrix for any metric (rendered by the page script from the embedded data).
    data = {r["name"]: {"rows": [{k: x[k] for k in ["scene", "method"] + [m[0] for m in METRICS]} for x in r["metrics"]["rows"]],
                        "scenes": [s["id"] for s in r["scenes"]["scenes"]]} for r in runs}
    meta = {"metrics": [{"key": k, "label": l, "unit": u, "hb": hb, "d": d} for k, l, u, hb, d in METRICS],
            "methods": [{"key": m, "short": METHOD_NAMES.get(m, (m, ""))[0], "long": METHOD_NAMES.get(m, (m, ""))[1]} for m in methods],
            "names": {s["id"]: s["name"] for s in runs[0]["scenes"]["scenes"]}}
    parts.append('<section><h2>Scene &times; method</h2><p class="note">%s</p><div class="tabs" role="group" aria-label="Depth model">%s</div>'
                 '<div class="tabs metric-tabs" role="group" aria-label="Metric">%s</div><div class="tablewrap"><table class="matrix"></table></div></section>' % (
                     notes.get("matrix_note", ""), tabs,
                     "".join('<button class="mtab" data-metric="%s" aria-pressed="%s">%s</button>' % (k, "true" if k == "lpips" else "false", l)
                             for k, l, *_ in METRICS)))
    parts.append('<script type="application/json" id="bench-data">%s</script>' % json.dumps({"data": data, "meta": meta}).replace("</", "<\/"))
    if notes.get("metrics"):
        parts.append('<section><h2>Metrics</h2><div class="metric-notes">%s</div></section>' % "".join(
            '<div class="mnote"><h3>%s</h3><p>%s</p></div>' % (h, p) for h, p in notes["metrics"]))
    parts.append('<section><h2>Scenes</h2><p class="note">%s</p>%s</section>' % (notes.get("scenes_note", ""), scene_table(runs[0])))
    parts.append('<section><h2>Every scene and method</h2><p class="note">%s</p><div class="tabs" role="group" aria-label="Depth model">%s</div>' % (
        notes.get("per_scene_note", ""), tabs))
    for s in runs[0]["scenes"]["scenes"]:
        parts.append('<article class="scene" id="%s"><h3><span class="sid">%s</span> %s</h3><p class="content">%s</p>' % (
            s["id"], s["id"], html.escape(s["name"]), html.escape(s.get("content", ""))))
        for r in runs:
            parts.append(scene_block(r, s))
        parts.append("</article>")
    parts.append("</section>")
    if notes.get("fps"):
        parts.append('<section><h2>Frame rate</h2>%s</section>' % notes["fps"])
    if notes.get("caveats"):
        parts.append('<section><h2>Limits of this test</h2><ul class="findings">%s</ul></section>' % "".join("<li>%s</li>" % c for c in notes["caveats"]))
    if notes.get("protocol"):
        parts.append('<section><h2>Protocol</h2>%s</section>' % notes["protocol"])
    parts.append(PAGE_TAIL)
    open(os.path.join(args.out, "index.html"), "w", encoding="utf-8").write("\n".join(parts))
    print("wrote", os.path.join(args.out, "index.html"))


PAGE_HEAD = """<title>Reprojection Method Bench</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Sans+Condensed:wght@500;600&family=IBM+Plex+Sans:wght@400;500;600&family=IBM+Plex+Mono:wght@400;500&display=swap">
<style>
/* Layout: a lab notebook page. Summary first, then the scene atlas; tables scroll inside their own frames. */
:root {
  --bg: #f4f6f6; --surface: #ffffff; --ink: #172024; --muted: #5a676c; --rule: #d3dadc;
  --accent: #0b6a87; --good: #2e8b57; --bad: #c4553a;
  --rv: #8a9296; --rd: #d9782b; --rn: #7a56c2; --ro: #2a2f33;
  --display: "IBM Plex Sans Condensed", "Arial Narrow", sans-serif;
  --body: "IBM Plex Sans", system-ui, sans-serif;
  --mono: "IBM Plex Mono", ui-monospace, "Cascadia Mono", monospace;
}
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
  --bg: #0f1416; --surface: #161d20; --ink: #e3e9ea; --muted: #93a2a7; --rule: #2a3438;
  --accent: #4fb4d6; --good: #4cc07f; --bad: #e0775c; --rv: #8d969a; --ro: #6b747a; color-scheme: dark } }
:root[data-theme="dark"] {
  --bg: #0f1416; --surface: #161d20; --ink: #e3e9ea; --muted: #93a2a7; --rule: #2a3438;
  --accent: #4fb4d6; --good: #4cc07f; --bad: #e0775c; --rv: #8d969a; --ro: #6b747a; color-scheme: dark }
body { background: var(--bg); color: var(--ink); font: 15px/1.55 var(--body);
  padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: max(16px, env(safe-area-inset-right, 0px)); padding-block: 28px 64px; }
main { max-width: 1240px; margin: 0 auto; display: grid; gap: 44px; }
h1, h2, h3 { font-family: var(--display); font-weight: 600; text-wrap: balance; margin: 0; letter-spacing: 0.005em; }
h1 { font-size: clamp(34px, 5vw, 52px); line-height: 1.02; }
h2 { font-size: 26px; border-top: 2px solid var(--ink); padding-top: 10px; }
h3 { font-size: 19px; }
p { margin: 0; max-width: 72ch; }
a { color: var(--accent); }
section { display: grid; gap: 16px; min-width: 0; }
.eyebrow { font: 500 12px/1 var(--mono); letter-spacing: 0.08em; text-transform: uppercase; color: var(--muted); }
.top { display: grid; gap: 14px; }
.lede { font-size: 17px; max-width: 78ch; }
.facts { display: grid; grid-template-columns: repeat(auto-fit, minmax(190px, 1fr)); gap: 10px 22px; margin: 6px 0 0; }
.facts div { border-left: 2px solid var(--rule); padding-left: 10px; }
.facts dt { font: 500 11px/1.3 var(--mono); letter-spacing: 0.06em; text-transform: uppercase; color: var(--muted); }
.facts dd { margin: 2px 0 0; font-size: 14px; }
.note { color: var(--muted); }
.findings { margin: 0; padding-left: 1.2em; display: grid; gap: 8px; max-width: 90ch; }
.findings b { font-weight: 600; }
.tablewrap { overflow-x: auto; border: 1px solid var(--rule); background: var(--surface); border-radius: 4px; }
table { border-collapse: collapse; width: 100%; font-size: 13.5px; }
th, td { padding: 7px 10px; text-align: left; vertical-align: middle; border-bottom: 1px solid var(--rule); }
thead th { font: 500 12px/1.25 var(--body); color: var(--muted); background: var(--bg); position: sticky; top: 0; white-space: nowrap; }
.mh { display: block; color: var(--ink); font-weight: 600; }
.dir { font: 400 11px/1.2 var(--mono); color: var(--muted); }
td.num { font: 400 13px/1.2 var(--mono); font-variant-numeric: tabular-nums; text-align: right; white-space: nowrap; }
td.heat { background: color-mix(in oklab, var(--good) calc(var(--t, -1) * 26%), color-mix(in oklab, var(--bad) calc((1 - var(--t, 1)) * 22%), transparent)); }
.mcol { min-width: 190px; font-weight: 400; }
.mk { display: block; font: 600 13.5px/1.2 var(--mono); }
.ml { display: block; font-size: 12px; color: var(--muted); line-height: 1.3; }
.tcol { width: 196px; padding: 5px 8px; }
.shot { display: block; padding: 0; border: 1px solid var(--rule); background: var(--ro); cursor: zoom-in; border-radius: 2px; line-height: 0; }
.shot:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
.shot img { width: 180px; height: auto; aspect-ratio: 960 / 542; display: block; }
.scenes td.content, p.content { color: var(--muted); font-size: 13px; }
.scenes td.content { min-width: 220px; }
.sid { font: 600 13px/1 var(--mono); color: var(--accent); }
.regions .rv { color: var(--rv); } .rd { color: var(--rd); } .rn { color: var(--rn); } .ro { color: var(--muted); }
figcaption .rv, figcaption .rd, figcaption .rn, figcaption .ro { font-weight: 600; }
.tabs { display: flex; flex-wrap: wrap; gap: 6px; }
.tab { font: 500 13px/1 var(--body); padding: 8px 12px; border: 1px solid var(--rule); background: var(--surface); color: var(--ink); border-radius: 3px; cursor: pointer; }
.tab[aria-pressed="true"] { background: var(--ink); color: var(--bg); border-color: var(--ink); }
.tab:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
.scene { display: grid; gap: 10px; padding-top: 14px; border-top: 1px solid var(--rule); scroll-margin-top: 12px; }
.scene h3 { display: flex; gap: 10px; align-items: baseline; }
.refs { display: flex; flex-wrap: wrap; gap: 12px; margin-bottom: 10px; }
.refs figure { margin: 0; display: grid; gap: 4px; }
.refs .shot img { width: 360px; max-width: 100%; }
.refs figcaption { font-size: 12px; color: var(--muted); }
.scene-run { display: grid; gap: 0; }
body:not([data-run="depthgt"]) [data-run="depthgt"].run-panel, body:not([data-run="depthgt"]) .scene-run[data-run="depthgt"],
body[data-run="depthgt"] .run-panel[data-run="zedlike"], body[data-run="depthgt"] .scene-run[data-run="zedlike"] { display: none; }
.metric-notes { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 14px 24px; }
.mnote { min-width: 0; }
.mnote h3 { font-size: 16px; margin-bottom: 4px; }
.mnote p { font-size: 14px; }
.lightbox { position: fixed; inset: 0; z-index: 20; background: color-mix(in oklab, var(--bg) 92%, black); display: grid;
  grid-template-rows: auto 1fr auto; gap: 8px; padding: calc(12px + env(safe-area-inset-top, 0px)) max(12px, env(safe-area-inset-right, 0px)) calc(12px + env(safe-area-inset-bottom, 0px)) max(12px, env(safe-area-inset-left, 0px)); }
.lightbox .bar { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; justify-content: space-between; }
.lightbox .cap { font: 500 14px/1.3 var(--mono); }
.lightbox .stage { min-height: 0; display: grid; place-items: center; }
.lightbox .stage img { max-width: 100%; max-height: 100%; object-fit: contain; }
.lightbox .hint { font-size: 12px; color: var(--muted); }
.lightbox button { font: 500 13px/1 var(--body); padding: 8px 12px; border: 1px solid var(--rule); background: var(--surface); color: var(--ink); border-radius: 3px; cursor: pointer; }
.lightbox button[aria-pressed="true"] { background: var(--accent); color: var(--bg); border-color: var(--accent); }
.mtab { font: 500 12px/1 var(--mono); padding: 7px 10px; border: 1px solid var(--rule); background: var(--surface); color: var(--ink); border-radius: 3px; cursor: pointer; }
.mtab[aria-pressed="true"] { background: var(--accent); color: var(--bg); border-color: var(--accent); }
.mtab:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
table.matrix th.sname { min-width: 210px; font-weight: 400; }
table.matrix th.sname .sid { margin-right: 6px; }
table.matrix tr.mean th, table.matrix tr.mean td { border-top: 2px solid var(--ink); font-weight: 600; }
.fpsgrid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 16px; }
@media (max-width: 560px) { .tcol { width: 128px; } .shot img { width: 116px; } .mcol { min-width: 150px; } }
@media (prefers-reduced-motion: no-preference) { .shot img { transition: opacity .15s; } .shot:hover img { opacity: .9; } }
</style>
<main>"""

PAGE_TAIL = """</main>
<div class="lightbox" hidden role="dialog" aria-modal="true" aria-label="Screenshot viewer">
  <div class="bar"><span class="cap"></span><span>
    <button type="button" class="flip" aria-pressed="false">Show GT (G)</button>
    <button type="button" class="prev">&larr; Prev</button><button type="button" class="next">Next &rarr;</button>
    <button type="button" class="close">Close (Esc)</button></span></div>
  <div class="stage"><img alt=""></div>
  <div class="hint">Hold G or the button to flip to GT at the same pose. Arrow keys step through the methods of this scene.</div>
</div>
<script>
(function () {
  const body = document.body;
  function setRun(run) {
    body.dataset.run = run;
    document.querySelectorAll('.tab').forEach(t => t.setAttribute('aria-pressed', String(t.dataset.run === run)));
    try { localStorage.setItem('bench-run', run); } catch (e) {}
  }
  document.querySelectorAll('.tab').forEach(t => t.addEventListener('click', () => setRun(t.dataset.run)));
  let saved = null; try { saved = localStorage.getItem('bench-run'); } catch (e) {}
  setRun(saved && document.querySelector('.tab[data-run="' + saved + '"]') ? saved : (document.querySelector('.tab') || {dataset: {run: 'zedlike'}}).dataset.run);

  const bench = JSON.parse(document.getElementById('bench-data').textContent);
  let metric = 'lpips';
  function renderMatrix() {
    const run = body.dataset.run, d = bench.data[run] || Object.values(bench.data)[0];
    const M = bench.meta.metrics.find(m => m.key === metric), methods = bench.meta.methods;
    const val = (s, m) => { const r = d.rows.find(x => x.scene === s && x.method === m); return r ? r[metric] : null; };
    const fmtv = v => v === null || v === undefined ? '–' : v.toFixed(M.d);
    let h = '<thead><tr><th>Scene</th>' + methods.map(m => '<th title="' + m.long + '"><span class="mh">' + m.short + '</span></th>').join('') + '</tr></thead><tbody>';
    const rowsHtml = (label, vals) => {
      const ok = vals.filter(v => v !== null && v !== undefined);
      const lo = Math.min(...ok), hi = Math.max(...ok);
      return label + vals.map(v => {
        let st = '';
        if (v !== null && v !== undefined && hi - lo > 1e-9) { let t = (v - lo) / (hi - lo); if (!M.hb) t = 1 - t; st = ' style="--t:' + t.toFixed(2) + '"'; }
        return '<td class="num heat"' + st + '>' + fmtv(v) + '</td>';
      }).join('') + '</tr>';
    };
    for (const s of d.scenes) h += rowsHtml('<tr><th class="sname"><a class="sid" href="#' + s + '">' + s + '</a>' + (bench.meta.names[s] || '') + '</th>', methods.map(m => val(s, m.key)));
    const off = d.scenes.filter(s => s !== 'S12');
    const means = methods.map(m => { const v = off.map(s => val(s, m.key)).filter(x => x !== null && x !== undefined); return v.length ? v.reduce((a, b) => a + b, 0) / v.length : null; });
    h += rowsHtml('<tr class="mean"><th class="sname">Mean, offset scenes</th>', means);
    document.querySelector('table.matrix').innerHTML = h + '</tbody>';
    document.querySelectorAll('.mtab').forEach(t => t.setAttribute('aria-pressed', String(t.dataset.metric === metric)));
  }
  document.querySelectorAll('.mtab').forEach(t => t.addEventListener('click', () => { metric = t.dataset.metric; renderMatrix(); }));
  document.querySelectorAll('.tab').forEach(t => t.addEventListener('click', renderMatrix));
  renderMatrix();

  const lb = document.querySelector('.lightbox'), img = lb.querySelector('.stage img'), cap = lb.querySelector('.cap');
  const flip = lb.querySelector('.flip');
  let list = [], idx = 0, showingGT = false;
  function show() {
    const b = list[idx]; if (!b) return;
    img.src = showingGT ? b.dataset.gt : b.dataset.src;
    cap.textContent = showingGT ? b.dataset.cap.split(' · ')[0] + ' · GT' : b.dataset.cap;
    flip.setAttribute('aria-pressed', String(showingGT));
  }
  function open(b) {
    const run = b.closest('.scene-run');
    list = Array.from((run || document).querySelectorAll('.shot'));
    idx = Math.max(0, list.indexOf(b)); showingGT = false; lb.hidden = false; show(); flip.focus();
  }
  function close() { lb.hidden = true; img.removeAttribute('src'); }
  document.querySelectorAll('main .shot').forEach(b => b.addEventListener('click', () => open(b)));
  lb.querySelector('.close').addEventListener('click', close);
  lb.querySelector('.prev').addEventListener('click', () => { idx = (idx + list.length - 1) % list.length; show(); });
  lb.querySelector('.next').addEventListener('click', () => { idx = (idx + 1) % list.length; show(); });
  const on = () => { showingGT = true; show(); }, off = () => { showingGT = false; show(); };
  flip.addEventListener('pointerdown', on); flip.addEventListener('pointerup', off); flip.addEventListener('pointerleave', () => { if (showingGT) off(); });
  flip.addEventListener('keydown', e => { if (e.key === ' ' || e.key === 'Enter') { e.preventDefault(); on(); } });
  flip.addEventListener('keyup', e => { if (e.key === ' ' || e.key === 'Enter') off(); });
  document.addEventListener('keydown', e => {
    if (lb.hidden) return;
    if (e.key === 'Escape') close();
    else if (e.key === 'ArrowRight') { idx = (idx + 1) % list.length; show(); }
    else if (e.key === 'ArrowLeft') { idx = (idx + list.length - 1) % list.length; show(); }
    else if ((e.key === 'g' || e.key === 'G') && !e.repeat) on();
  });
  document.addEventListener('keyup', e => { if (!lb.hidden && (e.key === 'g' || e.key === 'G')) off(); });
})();
</script>"""


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--run", action="append", required=True)
    ap.add_argument("--vrfps", default=None)
    ap.add_argument("--notes", default=None)
    ap.add_argument("--no-images", action="store_true")
    build(ap.parse_args())
