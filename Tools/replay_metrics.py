"""Per-frame and temporal metrics for replayed trajectories (Tools/replay_run.ps1 output).

Every method was replayed with the same trajectory and the same fixed time step, so frame i of every method and of
GT shows the same instant (same head pose, robot pose, shown robot frame). Per method, against GT:

  image error     PSNR (dB) and LPIPS (AlexNet, on 640x360, every 3rd frame) of each frame against GT's frame.
  misregistration how far the image sits from where the world is, deg: median length of the optical flow GT_i -> M_i
                  over textured GT pixels the method covers (holes and pixels outside the robot frame excluded).
  slip            how fast the image slides against the world, deg/s: median length of
                  (flow M_{i-1} -> M_i) - (flow GT_{i-1} -> GT_i). A world-locked image has 0; a head-locked one slips
                  at the head's angular speed; judder (frames that repeat, then jump) also shows up here.
  flicker         temporal inconsistency (warping error, Lai et al. 2018): M_{i-1} warped to frame i with GT's flow
                  vs M_i, mean |RGB difference| (0-255) on pixels where GT itself warps consistently. Holes that open
                  and close, popping keyframes and judder raise it; smooth, world-locked content does not.

Flow: OpenCV DIS (medium preset) on the full-resolution grey images. Angles use the desktop view's pinhole
(horizontal FOV 90 deg). A repeat GT run (GT2) gives the floor of every metric (the renderer is not bit-exact
from run to run: GPU lighting noise, mean |diff| < 1/255).

Usage: python replay_metrics.py <run_dir> [--methods GT2 M0 M1 ...] [--no-lpips]
Writes <run_dir>/metrics_frames.csv and metrics.json, prints a summary table.
"""

import argparse
import csv
import json
import os
import sys
from concurrent.futures import ProcessPoolExecutor

import cv2
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from replay_trajectory import SEGMENTS  # noqa: E402

FOV_DEG = 90.0
DEFAULT_METHODS = ["GT2", "M0", "M1", "M2", "M3", "M4f0", "M4f1", "M4f2"]
TEXTURE_GRAD = 12.0   # grey-level gradient (Sobel, after a 3 px blur) for "textured": flow is only reliable there
DARK = 8              # max channel below this = hole / outside the robot frame (the methods draw those black)
WARP_CONSISTENT = 12  # GT warps to within this (max channel) = not occluded, flow valid


def frame_count(d):
    return len([f for f in os.listdir(d) if f.startswith("eye_") and f.endswith(".png")])


def load(d, i):
    return cv2.cvtColor(cv2.imread(os.path.join(d, "eye_%05d.png" % i), cv2.IMREAD_COLOR), cv2.COLOR_BGR2RGB)


def grey(img):
    return cv2.cvtColor(img, cv2.COLOR_RGB2GRAY)


def textured(g):
    b = cv2.GaussianBlur(g, (0, 0), 1.0).astype(np.float32)
    return np.hypot(cv2.Sobel(b, cv2.CV_32F, 1, 0, ksize=3), cv2.Sobel(b, cv2.CV_32F, 0, 1, ksize=3)) / 4.0 > TEXTURE_GRAD


def warp(img, flow):
    h, w = flow.shape[:2]
    gx, gy = np.meshgrid(np.arange(w, dtype=np.float32), np.arange(h, dtype=np.float32))
    return cv2.remap(img, gx + flow[..., 0], gy + flow[..., 1], cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)


def psnr(a, b):
    mse = np.mean((a.astype(np.float32) - b.astype(np.float32)) ** 2)
    return 99.0 if mse <= 1e-9 else 10 * np.log10(255.0 ** 2 / mse)


def method_metrics(args):
    """Per-frame metrics of one method (runs in a worker process)."""
    run, name, hz = args
    gt_dir, m_dir = os.path.join(run, "GT"), os.path.join(run, name)
    n = min(frame_count(gt_dir), frame_count(m_dir))
    dis = cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_MEDIUM)
    w = cv2.imread(os.path.join(gt_dir, "eye_00000.png")).shape[1]
    deg_per_px = np.degrees(1.0 / ((w / 2) / np.tan(np.radians(FOV_DEG / 2))))
    rows = []
    prev_g = prev_m = None
    for i in range(n):
        g, m = load(gt_dir, i), load(m_dir, i)
        gg, mg = grey(g), grey(m)
        tex = textured(gg)
        covered = m.max(axis=2) >= DARK
        r = {"frame": i, "psnr": psnr(m, g)}
        # Misregistration: where GT's pixel went in the method's image.
        f = dis.calc(gg, mg, None)
        sel = tex & covered
        r["reg_deg"] = float(np.median(np.hypot(f[..., 0], f[..., 1])[sel]) * deg_per_px) if sel.sum() > 500 else np.nan
        r["covered"] = float(covered.mean())
        if prev_g is not None:
            pgg, pmg = grey(prev_g), grey(prev_m)
            fg = dis.calc(pgg, gg, None)
            fm = dis.calc(pmg, mg, None)
            sel = textured(pgg) & (prev_m.max(axis=2) >= DARK)
            d = np.hypot(fm[..., 0] - fg[..., 0], fm[..., 1] - fg[..., 1])
            r["slip_dps"] = float(np.median(d[sel]) * deg_per_px * hz) if sel.sum() > 500 else np.nan
            # Flicker: previous method frame carried along GT's motion vs this method frame.
            back = dis.calc(gg, pgg, None)
            consistent = np.abs(g.astype(np.int16) - warp(prev_g, back).astype(np.int16)).max(axis=2) < WARP_CONSISTENT
            diff = np.abs(m.astype(np.float32) - warp(prev_m, back).astype(np.float32)).mean(axis=2)
            r["flicker"] = float(diff[consistent].mean()) if consistent.sum() > 500 else np.nan
        else:
            r["slip_dps"] = r["flicker"] = np.nan
        rows.append(r)
        prev_g, prev_m = g, m
    return name, rows


def lpips_scores(run, names, every=3):
    import torch
    import lpips
    torch.set_num_threads(max(1, os.cpu_count() - 2))
    net = lpips.LPIPS(net="alex", verbose=False)
    n = frame_count(os.path.join(run, "GT"))

    def tensor(img):
        img = cv2.resize(img, (640, 360), interpolation=cv2.INTER_AREA)
        return torch.from_numpy(img).permute(2, 0, 1)[None].float() / 127.5 - 1.0

    out = {name: {} for name in names}
    with torch.no_grad():
        for i in range(0, n, every):
            g = tensor(load(os.path.join(run, "GT"), i))
            for name in names:
                out[name][i] = float(net(tensor(load(os.path.join(run, name), i)), g))
    return out


def summarize(vals):
    v = np.array([x for x in vals if x is not None and not np.isnan(x)])
    if v.size == 0:
        return {"mean": None, "p95": None, "p5": None}
    return {"mean": float(v.mean()), "p95": float(np.percentile(v, 95)), "p5": float(np.percentile(v, 5))}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run")
    ap.add_argument("--methods", nargs="*", default=DEFAULT_METHODS)
    ap.add_argument("--no-lpips", action="store_true")
    ap.add_argument("--hz", type=float, default=72.0)
    ap.add_argument("--start", type=float, default=1.0, help="time of frame 0 (replay_run -From)")
    a = ap.parse_args()
    names = [m for m in a.methods if os.path.isdir(os.path.join(a.run, m))]

    with ProcessPoolExecutor(max_workers=min(len(names), max(1, os.cpu_count() // 2))) as ex:
        results = dict(ex.map(method_metrics, [(a.run, nm, a.hz) for nm in names]))
    lp = {} if a.no_lpips else lpips_scores(a.run, names)

    fields = ["frame", "t", "method", "psnr", "lpips", "reg_deg", "slip_dps", "flicker", "covered"]
    summary = {"methods": names, "hz": a.hz, "fov_deg": FOV_DEG, "segments": SEGMENTS, "overall": {}, "by_segment": {}}
    with open(os.path.join(a.run, "metrics_frames.csv"), "w", newline="") as f:
        wr = csv.DictWriter(f, fieldnames=fields)
        wr.writeheader()
        for nm in names:
            rows = results[nm]
            for r in rows:
                r["t"] = a.start + r["frame"] / a.hz
                r["method"] = nm
                r["lpips"] = lp.get(nm, {}).get(r["frame"])
                wr.writerow({k: ("" if r.get(k) is None or (isinstance(r.get(k), float) and np.isnan(r[k])) else
                                 (round(r[k], 5) if isinstance(r[k], float) else r[k])) for k in fields})
            summary["overall"][nm] = {k: summarize([r.get(k) for r in rows]) for k in ["psnr", "lpips", "reg_deg", "slip_dps", "flicker"]}
            summary["by_segment"][nm] = {
                seg: {k: summarize([r.get(k) for r in rows if t0 <= r["t"] < t1])["mean"] for k in ["lpips", "reg_deg", "slip_dps", "flicker"]}
                for seg, t0, t1 in SEGMENTS}
    with open(os.path.join(a.run, "metrics.json"), "w") as f:
        json.dump(summary, f, indent=1)

    o = summary["overall"]
    print("| Method | PSNR dB (mean / worst 5%) | LPIPS (mean / worst 5%) | Misregistration deg (mean / p95) | Slip deg/s (mean / p95) | Flicker (mean) |")
    print("|---|---|---|---|---|---|")
    fmt = lambda x, p: "-" if x is None else ("%." + str(p) + "f") % x
    for nm in names:
        s = o[nm]
        print("| %s | %s / %s | %s / %s | %s / %s | %s / %s | %s |" % (
            nm, fmt(s["psnr"]["mean"], 1), fmt(s["psnr"]["p5"], 1), fmt(s["lpips"]["mean"], 3), fmt(s["lpips"]["p95"], 3),
            fmt(s["reg_deg"]["mean"], 2), fmt(s["reg_deg"]["p95"], 2), fmt(s["slip_dps"]["mean"], 1), fmt(s["slip_dps"]["p95"], 1),
            fmt(s["flicker"]["mean"], 2)))


if __name__ == "__main__":
    main()
