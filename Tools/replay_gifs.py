"""GIFs of a replayed trajectory (Tools/replay_run.ps1 + replay_metrics.py output).

Per method: <out>/<name>.gif, the spectator-screen layout: third-person view (observer) left, the operator's left eye
right, a footer with the time, what the operator is doing, the age of the shown robot frame and the live
misregistration. All methods replayed the same trajectory with the same time step, so frame k of every GIF shows the
same instant. Also <out>/compare.gif: the third-person view and all methods in one synchronized 3x3 grid.

Frames: every 6th frame of the 72 Hz replay (observer frames exist every 3rd), played at 12.5 fps (1.04x real time);
GIFs of moving 3D renders are large (~6 MB per method at 2 x 360 px).
Needs pillow and ffmpeg (palettegen / paletteuse) on PATH.

Usage: python replay_gifs.py <run_dir> <out_dir> [--tile 480] [--grid-tile 320]
"""

import argparse
import csv
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from replay_trajectory import SEGMENTS  # noqa: E402

ORDER = ["GT", "M0", "M1", "M2", "M3", "M4f0", "M4f1", "M4f2"]
LABEL = {
    "GT": "GT: ground truth (reference, no real system has it)",
    "M0": "M0 head-locked (baseline: the frame as received)",
    "M1": "M1 sphere reprojection, R = 1 m",
    "M2": "M2 sphere reprojection, adaptive R",
    "M3": "M3 depth mesh",
    "M4f0": "M4 mesh + sphere fill (fill 0, default)",
    "M4f1": "M4 mesh + nearest background (fill 1)",
    "M4f2": "M4 mesh + keyframe memory (fill 2)",
}
SHORT = {"GT": "GT (reference, now)", "M0": "M0 head-locked (baseline)", "M1": "M1 sphere R 1 m", "M2": "M2 sphere adaptive R",
         "M3": "M3 depth mesh", "M4f0": "M4 + sphere fill", "M4f1": "M4 + nearest background", "M4f2": "M4 + keyframe memory"}
BG = (16, 18, 20)
FG = (235, 235, 235)
DIM = (150, 155, 160)
ACCENT = (255, 196, 64)


def font(size, bold=False):
    for name in (("segoeuib.ttf", "arialbd.ttf") if bold else ("segoeui.ttf", "arial.ttf")):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def read_csv(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def segment_at(t):
    for name, t0, t1 in SEGMENTS:
        if t0 <= t < t1:
            return name
    return SEGMENTS[-1][0]


def footer(draw, x0, y0, w, h, t, t_start, t_end, info, f_small):
    """Timeline (segments as ticks, the current one highlighted), time, current action, live values."""
    seg = segment_at(t)
    draw.text((x0 + 10, y0 + 4), "t %.2f s   %s" % (t, seg), fill=FG, font=f_small)
    if info:
        tw = draw.textlength(info, font=f_small)
        draw.text((x0 + w - tw - 10, y0 + 4), info, fill=ACCENT, font=f_small)
    by = y0 + h - 9
    draw.rectangle([x0 + 10, by, x0 + w - 10, by + 3], fill=(60, 64, 70))
    sx = lambda tt: x0 + 10 + (w - 20) * (tt - t_start) / (t_end - t_start)
    for name, s0, s1 in SEGMENTS:
        draw.rectangle([sx(s0), by - 2, sx(s0) + 1, by + 5], fill=(110, 115, 120))
        if name == seg:
            draw.rectangle([sx(max(s0, t_start)), by, sx(min(s1, t_end)), by + 3], fill=(90, 96, 104))
    draw.rectangle([x0 + 10, by, sx(t), by + 3], fill=ACCENT)


DITHER = "none"


def encode(frames_dir, out_path, fps="100/8", colors=256):
    dither = "bayer:bayer_scale=4" if DITHER == "bayer" else DITHER
    vf = "split[a][b];[a]palettegen=max_colors=%d:stats_mode=full[p];[b][p]paletteuse=dither=%s:diff_mode=rectangle" % (colors, dither)
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(fps), "-i", os.path.join(frames_dir, "f_%04d.png"),
                    "-vf", vf, "-loop", "0", out_path], check=True)
    print(out_path, "%.1f MB" % (os.path.getsize(out_path) / 1e6))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run")
    ap.add_argument("out")
    ap.add_argument("--tile", type=int, default=360, help="width of each half of a per-method GIF")
    ap.add_argument("--grid-tile", type=int, default=232)
    ap.add_argument("--grid-colors", type=int, default=128)
    ap.add_argument("--every", type=int, default=6, help="use every n-th frame (needs an observer frame there)")
    ap.add_argument("--hz", type=float, default=72.0)
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--dither", default="none", help="ffmpeg paletteuse dither: none (smallest), bayer, sierra2_4a")
    a = ap.parse_args()
    global DITHER
    DITHER = a.dither
    # GIF delays are whole 1/100 s: 72 Hz / 6 = 12 Hz content -> 8 cs (12.5 fps, 1.04x real time).
    fps = "100/%d" % max(2, round(100 * a.every / a.hz))
    os.makedirs(a.out, exist_ok=True)
    methods = [m for m in ORDER if os.path.isdir(os.path.join(a.run, m))]
    states = {m: read_csv(os.path.join(a.run, m, "frames.csv")) for m in methods}
    metrics = {}
    mpath = os.path.join(a.run, "metrics_frames.csv")
    if os.path.exists(mpath):
        for r in read_csv(mpath):
            metrics[(r["method"], int(r["frame"]))] = r
    n = min(len(v) for v in states.values())
    idx = [i for i in range(0, n, a.every) if all(os.path.exists(os.path.join(a.run, m, "obs_%05d.png" % i)) for m in methods)]
    t_of = lambda i: float(states["GT"][i]["t"])
    t_start, t_end = t_of(idx[0]), t_of(n - 1)
    f_head, f_small = font(16, True), font(14)

    def info_for(m, i):
        age = float(states[m][i]["age_ms"])
        reg = metrics.get((m, i), {}).get("reg_deg", "")
        s = "shown frame %.0f ms old" % age
        if m == "GT":
            return "the remote world now (no latency)"
        return s + ("   misregistration %.1f deg" % float(reg) if reg else "")

    def eye(m, i, w):
        return Image.open(os.path.join(a.run, m, "eye_%05d.png" % i)).convert("RGB").resize((w, w * 9 // 16), Image.LANCZOS)

    # Per method: TPV | eye
    for m in methods:
        if a.only and m not in a.only:
            continue
        w = a.tile
        h = w * 9 // 16
        bar, foot = 26, 40
        tmp = tempfile.mkdtemp()
        try:
            for k, i in enumerate(idx):
                sheet = Image.new("RGB", (2 * w + 4, bar + h + foot), BG)
                sheet.paste(Image.open(os.path.join(a.run, m, "obs_%05d.png" % i)).convert("RGB").resize((w, h), Image.LANCZOS), (0, bar))
                sheet.paste(eye(m, i, w), (w + 4, bar))
                d = ImageDraw.Draw(sheet)
                d.text((8, 4), "Third-person view", fill=DIM, font=f_head)
                d.text((w + 12, 4), "Left eye: " + SHORT[m], fill=FG, font=f_head)
                footer(d, 0, bar + h, 2 * w + 4, foot, t_of(i), t_start, t_end, info_for(m, i), f_small)
                sheet.save(os.path.join(tmp, "f_%04d.png" % k))
            encode(tmp, os.path.join(a.out, "%s.gif" % m), fps)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    # Grid: TPV, GT, M0 / M1, M2, M3 / M4 fills
    if a.only is None or "compare" in a.only:
        gw = a.grid_tile
        gh = gw * 9 // 16
        lab, foot = 20, 40
        cells = ["TPV"] + methods
        cols = 3
        rows_n = (len(cells) + cols - 1) // cols
        f_lab = font(13, True)
        tmp = tempfile.mkdtemp()
        try:
            for k, i in enumerate(idx):
                sheet = Image.new("RGB", (cols * gw + (cols - 1) * 3, rows_n * (gh + lab) + foot), BG)
                d = ImageDraw.Draw(sheet)
                for c, m in enumerate(cells):
                    x, y = (c % cols) * (gw + 3), (c // cols) * (gh + lab)
                    if m == "TPV":
                        img = Image.open(os.path.join(a.run, "GT", "obs_%05d.png" % i)).convert("RGB").resize((gw, gh), Image.LANCZOS)
                        text, col = "Third-person view", DIM
                    else:
                        img = eye(m, i, gw)
                        reg = metrics.get((m, i), {}).get("reg_deg", "")
                        text = SHORT[m] + ("   %.1f deg" % float(reg) if reg and m != "GT" else "")
                        col = FG
                    sheet.paste(img, (x, y + lab))
                    d.text((x + 5, y + 2), text, fill=col, font=f_lab)
                age = float(states["M0"][i]["age_ms"]) if "M0" in states else 0.0
                footer(d, 0, rows_n * (gh + lab), sheet.width, foot, t_of(i), t_start, t_end,
                       "shown frame %.0f ms old   deg = misregistration" % age, f_small)
                sheet.save(os.path.join(tmp, "f_%04d.png" % k))
            encode(tmp, os.path.join(a.out, "compare.gif"), fps, a.grid_colors)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
