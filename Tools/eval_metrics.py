"""Metrics for teleop_eval.py runs: every method's screenshot against GT, per scene.

Usage (Python 3.10+, numpy, scipy, pillow; lpips + torch optional):
    python eval_metrics.py <run_dir> [--log <editor log>] [--no-lpips]
Writes <run_dir>/metrics.csv and metrics.json.

Regions come from the two class maps (teleop.Debug.MaskView; green = depth mesh covers, red = hole inside the shown
frame, blue = outside it):
    O  outside the shown frame                       (blue)
    D  disocclusion: hole of the mesh with exact depth (red in maskGT): the camera never saw it from where it was
    N  hole only because the depth is degraded        (red in maskDeg, green in maskGT): seen, but no stereo depth
    V  visible with depth                             (green in both)
Metrics (vs GT, 8-bit sRGB screenshots):
    psnr      whole frame, dB (higher is better)
    ssim      whole frame, luma, Gaussian window sigma 1.5 (higher is better)
    lpips     whole frame, AlexNet (lower is better; perceptual, standard in novel view synthesis)
    mae_V/D/N mean |RGB difference| (0-255) in the region (lower is better); V = registration where data exists,
              D = disocclusion fill, N = missing-depth fill
    shift_px  median image misregistration in V (block matching on textured patches), px; shift_deg the same in degrees
    ghost     false edges: method edge pixels with no GT edge within 3 px, % of GT edge pixels (inside the frame)
    gpu_ms    median GPU frame time of the last 90 frames (desktop PIE, whole editor frame), fps from the frame time
"""

import argparse
import csv
import json
import math
import os
import re
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

H_FOV_DEG = 90.0  # desktop PIE operator camera


def load(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)


def luma(img):
    return img[..., 0] * 0.299 + img[..., 1] * 0.587 + img[..., 2] * 0.114


def classes(mask_img):
    """0 = green (mesh), 1 = red (hole in frame), 2 = blue (outside frame)."""
    r, g, b = mask_img[..., 0], mask_img[..., 1], mask_img[..., 2]
    return np.argmax(np.stack([g, r, b], axis=-1), axis=-1)


def psnr(a, b):
    mse = np.mean((a - b) ** 2)
    return 99.0 if mse <= 1e-9 else 10.0 * math.log10(255.0 ** 2 / mse)


def ssim(a, b):
    x, y = luma(a), luma(b)
    s = 1.5
    mx, my = ndimage.gaussian_filter(x, s), ndimage.gaussian_filter(y, s)
    sxx = ndimage.gaussian_filter(x * x, s) - mx * mx
    syy = ndimage.gaussian_filter(y * y, s) - my * my
    sxy = ndimage.gaussian_filter(x * y, s) - mx * my
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    m = ((2 * mx * my + c1) * (2 * sxy + c2)) / ((mx * mx + my * my + c1) * (sxx + syy + c2))
    return float(np.mean(m))


def mae(a, b, region):
    n = int(region.sum())
    if n < 50:
        return None
    return float(np.mean(np.abs(a - b)[region]))


def _down(a):
    h, w = a.shape[0] // 2 * 2, a.shape[1] // 2 * 2
    a = a[:h, :w]
    return 0.25 * (a[0::2, 0::2] + a[1::2, 0::2] + a[0::2, 1::2] + a[1::2, 1::2])


def _patch_ssd(L, G, y, x, half, sy, sx):
    h, w = G.shape
    yy, xx = y + sy, x + sx
    if y - half < 0 or x - half < 0 or y + half > h or x + half > w:
        return np.inf
    if yy - half < 0 or xx - half < 0 or yy + half > h or xx + half > w:
        return np.inf
    return float(np.mean((L[yy - half:yy + half, xx - half:xx + half] - G[y - half:y + half, x - half:x + half]) ** 2))


MAX_SHIFT_PX = 64


def misregistration(img, gt, region, patch=24, step=16):
    """Median shift (px) that best maps GT patches onto the method image, over textured GT patches fully inside region
    (24 px patches every 16 px). Coarse-to-fine: exhaustive search +-16 at 1/4 resolution (= +-64 px, 48 px window),
    refined +-2 at 1/2 and at full resolution. Patches whose coarse match sits on the search border count as 64 px."""
    L, G = luma(img), luma(gt)
    h, w = G.shape
    half = patch // 2
    inside = ndimage.binary_erosion(region, iterations=half + 1)
    ys, xs = np.mgrid[half:h - half:step, half:w - half:step]
    ys, xs = ys.ravel(), xs.ravel()
    keep = inside[ys, xs]
    ys, xs = ys[keep], xs[keep]
    gm = ndimage.uniform_filter(G, patch)
    gs = np.sqrt(np.maximum(ndimage.uniform_filter(G * G, patch) - gm * gm, 0))
    keep = gs[ys, xs] > 8.0
    ys, xs = ys[keep], xs[keep]
    if len(ys) < 5:
        return None, int(len(ys))
    L2, G2 = _down(L), _down(G)
    L4, G4 = _down(L2), _down(G2)
    sr = MAX_SHIFT_PX // 4
    y4, x4 = np.clip(ys // 4, 0, G4.shape[0] - 1), np.clip(xs // 4, 0, G4.shape[1] - 1)
    best = np.full(len(ys), np.inf)
    bdy = np.zeros(len(ys), int)
    bdx = np.zeros(len(ys), int)
    for dy in range(-sr, sr + 1):
        for dx in range(-sr, sr + 1):
            sh = np.roll(np.roll(L4, -dy, axis=0), -dx, axis=1)
            v = ndimage.uniform_filter((sh - G4) ** 2, 12)[y4, x4]
            better = v < best
            best[better] = v[better]
            bdy[better] = dy
            bdx[better] = dx
    out = []
    for i in range(len(ys)):
        if abs(bdy[i]) == sr or abs(bdx[i]) == sr:
            out.append(float(MAX_SHIFT_PX))
            continue
        # 1/2 resolution, 24 px window there (= 48 px), +-2 around 2x the coarse shift.
        cy, cx = 2 * bdy[i], 2 * bdx[i]
        y2, x2 = ys[i] // 2, xs[i] // 2
        bv, bs = np.inf, (cy, cx)
        for fy in range(-2, 3):
            for fx in range(-2, 3):
                v = _patch_ssd(L2, G2, y2, x2, 12, cy + fy, cx + fx)
                if v < bv:
                    bv, bs = v, (cy + fy, cx + fx)
        cy, cx = 2 * bs[0], 2 * bs[1]
        bv, bs = np.inf, (cy, cx)
        for fy in range(-2, 3):
            for fx in range(-2, 3):
                v = _patch_ssd(L, G, ys[i], xs[i], half, cy + fy, cx + fx)
                if v < bv:
                    bv, bs = v, (cy + fy, cx + fx)
        out.append(min(math.hypot(*bs), float(MAX_SHIFT_PX)))
    return float(np.median(out)), len(out)


def edges(img):
    l = ndimage.gaussian_filter(luma(img), 1.0)
    gx, gy = ndimage.sobel(l, 1), ndimage.sobel(l, 0)
    return np.hypot(gx, gy) > 48.0


def ghost_edges(img, gt, frame):
    e_m, e_g = edges(img) & frame, edges(gt) & frame
    near_g = ndimage.binary_dilation(e_g, iterations=3)
    n_g = int(e_g.sum())
    if n_g < 50:
        return None
    return 100.0 * float((e_m & ~near_g).sum()) / n_g


def parse_eval_log(log_path):
    """[eval] lines -> {tag: {key: value}} (last occurrence wins)."""
    states = {}
    if not log_path or not os.path.exists(log_path):
        return states
    rx = re.compile(r'\[eval\] tag=(\S+) (.*)$')
    with open(log_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = rx.search(line)
            if not m or " no state" in line:
                continue
            kv = {}
            for k, v in re.findall(r'(\w+)=("[^"]*"|\S+)', m.group(2)):
                kv[k] = v.strip('"')
            states[m.group(1)] = kv
    return states


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run_dir")
    ap.add_argument("--log", default=None)
    ap.add_argument("--no-lpips", action="store_true")
    args = ap.parse_args()

    meta = json.load(open(os.path.join(args.run_dir, "scenes.json")))
    methods = [m for m in meta["methods"] if m != "GT"]
    states = parse_eval_log(args.log)

    lp = None
    if not args.no_lpips:
        try:
            import torch
            import lpips
            lp_model = lpips.LPIPS(net="alex", verbose=False)

            def lp(a, b):
                ta = torch.from_numpy(a / 127.5 - 1.0).permute(2, 0, 1)[None].float()
                tb = torch.from_numpy(b / 127.5 - 1.0).permute(2, 0, 1)[None].float()
                with torch.no_grad():
                    return float(lp_model(ta, tb).item())
        except Exception as e:  # optional
            print("LPIPS unavailable:", e)

    rows, scene_info = [], []
    for sc in meta["scenes"]:
        sid = sc["id"]
        gt = load(os.path.join(args.run_dir, "%s_GT.png" % sid))
        h, w = gt.shape[:2]
        fpx = (w / 2) / math.tan(math.radians(H_FOV_DEG / 2))
        cg = classes(load(os.path.join(args.run_dir, "%s_maskGT.png" % sid)))
        cd = classes(load(os.path.join(args.run_dir, "%s_maskDeg.png" % sid)))
        O = cg == 2
        D = cg == 1
        N = (cd == 1) & (cg == 0)
        V = (cg == 0) & (cd == 0)
        frame = ndimage.binary_erosion(~O, iterations=6)
        st = states.get("%s/GT" % sid, {})
        info = {"scene": sid, "name": sc.get("name", sid), "V%": 100 * V.mean(), "D%": 100 * D.mean(), "N%": 100 * N.mean(),
                "O%": 100 * O.mean(), "offset_cm": st.get("offset_cm"), "offset_len_cm": st.get("offset_len_cm"),
                "rot_deg": st.get("rot_deg"), "age_ms": st.get("age_ms"), "R_adapt": st.get("R_adapt"),
                "keyframes": st.get("keyframes")}
        scene_info.append(info)
        for m in methods:
            p = os.path.join(args.run_dir, "%s_%s.png" % (sid, m))
            if not os.path.exists(p):
                continue
            img = load(p)
            shift, npatch = misregistration(img, gt, V)
            s = states.get("%s/%s" % (sid, m), {})
            gpu = float(s["gpu_ms"]) if "gpu_ms" in s else None
            frame_ms = float(s["frame_ms"]) if "frame_ms" in s else None
            r = {"scene": sid, "method": m, "psnr": psnr(img, gt), "ssim": ssim(img, gt), "lpips": lp(img, gt) if lp else None,
                 "mae_V": mae(img, gt, V), "mae_D": mae(img, gt, D), "mae_N": mae(img, gt, N),
                 "shift_px": shift, "shift_deg": None if shift is None else math.degrees(math.atan(shift / fpx)),
                 "patches": npatch, "ghost": ghost_edges(img, gt, frame), "gpu_ms": gpu,
                 "fps": None if not frame_ms else 1000.0 / frame_ms}
            rows.append(r)
            print("%-8s %-7s psnr %5.2f ssim %.3f lpips %s V %s D %s N %s shift %s ghost %s gpu %s" % (
                sid, m, r["psnr"], r["ssim"], "-" if r["lpips"] is None else "%.3f" % r["lpips"],
                *["-" if r[k] is None else "%.1f" % r[k] for k in ("mae_V", "mae_D", "mae_N", "shift_px", "ghost", "gpu_ms")]))
            sys.stdout.flush()

    with open(os.path.join(args.run_dir, "metrics.csv"), "w", newline="") as f:
        wr = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        wr.writeheader()
        wr.writerows(rows)
    json.dump({"rows": rows, "scenes": scene_info, "methods": methods}, open(os.path.join(args.run_dir, "metrics.json"), "w"), indent=1)
    print("wrote", len(rows), "rows")


if __name__ == "__main__":
    main()
