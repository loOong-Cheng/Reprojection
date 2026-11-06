"""Per-frame and temporal metrics for replayed trajectories (Tools/replay_run.ps1 output).

Every method was replayed with the same trajectory and the same fixed time step, so frame i of every method and of
GT shows the same instant (same head pose, robot pose, shown robot frame). Per method, against GT:

  image error     PSNR (dB) and LPIPS (AlexNet, on 640x360, every 3rd frame) of each frame against GT's frame.
  misregistration how far the image sits from where the world is, deg: median angle of the optical flow GT_i -> M_i
                  (between the view rays of a GT pixel and of where its content is in M_i) over textured GT pixels whose
                  content M_i shows: forward-backward consistent flow, so holes, disocclusions and pixels outside the
                  robot frame are excluded ("matched" = that fraction of the frame).
  slip            how fast the image slides against the world, deg/s: median length of
                  (flow M_{i-1} -> M_i) - (flow GT_{i-1} -> GT_i). A world-locked image has 0; a head-locked one slips
                  at the head's angular speed; judder (frames that repeat, then jump) also shows up here.
  flicker         temporal inconsistency (warping error, Lai et al. 2018): M_{i-1} warped to frame i with GT's flow
                  vs M_i, mean |RGB difference| (0-255) on pixels where GT itself warps consistently. Holes that open
                  and close, popping keyframes and judder raise it; smooth, world-locked content does not.

Flow: OpenCV DIS (medium preset) on the grey images, also started from an ORB + RANSAC homography so displacements of
tens of degrees are found (see Flow). Angles use the desktop view's pinhole (horizontal FOV 90 deg). A repeat GT run (GT2) gives the floor of every metric (the renderer is not bit-exact
from run to run: GPU lighting noise, mean |diff| < 1/255).

Usage: python replay_metrics.py <run_dir> [--methods GT2 M0 M1 ...] [--no-lpips] [--lpips-every 3] [--threads N]
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
from replay_trajectory import read_segments  # noqa: E402

FOV_DEG = 90.0
DEFAULT_METHODS = ["GT2", "M0", "M1", "M2", "M3", "M4f0", "M4f1", "M4f2"]
TEXTURE_GRAD = 12.0   # grey-level gradient (Sobel, after a 3 px blur) for "textured": flow is only reliable there
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


class Flow:
    """Optical flow that also finds large displacements, and the angle each displacement is.

    DIS alone is coarse-to-fine but loses track beyond ~50 px at 640 px width (~10 deg): head-locked M0 at a 20-40 deg gap
    measured ~5.6 deg. So every flow is also started from a homography fitted to ORB matches (a camera rotation is exactly a
    homography; parallax is left to DIS), and the flow (with or without that start) that warps better wins.
    """

    def __init__(self, w, h):
        self.dis = cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_MEDIUM)
        self.orb = cv2.ORB_create(2000)
        self.bf = cv2.BFMatcher(cv2.NORM_HAMMING, crossCheck=True)
        self.gx, self.gy = np.meshgrid(np.arange(w, dtype=np.float32), np.arange(h, dtype=np.float32))
        f = (w / 2) / np.tan(np.radians(FOV_DEG / 2))
        self.f, self.cx, self.cy = f, w / 2, h / 2
        r = np.dstack([(self.gx + 0.5 - self.cx) / f, (self.gy + 0.5 - self.cy) / f, np.ones_like(self.gx)])
        self.rays = r / np.linalg.norm(r, axis=2, keepdims=True)

    def features(self, g):
        return self.orb.detectAndCompute(g, None)

    def homography_flow(self, fa, fb):
        (ka, da), (kb, db) = fa, fb
        if da is None or db is None or len(ka) < 40 or len(kb) < 40:
            return None
        m = self.bf.match(da, db)
        if len(m) < 40:
            return None
        pa = np.float32([ka[x.queryIdx].pt for x in m])
        pb = np.float32([kb[x.trainIdx].pt for x in m])
        H, inl = cv2.findHomography(pa, pb, cv2.RANSAC, 3.0)
        if H is None or inl is None or inl.sum() < 30:
            return None
        p = cv2.perspectiveTransform(np.dstack([self.gx, self.gy]).reshape(-1, 1, 2), H).reshape(self.gx.shape + (2,))
        fl = (p - np.dstack([self.gx, self.gy])).astype(np.float32)
        # Below ~8 px plain DIS is reliable: skip the second flow.
        return fl if np.all(np.isfinite(fl)) and np.median(np.hypot(fl[..., 0], fl[..., 1])) > 8.0 else None

    def calc(self, a, b, fa=None, fb=None):
        """Flow a -> b (grey images): pixel x of a is at x + flow in b. fa / fb: features() of a / b, to reuse."""
        plain = self.dis.calc(a, b, None)
        init = self.homography_flow(fa or self.features(a), fb or self.features(b))
        if init is None:
            return plain
        # Residual flow against b pulled back by the homography, composed with it: x -> x + r -> x + r + init(x + r).
        r = self.dis.calc(a, warp(b, init), None)
        started = r + warp(init, r)
        err = lambda fl: np.abs(a.astype(np.float32) - warp(b, fl).astype(np.float32)).mean()
        return started if err(started) < err(plain) else plain

    def angle_deg(self, flow):
        """Angle between the view rays of x and x + flow (exact, not px x deg-per-px)."""
        x, y = self.gx + flow[..., 0], self.gy + flow[..., 1]
        r = np.dstack([(x + 0.5 - self.cx) / self.f, (y + 0.5 - self.cy) / self.f, np.ones_like(x)])
        r /= np.linalg.norm(r, axis=2, keepdims=True)
        return np.degrees(np.arccos(np.clip((r * self.rays).sum(axis=2), -1.0, 1.0)))

    def consistent(self, fw, bw, tol=1.0):
        """Forward-backward check: x -> x + fw -> back by bw lands within tol px (+5% of the motion) of x.
        Fails where a's content is not in b: holes, disocclusions, outside the robot frame."""
        back = warp(bw, fw)
        e = np.hypot(fw[..., 0] + back[..., 0], fw[..., 1] + back[..., 1])
        inside = (self.gx + fw[..., 0] >= 0) & (self.gx + fw[..., 0] <= self.gx.shape[1] - 1) & \
                 (self.gy + fw[..., 1] >= 0) & (self.gy + fw[..., 1] <= self.gx.shape[0] - 1)
        return inside & (e < tol + 0.05 * np.hypot(fw[..., 0], fw[..., 1]))


def psnr(a, b):
    mse = np.mean((a.astype(np.float32) - b.astype(np.float32)) ** 2)
    return 99.0 if mse <= 1e-9 else 10 * np.log10(255.0 ** 2 / mse)


MIN_MATCHED = 0.05  # misregistration needs at least this share of GT's textured pixels found in the method's image


def chunk_metrics(args):
    """Per-frame metrics of every method for frames [i0, i1) (one worker process). GT's own flows are computed once."""
    run, names, hz, i0, i1 = args
    gt_dir = os.path.join(run, "GT")
    h, w = cv2.imread(os.path.join(gt_dir, "eye_00000.png")).shape[:2]
    fl = Flow(w, h)
    deg_per_px = np.degrees(1.0 / fl.f)
    out = {nm: [] for nm in names}
    prev_g, prev_m = None, {}
    for i in range(max(0, i0 - 1), i1):
        g = load(gt_dir, i)
        gg = grey(g)
        cur_g = (g, gg, fl.features(gg), textured(gg))
        score = i >= i0
        if score and prev_g is not None:
            pg, pgg, pfeat_g, ptex_g = prev_g
            fg = fl.calc(pgg, gg, pfeat_g, cur_g[2])
            back = fl.calc(gg, pgg, cur_g[2], pfeat_g)
            consistent = np.abs(g.astype(np.int16) - warp(pg, back).astype(np.int16)).max(axis=2) < WARP_CONSISTENT
        for nm in names:
            m = load(os.path.join(run, nm), i)
            mg = grey(m)
            cur_m = (m, mg, fl.features(mg), textured(mg))
            if score:
                r = {"frame": i, "psnr": psnr(m, g)}
                # Misregistration: where GT's content is in the method's image, over GT content the method shows
                # (forward-backward consistent: not in a hole, a disocclusion or outside the robot frame).
                fw = fl.calc(gg, mg, cur_g[2], cur_m[2])
                ok = fl.consistent(fw, fl.calc(mg, gg, cur_m[2], cur_g[2]))
                sel = cur_g[3] & ok
                r["matched"] = float(sel.sum() / max(1, cur_g[3].sum()))
                r["reg_deg"] = float(np.median(fl.angle_deg(fw)[sel])) if r["matched"] >= MIN_MATCHED and sel.sum() > 500 else np.nan
                if prev_g is not None:
                    pm, pmg, pfeat_m, ptex_m = prev_m[nm]
                    fm = fl.calc(pmg, mg, pfeat_m, cur_m[2])
                    sel = ptex_g & ptex_m
                    d = np.hypot(fm[..., 0] - fg[..., 0], fm[..., 1] - fg[..., 1])
                    r["slip_dps"] = float(np.median(d[sel]) * deg_per_px * hz) if sel.sum() > 500 else np.nan
                    # Flicker: previous method frame carried along GT's motion vs this method frame.
                    diff = np.abs(m.astype(np.float32) - warp(pm, back).astype(np.float32)).mean(axis=2)
                    r["flicker"] = float(diff[consistent].mean()) if consistent.sum() > 500 else np.nan
                else:
                    r["slip_dps"] = r["flicker"] = np.nan
                out[nm].append(r)
            prev_m[nm] = cur_m
        prev_g = cur_g
    return out


def lpips_scores(run, names, every=3, threads=None):
    import torch
    import lpips
    torch.set_num_threads(threads or max(1, os.cpu_count() - 2))
    net = lpips.LPIPS(net="alex", verbose=False)
    n = frame_count(os.path.join(run, "GT"))

    def tensor(img):
        if img.shape[1] != 640:
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
    ap.add_argument("--lpips-every", type=int, default=3)
    ap.add_argument("--threads", type=int, default=None, help="LPIPS torch threads (default: all but 2)")
    ap.add_argument("--workers", type=int, default=None, help="flow worker processes (default: one per method, at most half the CPUs)")
    a = ap.parse_args()
    SEGMENTS = read_segments(os.path.join(a.run, "trajectory.csv"))
    names = [m for m in a.methods if os.path.isdir(os.path.join(a.run, m))]

    n = min(frame_count(os.path.join(a.run, nm)) for nm in names + ["GT"])
    workers = a.workers or max(1, os.cpu_count() // 2)
    edges = np.linspace(0, n, 2 * workers + 1).astype(int)
    results = {nm: [] for nm in names}
    with ProcessPoolExecutor(max_workers=workers) as ex:
        for part in ex.map(chunk_metrics, [(a.run, names, a.hz, int(e0), int(e1)) for e0, e1 in zip(edges[:-1], edges[1:]) if e1 > e0]):
            for nm in names:
                results[nm] += part[nm]
    lp = {} if a.no_lpips else lpips_scores(a.run, names, a.lpips_every, a.threads)

    fields = ["frame", "t", "method", "psnr", "lpips", "reg_deg", "slip_dps", "flicker", "matched"]
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
            summary["overall"][nm] = {k: summarize([r.get(k) for r in rows]) for k in ["psnr", "lpips", "reg_deg", "slip_dps", "flicker", "matched"]}
            # Frames where too little of GT's view is in the method's image to measure misregistration (gap beyond the FOV).
            summary["overall"][nm]["unmatched_share"] = float(np.mean([r["matched"] < MIN_MATCHED for r in rows]))
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
