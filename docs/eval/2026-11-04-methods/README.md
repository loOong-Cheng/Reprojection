# Presentation methods compared: 12 views, 11 presentations, 2 depth models

Date: 2026-11-04. Simulator: this repository (UE 5.7, VR Template map as the remote world). Every presentation method
is compared with ground truth (GT: the remote world seen from the operator's eye now) on the same frozen state per
scene. There are 9 image metrics and a VR frame-rate test.

## Result in short

- **M4 (depth mesh + a fill for its holes) is best on every image metric.** Mean LPIPS with the default ZEDlike depth:
  M4 0.145-0.184 depending on fill and sphere, M3 0.264, M2 0.256, M1 0.325, M0 0.434.
- **For M4's sphere, the adaptive radius (M2's) beats the fixed 1 m in 7 of 11 scenes** (2 worse, 2 even). With
  ZEDlike depth the sphere fills every pixel that has no stereo depth, up to 35% of the frame in far views. Far
  courtyard (S06): 32.3 dB with the adaptive sphere, 23.2 dB with 1 m. With perfect depth the two spheres are even.
- **Memory (fill 2) fills disocclusions best** (error 25 vs 40 for the sphere fill, lower in 10 of 11 scenes). It also
  has the best PSNR, SSIM and false-edge rate. Its LPIPS is level with the sphere fill (0.146 vs 0.145).
- **Memory also pastes stale content:** old robot-hand fragments in S04, old fan blades at zero offset (S12, the only
  presentation there that is worse than head-locked M0). With a full memory it costs about 7 ms of extra GPU time in VR
  (57 fps vs 69). It is not ready to be the default.
- **Nearest background (fill 1)** lowers the disocclusion error (35 vs 40), but its LPIPS is no better than the sphere's
  in 9 of 11 scenes: it replaces ghost copies with streaks.
- **M2 beats M1 in 9 of 11 scenes.** In far views it cuts the misregistration from 54 to 5 px (S06).
- **No single metric ranks these methods.** PSNR puts M3 2.3 dB above M2; LPIPS rates them about equal. M3 places the
  image exactly but leaves its holes dark, and the per-region errors show that.

## What was compared

| Key | Presentation |
|---|---|
| GT | Reference: the remote world rendered from the eye now (not available to a real system) |
| M0 | Newest frame shown head-locked, no compensation |
| M1 | Sphere reprojection, fixed R = 1 m |
| M2 | Sphere reprojection, adaptive R (median of the centre depth over 2 s, eased over 150 ms) |
| M3 | Depth mesh from the degraded depth; holes left dark |
| M4 fill 0 | M3 + the sphere behind it fills the holes (HANDOFF design, default) |
| M4 fill 1 | M3 + nearest background pixel (walk from the sphere's sample to the first clearly farther depth) |
| M4 fill 2 | M3 + keyframe memory of earlier frames (edge band 64 px), fill 1 where memory has nothing |
| ..., R 1 m | The same M4 fills with M1's fixed sphere (`teleop.M4.Sphere 1`) instead of M2's (`2`, default) |

## Setup

- Remote world: VR Template map. The robot stands at the desk with props: checkerboard, 1 cm rods, a sliding block and a
  fan (both moving), plus the Template cubes, ball, boxes and walls.
- Robot: Waist3+Neck2 (default), stereo cameras 1280x960 at 60 Hz, 110 deg horizontal, baseline 64 mm, camera mount
  10 cm forward / 5 cm up from the neck pivot.
- Latency: WiFi preset (p50 ~54 ms, p95 ~90 ms). The frozen frame was 57-116 ms old.
- Depth: ZEDlike (default degradation: blocks, edge holes, noise, range) and DepthGT (perfect depth).
- View: desktop Play-In-Editor, left eye, 1280x723 px, 90 deg horizontal (1 px is about 0.09 deg). Each eye uses its
  own robot camera; a mono view uses the left one.

## Protocol

A frame on screen is old and the robot lags the head. Methods only differ in how they handle the resulting
eye-camera offset, so every method must see exactly the same frame, robot pose, world and eye. Per scene
(`Content/Python/teleop_eval.py`):

1. Set the view (yaw, pitch) and the robot hands. Select M4 fill 2 with the adaptive sphere and clear the memory. The
   head turns about a neck point 10 cm behind and 5 cm below the eye, as a real head does.
2. Sway for 3 s: +-6 cm sideways, +-2 cm up/down, +-6 deg yaw, +-2 deg pitch, and the hands +-4-8 cm in the desk
   scenes. The robot follows, while M2's radius and the memory build up as in use.
3. Return to the view and settle for 2.6 s (longer than M2's 2 s window).
4. Freeze (`teleop.Debug.Freeze 1`): the props, the robot and its cameras, the frame on screen, M2's radius and the
   memory stand still.
5. Move the eye by the scene's offset: what the robot has not caught up with (latency) or cannot do (the waist does not
   follow height).
6. For each presentation: switch, wait 1.6 s (temporal AA and 90 GPU samples), log the state (`teleop.Eval.State`) and
   take a screenshot without UI (`teleop.Eval.Shot`).
7. Render two class maps (`teleop.Debug.MaskView 1`) from which the regions below are computed: the mesh from exact
   depth (`teleop.Debug.MeshGTDepth 1`, ground truth, used only for the metric) and from the degraded depth.

The offsets span what latency and the robot's limits produce. A head moving at 0.3-1 m/s, or turning at 100-300 deg/s,
with 60-100 ms of frame age plus the robot's response, gives about 5-20 cm or 5-20 deg.

| Scene | View | In view | Eye vs camera (fwd, right, up cm) | Turn deg | Frame age ms | M2 R cm | Keyframes | V / D / N / O % |
|---|---|---|---|---|---|---|---|---|
| S01 | desk front, lean right 10 cm | checkerboard, fan (moving), ball, doorway, white box + rods, cubes | +0.0, +10.0, -0.0 | 0.00 | 66.5 | 86.9 | 9 | 80 / 10 / 10 / 0 |
| S02 | rods and boxes, lean left-up | 1 cm rods (high depth gradient), white boxes, cubes, far wall | -4.6, -6.9, +1.9 | 0.00 | 77.9 | 342.5 | 10 | 68 / 10 / 23 / 0 |
| S03 | fan and doorway, small lean + turn | rotating fan blades (moving, thin), sliding block (moving), checkerboard, dark doorway (far) | -1.4, +6.5, -0.1 | 4.00 | 73.2 | 458.9 | 12 | 72 / 6 / 22 / 0 |
| S04 | hands at the desk, lean in | robot hands (actuators, moving during the pre-motion), cubes, desk edge | +7.8, +0.0, +0.0 | 0.00 | 96.1 | 59.6 | 12 | 93 / 4 / 2 / 0 |
| S05 | cube stack and ball, big lean right | purple cube stack, ball, hands, desk edge, floor | -9.2, +10.6, -5.3 | 0.00 | 70.1 | 82.5 | 13 | 89 / 6 / 5 / 0 |
| S06 | far courtyard corner, lean right 20 cm | far walls (4-10 m), blue cubes on the floor, sky | +10.0, -17.3, +0.0 | 0.00 | 115.7 | 488.2 | 15 | 65 / 0 / 35 / 0 |
| S07 | head turn left 15 deg | side wall, cube stack edge, desk corner | -3.1, -2.7, -0.6 | 15.00 | 69.4 | 488.2 | 11 | 80 / 0 / 12 / 8 |
| S08 | stand up 15 cm (waist cannot follow) | desk top: rods, white box, cubes, hands | -7.1, +0.0, +12.1 | 5.00 | 67.0 | 79.7 | 14 | 77 / 18 / 5 / 0 |
| S09 | lean back 20 cm | whole desk: checkerboard, fan, ball, hands, rods | -18.1, +0.0, -8.4 | 0.00 | 97.0 | 89.6 | 10 | 88 / 6 / 6 / 0 |
| S10 | fast diagonal nod | white boxes, cubes, desk, wall corner | +4.4, +3.6, -0.9 | 14.41 | 63.4 | 310.0 | 13 | 91 / 4 / 5 / 0 |
| S11 | near floor and desk side | floor (near, slanted), desk side, wall | +9.7, +5.0, +2.6 | 0.00 | 66.7 | 157.2 | 15 | 100 / 0 / 0 / 0 |
| S12 | control: eye at the camera | desk front looking level (the camera is exactly at the eye only at pitch 0; pitched, the neck pivot puts it ~3 cm off): all methods should match GT | +0.0, +0.0, +0.0 | 0.00 | 69.9 | 179.3 | 14 | 86 / 5 / 9 / 0 |

Eye vs camera: the left eye in the left camera's frame of the frozen frame. Turn: the angle between the eye and that
camera. Regions are shares of the frame (ZEDlike run, see Metrics).

## Metrics

The previous score was the mean luma error on pixels where M3 differed from GT by more than 40, and it was not
suitable:
- it depended on M3 itself;
- it skipped holes over dark backgrounds (where M3's dark hole differs by less than 40);
- it ignored colour and everything outside the holes;
- it could not compare M1/M2 or detect misregistration.

The metrics used here:

| Metric | What it measures | Why |
|---|---|---|
| PSNR (dB, higher is better) | Whole-frame pixel fidelity, 10 log10(255^2 / MSE) | Standard. Squared error lets a few large errors dominate; a small shift of a high-contrast texture costs as much as a ghost |
| SSIM (higher is better) | Structural similarity of luma, Gaussian window sigma 1.5 | Standard; compares local structure and contrast and is less sensitive to brightness offsets |
| LPIPS (lower is better) | Learned perceptual distance, AlexNet features | Standard perceptual metric for novel view synthesis; tracks human judgements better than PSNR |
| Err V / D / N (0-255, lower is better) | Mean abs RGB difference per region | Regions from geometry, the same for every method (below) |
| Shift (px, lower is better) | Median misregistration in V: block matching of textured 24 px GT patches (coarse-to-fine, up to 64 px) | The geometric error felt as the image swimming against head motion; independent of colour and exposure |
| False edges (%, lower is better) | Method edge pixels (Sobel on luma) with no GT edge within 3 px, % of the GT edge count | Counts ghost copies and hole borders (the "overlapping edges"); misregistered edges count too |
| GPU (ms) | Desktop: median GPU frame time of 90 frames, frozen (no robot captures then), mono, editor | Cost of the method itself |
| VR fps, VR GPU | Standalone game on the Meta XR Simulator, 72 Hz cap, both robot cameras capturing | Full pipeline in stereo |

Regions, from the two class maps:
- **V**: seen by the camera with depth. Tests registration.
- **D**: a hole even with exact depth. That is the disocclusions plus the thin band the mesh cuts along every depth
  edge (about 5% of the frame at zero offset). Tests hole filling.
- **N**: a hole only because the degraded depth is missing there (the camera saw it). Tests filling under real depth.
- **O**: outside the frame on screen. No method has data there; it is only reported as a share.

None of these measure anything over time: flicker, swimming during motion, judder, or the step when a new frame
arrives. That needs a video metric (for example temporal warping error) or the blind test in the headset.

## Results

Means are over S01-S11. S12 is the zero-offset control: there M0, M1, M2 and M4 fill 0/1 all give the same image
(26.0 dB), and what remains is the moving props in a 70 ms old frame plus the mesh's edge band.

**ZEDlike depth (default), mean over S01-S11**

| Method | PSNR dB ↑ | SSIM ↑ | LPIPS ↓ | Err V ↓ | Err D ↓ | Err N ↓ | Shift px ↓ | False edges % ↓ | GPU ms ↓ | VR fps | VR GPU ms |
|---|---|---|---|---|---|---|---|---|---|---|---|
| M0 | 15.0 | 0.661 | 0.434 | 26.4 | 53.1 | 23.3 | 44.9 | 60.5 | 1.45 | 69.1 | 3.94 |
| M1 (R 1 m) | 16.6 | 0.696 | 0.325 | 16.6 | 37.1 | 26.5 | 34.1 | 46.1 | 1.45 | 69.1 | 3.99 |
| M2 (adaptive R) | 18.0 | 0.744 | 0.256 | 15.6 | 40.5 | 22.0 | 25.1 | 31.3 | 1.45 | 70.0 | 4.03 |
| M3 | 20.3 | 0.857 | 0.264 | 2.7 | 73.8 | 50.4 | 0.6 | 19.0 | 1.82 | 69.5 | 6.78 |
| M4 fill 0 | 23.4 | 0.893 | 0.145 | 2.5 | 39.7 | 21.5 | 0.5 | 10.1 | 1.88 | 69.1 | 5.77 |
| M4 fill 1 | 23.7 | 0.890 | 0.153 | 2.5 | 34.5 | 22.8 | 0.5 | 11.6 | 1.88 | 69.0 | 5.96 |
| M4 fill 2 | 24.6 | 0.901 | 0.146 | 2.6 | 25.2 | 17.6 | 0.6 | 9.1 | 3.47 | 57.1 | 12.68 |
| M4 fill 0, R 1 m | 22.3 | 0.879 | 0.174 | 2.5 | 36.5 | 25.9 | 0.5 | 15.3 | 1.84 | 69.0 | 5.58 |
| M4 fill 1, R 1 m | 22.4 | 0.876 | 0.184 | 2.5 | 34.6 | 26.2 | 0.5 | 17.1 | 1.89 | 68.6 | 5.92 |
| M4 fill 2, R 1 m | 23.3 | 0.888 | 0.173 | 2.7 | 25.1 | 21.2 | 0.6 | 13.9 | 3.38 | 55.9 | 10.50 |

**Perfect depth (DepthGT), mean over S01-S11**

| Method | PSNR dB ↑ | SSIM ↑ | LPIPS ↓ | Err V ↓ | Err D ↓ | Err N ↓ | Shift px ↓ | False edges % ↓ | GPU ms ↓ |
|---|---|---|---|---|---|---|---|---|---|
| M0 | 15.0 | 0.662 | 0.433 | 25.5 | 52.3 | 55.1 | 42.5 | 60.6 | 1.45 |
| M1 (R 1 m) | 16.6 | 0.696 | 0.326 | 17.6 | 37.1 | 38.7 | 34.8 | 46.4 | 1.46 |
| M2 (adaptive R) | 18.0 | 0.746 | 0.256 | 15.5 | 40.9 | 34.9 | 23.0 | 31.0 | 1.46 |
| M3 | 22.3 | 0.911 | 0.128 | 2.9 | 70.8 | 62.5 | 0.3 | 8.9 | 1.85 |
| M4 fill 0 | 23.2 | 0.918 | 0.109 | 2.8 | 40.0 | 34.3 | 0.2 | 7.2 | 1.87 |
| M4 fill 1 | 23.7 | 0.918 | 0.111 | 2.8 | 32.6 | 35.0 | 0.2 | 6.7 | 1.95 |
| M4 fill 2 | 25.5 | 0.931 | 0.100 | 2.7 | 22.9 | 26.5 | 0.1 | 4.2 | 6.16 |
| M4 fill 0, R 1 m | 23.5 | 0.918 | 0.107 | 2.8 | 36.0 | 37.5 | 0.2 | 7.7 | 1.87 |
| M4 fill 1, R 1 m | 23.7 | 0.917 | 0.111 | 2.8 | 34.0 | 37.6 | 0.2 | 7.3 | 1.90 |
| M4 fill 2, R 1 m | 25.6 | 0.931 | 0.100 | 2.7 | 23.8 | 26.7 | 0.1 | 4.4 | 6.70 |

Per scene (ZEDlike, best in bold):

**LPIPS (lower is better) per scene, ZEDlike**

| Scene | M0 | M1 (R 1 m) | M2 (adaptive R) | M3 | M4 fill 0 | M4 fill 1 | M4 fill 2 | M4 fill 0, R 1 m | M4 fill 1, R 1 m | M4 fill 2, R 1 m |
|---|---|---|---|---|---|---|---|---|---|---|
| S01 | 0.442 | 0.402 | 0.387 | 0.276 | 0.208 | 0.225 | 0.230 | **0.208** | 0.224 | 0.229 |
| S02 | 0.391 | 0.376 | 0.328 | 0.391 | 0.230 | 0.227 | **0.225** | 0.269 | 0.284 | 0.269 |
| S03 | 0.503 | 0.314 | 0.199 | 0.341 | 0.139 | 0.134 | **0.132** | 0.205 | 0.214 | 0.195 |
| S04 | 0.455 | 0.206 | 0.252 | 0.207 | **0.112** | 0.134 | 0.141 | 0.117 | 0.129 | 0.135 |
| S05 | 0.408 | 0.361 | 0.353 | 0.185 | 0.152 | 0.154 | 0.143 | 0.149 | 0.149 | **0.138** |
| S06 | 0.249 | 0.363 | **0.095** | 0.356 | 0.097 | 0.097 | 0.097 | 0.219 | 0.219 | 0.216 |
| S07 | 0.424 | 0.292 | 0.121 | 0.255 | 0.110 | 0.114 | **0.103** | 0.193 | 0.194 | 0.176 |
| S08 | 0.503 | 0.403 | 0.391 | 0.315 | **0.222** | 0.252 | 0.227 | 0.222 | 0.251 | 0.226 |
| S09 | 0.537 | 0.389 | 0.348 | 0.259 | 0.172 | 0.184 | **0.164** | 0.178 | 0.185 | 0.166 |
| S10 | 0.565 | 0.201 | 0.234 | 0.220 | 0.112 | 0.116 | **0.098** | 0.107 | 0.118 | 0.104 |
| S11 | 0.295 | 0.264 | 0.101 | 0.099 | **0.044** | 0.052 | 0.051 | 0.049 | 0.052 | 0.051 |
| S12 | 0.067 | 0.066 | 0.066 | 0.244 | **0.065** | 0.065 | 0.104 | 0.065 | 0.066 | 0.103 |

**PSNR dB (higher is better) per scene, ZEDlike**

| Scene | M0 | M1 (R 1 m) | M2 (adaptive R) | M3 | M4 fill 0 | M4 fill 1 | M4 fill 2 | M4 fill 0, R 1 m | M4 fill 1, R 1 m | M4 fill 2, R 1 m |
|---|---|---|---|---|---|---|---|---|---|---|
| S01 | 12.6 | 13.4 | 13.7 | 16.9 | 18.1 | 18.0 | 18.6 | 18.2 | 18.3 | **18.9** |
| S02 | 12.6 | 13.7 | 13.6 | 14.6 | 18.2 | 19.6 | **19.9** | 16.9 | 17.0 | 17.5 |
| S03 | 12.2 | 16.4 | 15.7 | 17.1 | 21.8 | 23.2 | **24.4** | 19.5 | 19.5 | 20.5 |
| S04 | 13.4 | 17.5 | 16.6 | 19.2 | **24.0** | 23.4 | 23.0 | 23.2 | 23.1 | 23.0 |
| S05 | 13.3 | 14.0 | 13.1 | 20.3 | 19.8 | 20.1 | 21.9 | 20.2 | 20.5 | **22.2** |
| S06 | 24.2 | 19.5 | 28.7 | 18.6 | 32.3 | 32.3 | **32.3** | 23.2 | 23.2 | 23.2 |
| S07 | 17.3 | 20.5 | 22.7 | **30.7** | 23.8 | 23.8 | 25.2 | 23.6 | 23.6 | 25.0 |
| S08 | 12.3 | 13.0 | 13.3 | 14.3 | 17.3 | 17.5 | 18.5 | 18.1 | 18.5 | **19.4** |
| S09 | 11.9 | 14.5 | 15.2 | 18.6 | 19.9 | 20.5 | 21.2 | 19.7 | 20.5 | **21.3** |
| S10 | 14.1 | 18.9 | 18.1 | 18.8 | 25.0 | 25.3 | **28.4** | 25.6 | 25.3 | 28.1 |
| S11 | 20.7 | 21.1 | 27.0 | 34.1 | 37.3 | 37.1 | 37.6 | 37.2 | 37.4 | **37.7** |
| S12 | 26.0 | 26.0 | 26.0 | 18.6 | 26.0 | 26.0 | 24.6 | **26.0** | 26.0 | 24.6 |

**Err D (lower is better) per scene, ZEDlike**

| Scene | M0 | M1 (R 1 m) | M2 (adaptive R) | M3 | M4 fill 0 | M4 fill 1 | M4 fill 2 | M4 fill 0, R 1 m | M4 fill 1, R 1 m | M4 fill 2, R 1 m |
|---|---|---|---|---|---|---|---|---|---|---|
| S01 | 60.6 | 44.1 | 42.1 | 77.6 | 41.8 | 41.1 | **26.7** | 43.7 | 41.1 | 27.5 |
| S02 | 67.6 | 43.9 | 54.4 | 85.1 | 53.3 | 35.7 | **29.0** | 43.2 | 40.4 | 30.1 |
| S03 | 60.2 | 32.7 | 46.6 | 78.7 | 45.0 | 27.5 | **19.2** | 32.0 | 33.3 | 21.9 |
| S04 | 57.1 | 40.0 | 31.6 | 84.4 | 30.7 | 34.5 | **29.5** | 39.2 | 39.9 | 30.4 |
| S05 | 73.5 | 49.8 | 50.6 | 57.1 | 50.4 | 47.8 | 23.3 | 49.3 | 45.6 | **22.7** |
| S06 | **10.9** | 27.5 | 12.4 | 68.0 | 12.4 | 12.4 | 12.4 | 27.5 | 27.5 | 27.5 |
| S07 | 49.5 | 22.3 | 29.9 | 28.9 | 26.6 | 24.5 | 20.9 | 20.7 | 22.3 | **20.1** |
| S08 | 53.6 | 43.4 | 50.7 | 91.7 | 50.1 | 48.5 | 39.4 | 42.8 | 40.3 | **33.4** |
| S09 | 68.0 | 53.0 | 49.0 | 77.1 | 48.5 | 36.5 | 26.7 | 52.3 | 38.0 | **26.5** |
| S10 | 56.6 | 29.1 | 36.9 | 85.4 | 36.1 | 32.8 | **14.2** | 28.8 | 29.5 | 14.8 |
| S11 | 25.9 | 22.5 | 41.4 | 77.3 | 41.2 | 38.4 | 36.0 | 22.3 | 23.3 | **20.8** |
| S12 | 12.5 | **12.5** | 12.5 | 81.8 | 12.5 | 12.6 | 20.0 | 12.5 | 12.5 | 20.0 |

In S06, S07 and S11 the D region is under 1% of the frame, so their Err D values are noisy.

**VR frame rate (Meta XR Simulator, 72 Hz cap)**

| Configuration | fps median (min-max) | GPU ms median |
|---|---|---|
| GT | 68.6 (68.0-70.0) | 4.83 |
| M0 | 69.1 (67.6-71.0) | 3.94 |
| M1 (R 1 m) | 69.1 (67.7-70.1) | 3.99 |
| M2 (adaptive R) | 70.0 (69.0-71.0) | 4.03 |
| M3 | 69.5 (68.6-70.1) | 6.78 |
| M4 fill 0 | 69.1 (66.5-69.5) | 5.77 |
| M4 fill 1 | 69.0 (68.5-69.6) | 5.96 |
| M4 fill 2 (full memory, new keyframe every 0.1 s) | 57.1 (52.7-58.6) | 12.68 |
| M4 fill 0, R 1 m | 69.0 (67.5-70.1) | 5.58 |
| M4 fill 1, R 1 m | 68.6 (68.1-69.1) | 5.92 |
| M4 fill 2, R 1 m (full memory, new keyframe every 0.1 s) | 55.9 (53.5-58.6) | 10.50 |
| M4 fill 2, head still (1 keyframe) | 68.0 (65.6-69.1) | 7.81 |

Every method except fill 2 runs at 68-70 fps, the same as GT and M0. That was this PC's ceiling with Meta Horizon Link
running in the background. M3 and M4 add 1.6-2.8 ms of GPU time over M0. Fill 2 was forced to its worst case: 16
keyframes and a new one every 0.1 s, as while moving. It drops to 56-57 fps at 10.5-12.7 ms, over the 13.9 ms budget
of 72 Hz once the rest of the frame is added. With a still head (one keyframe) it holds 68 fps at 7.8 ms. With perfect
depth (full-resolution keyframe meshes) its desktop cost is 6.2 ms against 3.5 ms with ZEDlike.

## Screenshots

One sheet per scene (ZEDlike): GT, the regions (V grey, D orange, N purple, O dark) and all 10 presentations, each
labelled with LPIPS, PSNR and Err D. The full-resolution PNGs of both depth models are in
`Reprojection/Saved/TeleopEval/2026-11-04/` on the test PC, which is not in git.

**S01, desk front, lean right 10 cm.** In view: checkerboard, fan (moving), ball, doorway, white box + rods, cubes.

![S01](img/S01.jpg)

**S02, rods and boxes, lean left-up.** In view: 1 cm rods (high depth gradient), white boxes, cubes, far wall.

![S02](img/S02.jpg)

**S03, fan and doorway, small lean + turn.** In view: rotating fan blades (moving, thin), sliding block (moving), checkerboard, dark doorway (far).

![S03](img/S03.jpg)

**S04, hands at the desk, lean in.** In view: robot hands (actuators, moving during the pre-motion), cubes, desk edge.

![S04](img/S04.jpg)

**S05, cube stack and ball, big lean right.** In view: purple cube stack, ball, hands, desk edge, floor.

![S05](img/S05.jpg)

**S06, far courtyard corner, lean right 20 cm.** In view: far walls (4-10 m), blue cubes on the floor, sky.

![S06](img/S06.jpg)

**S07, head turn left 15 deg.** In view: side wall, cube stack edge, desk corner.

![S07](img/S07.jpg)

**S08, stand up 15 cm (waist cannot follow).** In view: desk top: rods, white box, cubes, hands.

![S08](img/S08.jpg)

**S09, lean back 20 cm.** In view: whole desk: checkerboard, fan, ball, hands, rods.

![S09](img/S09.jpg)

**S10, fast diagonal nod.** In view: white boxes, cubes, desk, wall corner.

![S10](img/S10.jpg)

**S11, near floor and desk side.** In view: floor (near, slanted), desk side, wall.

![S11](img/S11.jpg)

**S12, control: eye at the camera.** In view: desk front looking level (the camera is exactly at the eye only at pitch 0; pitched, the neck pivot puts it ~3 cm off): all methods should match GT.

![S12](img/S12.jpg)

## Findings in detail

- **Registration vs holes.** M3 and M4 put what they show within 0.5-0.6 px of GT (median). M0-M2 are 25-45 px off,
  because a sphere is only right at its radius. But M3 leaves 74 (D) and 50 (N) of error in its holes, which is why it
  is no better than M2 in LPIPS. M4 keeps M3's registration and fills the holes.
- **Sphere radius.** Inside M4 the sphere fills the N region: every pixel without stereo depth, such as the dark
  doorway, the far walls or the sky. With ZEDlike depth that is up to 35% of the frame, so the radius matters as much as
  the fill. The adaptive radius wins clearly in the far views (S02, S03, S06, S07) and slightly in three more. R = 1 m wins
  slightly in S05 and S10, desk views with most content at about 1 m (in S10 the window median was 310 cm). As a presentation of its own, M2 beats M1 in 9 of
  11 scenes. M1 wins in S04 and S10, where the window median (60 cm and 310 cm) fits the rest of the view worse than
  1 m does.
- **Memory.** Fill 2 has the lowest disocclusion error in 10 of 11 scenes, because a keyframe taken while swaying
  usually saw the background that is now revealed. Its weaknesses:
  - Stale content of anything that moved. The robot hands in S04 are the clearest case: LPIPS 0.141 against 0.112 for
    the sphere fill. Moving props cause the same.
  - Showing stale content where nothing is missing: at zero offset (S12) it is 1.4 dB worse than M0.
  - Its cost.

  Masking the robot hands and the moving props out of the keyframes, and limiting how many keyframe meshes are drawn,
  are the next steps.
- **Nearest background.** Fill 1 removes the ghost copies of the sphere fill but paints streaks of the background colour
  across the hole. LPIPS rates the two failures about equally.
- **Perfect depth.** With DepthGT, M3 alone reaches LPIPS 0.128: its holes are then only the disocclusions. Memory
  leads more clearly (PSNR 25.5 vs 23.2, Err D 23 vs 40), and the sphere radius hardly matters.

## Problems found in the test setup (fixed before these runs)

- **Desktop eye offset.** The desktop view rendered from the head centre, but a mono view uses the left robot frame. With
  the 64 mm baseline, every earlier desktop test had the eye 3.2 cm to the right of its camera. The desktop view now
  renders from the left eye (`teleop.Debug.DesktopEye`, default 1). With that fixed, the control scene gives identical
  images for M0, M1, M2 and M4, and 0 px shift.
- **Head rotation about the eye.** The desktop head pose turned about the eye, so the implied neck moved, and the robot
  leaned its waist and rolled its cameras by 3-14 deg to follow. A real head turns about the neck; `teleop_eval._head`
  now does that, and the robot then matches the head exactly at rest (0.00 cm, 0 deg).

## Limits

- **One eye, mono.** Binocular effects, such as the two eyes seeing different holes, are not measured.
- **One frozen instant per scene.** The moving props appear where they were when the frame was captured, so every method
  except GT shows them displaced by the same amount.
- **Memory comes from one sway.** It was built from the same 3 s, +-6 cm sway in every scene. More past motion gives it
  more to work with; offsets beyond the sway show where it has nothing.
- **Approximate frame border.** The class maps use the sphere's radius to decide what is inside the frame, so the O/V
  border is approximate by a few pixels.
- **Desktop GPU times** include the editor and are mono. **VR frame rates** were limited by the PC's state that day
  (68-70 fps ceiling).

## Reproduce

1. Open the project in the editor and start Play-In-Editor on the VR Template map, desktop with no HMD. Then run in the
   editor's Python:
   ```python
   import importlib, teleop_eval; importlib.reload(teleop_eval)
   teleop_eval.run(r"D:/out/zedlike")   # teleop.DepthPreset 1 first; ~36 s per scene, logs "EVAL| done"
   ```
2. Copy `Reprojection/Saved/Logs/Reprojection.log` (it has the `[eval]` lines), then compute the metrics and the page
   outside the editor:
   ```
   python Tools/eval_metrics.py D:/out/zedlike --log <copied log>      # numpy, scipy, pillow; torch + lpips for LPIPS
   python Tools/eval_report.py --out D:/out/report --run zedlike=D:/out/zedlike --run depthgt=D:/out/depthgt --vrfps vr_fps.csv
   ```
3. VR frame rate: for each configuration, start a standalone game (`UnrealEditor.exe <uproject> /Game/VRTemplate/VRTemplateMap
   -game -vr`) with `XR_RUNTIME_JSON` pointing at the Meta XR Simulator. Use
   `-ExecCmds="teleop.Debug.QuitAfterSec 42, teleop.Observer 0, teleop.Method ..."` and take the median of the `[fps]`
   lines after 16 s. Stop `synth_env_server` first and the simulator afterwards.

Data in this folder: `data/metrics_zedlike.csv`, `data/metrics_depthgt.csv` (one row per scene and method),
`data/scenes.json` (scene definitions), `data/vr_fps.csv`.
