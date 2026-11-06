# Method comparison in motion: one trajectory, every method (2026-11-05)

The frozen-scene evaluation ([2026-11-04](../2026-11-04-methods/README.md)) compares methods at single instants.
That flatters the baseline: a frozen M0 frame is a complete, sharp image, just from the wrong place and time. Here
every method replays **the same operator motion**, frame for frame, and is measured over time.

![All methods side by side](../../img/replay/compare.gif)

## Setup

**Trajectory** ([`Tools/replay_trajectory.py`](../../../Tools/replay_trajectory.py), 10 s, frames from t = 1 s).
A seated operator at the Template desk:

| Time (s) | Segment | Motion |
|---|---|---|
| 1.0–1.5 | still | idle sway only (a few mm, < 0.5°) |
| 1.5–2.4 | turn right | head yaw 0 → 45°, pitch −15 → −25° (peak 96°/s) |
| 2.4–3.9 | reach, grab | right hand to the top cube of the purple stack (58 cm), grip at 3.7 s; leans in 4 cm |
| 3.9–5.0 | carry, look back | hand to the front of the desk, head back to yaw 5° (peak 70°/s) |
| 5.0–6.1 | drop, hand back | release at 5.3 s (the cube falls onto the desk), hand to rest |
| 6.1–7.6 | lean right | head 15 cm right with 3° roll, back |
| 7.6–8.9 | quick look left | yaw 0 → −30° → 0 in 0.4 s each (peak 144°/s) |
| 8.9–10.0 | hold still | |

Every move is a minimum-jerk segment, the usual model of human point-to-point movement. The head turns about a neck
point 10 cm behind and 5 cm below the eye, as in the frozen evaluation.

**Replay** (`FTeleopReplay`, [`Tools/replay_run.ps1`](../../../Tools/replay_run.ps1)). Each method runs in its own
standalone process: `-game -nohmd`, desktop, 1280×720, left eye, 90° horizontal FOV. The engine uses a fixed time step
(`-UseFixedTimeStep -FPS=72`), and all simulation time comes from `TeleopClock`, which counts frames. So every run sees
the same robot motion, the same camera captures and the same latency draws, and frame *i* of every method shows the same
instant. M2's depth readback waits for the GPU in this mode, so its radius updates on the same frame in every run.
Other settings:
- WiFi latency: the shown frame is 56–125 ms old (p50 56, p95 97).
- ZEDlike depth.
- The Waist3Neck2 robot.
- M4 on the adaptive sphere.
- No HUD.

Per frame the replay writes the eye view, the observer view (every third frame) and the state: shown frame, age,
eye–camera offset, sphere radius and keyframes.

**Determinism.** Two runs of M4 fill 2 have identical state logs. Their pixels differ only by GPU lighting noise in the
robot camera images: mean |diff| ≤ 0.75/255, 99th percentile 8/255. All metrics of the second run match the first to
the precision shown below. A second GT run (GT2) is the floor of every metric.

## Metrics

Each method's frame *i* is compared with GT's frame *i*.

| Metric | Definition | What it catches |
|---|---|---|
| **PSNR**, **LPIPS** | per frame against GT. LPIPS uses AlexNet on 640×360, every third frame. | overall image error, holes, ghosts |
| **Misregistration** (°) | median angle between where content is in GT<sub>i</sub> and where it is in M<sub>i</sub> (optical flow GT<sub>i</sub> → M<sub>i</sub>), over textured GT pixels whose content M<sub>i</sub> shows | the world shown in the wrong place |
| **Slip** (°/s) | median of \|flow(M<sub>i−1</sub>→M<sub>i</sub>) − flow(GT<sub>i−1</sub>→GT<sub>i</sub>)\| × 72 Hz | the image sliding against the world: swimming, head-locking, judder |
| **Flicker** (0–255) | warping error: M<sub>i−1</sub> warped to frame *i* with GT's flow vs M<sub>i</sub>, mean \|RGB\|, on pixels where GT itself warps consistently | holes opening and closing, popping content, judder |

**How misregistration is measured.**
- The flow is OpenCV DIS. It is also started from a homography fitted to ORB feature matches, which is exact for a camera
  rotation. Whichever flow warps better is kept, so gaps of tens of degrees are found.
- Each displacement is converted to the exact angle between the two view rays.
- Only content the method shows is scored: the flow must be forward-backward consistent within 1 px. Holes,
  disocclusions and areas outside the robot frame drop out; LPIPS, PSNR and flicker see them.
- Checks:
  - Synthetic camera rotations of 1–40° measure within 2%.
  - For M0, it tracks the logged eye–camera rotation (correlation 0.89; at t = 2.0 s, 10.1° measured vs 10.2°
    rotated).
  - A 1–4 px consistency tolerance changes M0–M2 by under 10%; M3 and M4 stay sub-pixel.
- **Correction (2026-11-05):** the first version used DIS alone and pixel distance × the angle of a centre pixel. It
  read about 25% high at small angles and lost track above about 15° (a 20° rotation measured 3.5°). It also counted
  flow guessed inside holes, because the holes show the dark operator room, not black. The matrix tests exposed this.
  All numbers here are from the corrected metric; LPIPS, PSNR and flicker did not change.

### Whole trajectory

| Method | PSNR dB ↑ (mean / worst 5%) | LPIPS ↓ (mean / worst 5%) | Misregistration ° ↓ (mean / p95) | Slip °/s ↓ (mean / p95) | Flicker ↓ |
|---|---|---|---|---|---|
| GT2 (repeat run, floor) | 97.0 / 79.9 | 0.000 / 0.000 | 0.00 / 0.00 | 0.0 / 0.0 | 0.46 |
| **M0** head-locked (baseline) | 13.3 / 10.7 | 0.367 / 0.611 | 3.24 / 9.59 | 23.4 / 103.1 | 5.16 |
| M1 sphere, R 1 m | 17.2 / 13.8 | 0.206 / 0.339 | 1.07 / 2.50 | 2.8 / 7.9 | 1.79 |
| M2 sphere, adaptive R | 17.6 / 14.3 | 0.211 / 0.346 | 1.07 / 2.54 | 2.4 / 5.7 | 1.77 |
| M3 depth mesh | 15.8 / 15.0 | 0.315 / 0.372 | **0.03** / **0.04** | 2.1 / 4.6 | 2.38 |
| M4 fill 0 (sphere) | **20.0** / 17.5 | **0.161** / **0.219** | 0.04 / 0.05 | 1.9 / 4.0 | **1.63** |
| M4 fill 1 (nearest background) | 19.4 / 17.6 | 0.197 / 0.243 | 0.04 / 0.05 | 2.1 / 4.6 | 2.31 |
| M4 fill 2 (keyframe memory) | 19.9 / **18.2** | 0.185 / 0.221 | 0.04 / 0.05 | **1.7** / **3.5** | 1.75 |

![Misregistration over time](../../img/replay/misregistration.png)

*Misregistration per frame. The first panel shows the operator's head turn speed. In the other panels the method is
blue, with M0 in grey behind it for comparison.*

### By segment

Misregistration (°):

| Method | still | turn right | reach, grab | carry, look back | drop | lean right | quick look left | hold |
|---|---|---|---|---|---|---|---|---|
| M0 | 1.12 | 5.42 | 1.26 | 4.28 | 1.71 | 4.60 | 5.35 | 1.34 |
| M1 | 0.90 | 0.74 | 0.90 | 0.77 | 0.97 | 1.93 | 1.01 | 0.91 |
| M2 | 0.83 | 0.64 | 1.03 | 0.71 | 0.95 | 1.96 | 1.03 | 0.89 |
| M3 | 0.03 | 0.04 | 0.03 | 0.03 | 0.04 | 0.03 | 0.04 | 0.03 |
| M4 fill 0 | 0.04 | 0.04 | 0.04 | 0.04 | 0.04 | 0.04 | 0.04 | 0.04 |
| M4 fill 1 | 0.04 | 0.04 | 0.04 | 0.04 | 0.04 | 0.04 | 0.04 | 0.04 |
| M4 fill 2 | 0.04 | 0.04 | 0.04 | 0.04 | 0.04 | 0.04 | 0.05 | 0.04 |

M3 and M4 sit at the measurement floor (0.04° is about 0.6 px at 1280 px): the content they show is in place. Their
differences are in what fills the holes, which the image metrics and flicker measure.

Slip (°/s):

| Method | still | turn right | reach, grab | carry, look back | drop | lean right | quick look left | hold |
|---|---|---|---|---|---|---|---|---|
| M0 | 1.5 | 50.2 | 2.7 | 32.7 | 7.4 | 17.6 | 63.9 | 6.7 |
| M1 | 1.2 | 3.4 | 1.3 | 3.1 | 1.7 | 5.7 | 3.9 | 0.8 |
| M2 | 1.2 | 3.1 | 1.5 | 2.8 | 1.6 | 4.1 | 3.2 | 0.8 |
| M3 | 1.1 | 3.0 | 1.2 | 3.3 | 1.5 | 2.9 | 2.8 | 0.7 |
| M4 fill 0 | 1.0 | 2.6 | 1.1 | 2.7 | 1.3 | 2.7 | 2.4 | 0.7 |
| M4 fill 1 | 1.1 | 3.0 | 1.2 | 3.1 | 1.5 | 3.1 | 2.8 | 0.8 |
| M4 fill 2 | 1.1 | 2.5 | 1.0 | 2.6 | 1.3 | 2.4 | 2.1 | 0.7 |

Flicker (GT2 floor 0.3–0.6):

| Method | still | turn right | reach, grab | carry, look back | drop | lean right | quick look left | hold |
|---|---|---|---|---|---|---|---|---|
| M0 | 1.03 | 9.98 | 1.18 | 7.12 | 3.23 | 5.12 | 11.04 | 1.63 |
| M1 | 0.89 | 1.88 | 0.86 | 2.00 | 1.85 | 2.92 | 2.59 | 0.70 |
| M2 | 0.87 | 1.89 | 0.85 | 1.97 | 1.81 | 2.82 | 2.58 | 0.69 |
| M3 | 1.55 | 2.55 | 2.14 | 3.45 | 2.44 | 2.35 | 2.92 | 1.23 |
| M4 fill 0 | 1.07 | 1.53 | 1.01 | 1.81 | 1.87 | 2.05 | 2.41 | 0.88 |
| M4 fill 1 | 1.65 | 2.29 | 1.44 | 2.54 | 2.58 | 2.99 | 3.21 | 1.33 |
| M4 fill 2 | 1.41 | 1.80 | 1.13 | 1.92 | 2.16 | 2.02 | 2.26 | 1.13 |

LPIPS:

| Method | still | turn right | reach, grab | carry, look back | drop | lean right | quick look left | hold |
|---|---|---|---|---|---|---|---|---|
| M0 | 0.212 | 0.452 | 0.257 | 0.457 | 0.309 | 0.502 | 0.431 | 0.230 |
| M1 | 0.167 | 0.184 | 0.188 | 0.212 | 0.201 | 0.293 | 0.196 | 0.165 |
| M2 | 0.173 | 0.188 | 0.192 | 0.219 | 0.205 | 0.296 | 0.204 | 0.167 |
| M3 | 0.275 | 0.322 | 0.352 | 0.357 | 0.301 | 0.310 | 0.305 | 0.271 |
| M4 fill 0 | 0.129 | 0.151 | 0.157 | 0.182 | 0.160 | 0.190 | 0.165 | 0.125 |
| M4 fill 1 | 0.175 | 0.190 | 0.185 | 0.216 | 0.203 | 0.224 | 0.199 | 0.166 |
| M4 fill 2 | 0.159 | 0.174 | 0.177 | 0.205 | 0.195 | 0.199 | 0.185 | 0.165 |

## Findings

- **The baseline fails in motion, not at rest.**
  - While the head is still, M0 is about 1.1° off, close to the spheres (0.8–0.9°).
  - In the turns it averages 4.3–5.4° off, with a peak of 16.6°. It slips at 33–64°/s on average, over 100°/s at the
    peaks.
  - It flickers 10× more than at rest, because each new frame jumps.
  - A still screenshot shows none of this.
- **Rotation compensation is the largest single gain.**
  - Against M0, the spheres (M1, M2) cut misregistration 3×, slip 8–10× and flicker 3×.
  - What they leave is parallax:
    - Even at rest, the robot camera sits 2.7 cm from the eye, because the neck pivot moves it when the head is
      pitched down.
    - During the lean their error doubles, to about 1.9° and 4–6°/s.
  - M2's radius settled at 82–89 cm, because the desk fills the centre of the view. So on this trajectory M1 and M2 are
    nearly the same.
- **Depth fixes the registration.**
  - What M3 and M4 show is in place, at the measurement floor (0.03–0.04°, under 1 px), 25× better than the spheres.
  - But M3's holes are large: the sky and the room behind the doorway are beyond stereo range, plus the disocclusions.
    They make M3 the second worst on LPIPS, and its flicker is higher than the spheres'.
- **Among the M4 fills, the motion metrics and the image metrics disagree.**
  - Fill 0 (sphere) has the best LPIPS and PSNR and the least flicker.
  - All three fills place content equally well (0.04°). Fill 2 (memory) slips least, at 1.7°/s against 1.9°/s. But it
    flickers more than fill 0 at rest (1.4 vs 1.1), and it shows stale fragments of the spinning fan and the robot
    hand. So its LPIPS is clearly worse than fill 0's (0.185 vs 0.161).
  - Fill 1 streaks, and its flicker is the highest of the three.
  - In the frozen evaluation, fill 2 led PSNR and tied fill 0 on LPIPS. In motion, fill 0 leads.
- **No single number ranks the methods.**
  - Misregistration and slip rank M3 above the spheres; LPIPS and PSNR rank it below them.
  - The temporal metrics separate the baseline from everything else far more sharply than the per-frame ones do:
    M0 against M1 is 8× on slip, but only 1.8× on LPIPS.

## Limits

- One trajectory, one scene, one latency preset (WiFi). The motion is scripted minimum-jerk motion, not recorded human
  motion. To record real operator motion on the Quest and replay it here, use `teleop.Replay.Record <file>` and
  `teleop.Replay.Stop`.
- Desktop, mono, left eye only: no binocular effects, and no headset compositor (which adds its own timewarp).
- The flow-based metrics use median statistics over textured pixels. They say nothing about untextured areas.
  Misregistration only scores content the method shows, so it cannot see holes, ghosts or stretched content; flicker,
  LPIPS and PSNR do.
- The renderer is not bit-exact between runs. The noise is far below the method differences.
- The depth is ground truth with modelled errors, not stereo matching. The errors don't depend on image content, and
  each frame carries its exact camera pose. So the sub-pixel registration of M3 and M4 holds for an exact pose and is
  optimistic for a real robot; see the [repo README's limitations](../../../README.md#limitations-and-next-steps).

## Reproduce

```powershell
python Tools\replay_trajectory.py traj.csv
.\Tools\replay_run.ps1 -Trajectory traj.csv                       # ~25 s per method; editor can stay closed
python Tools\replay_metrics.py Reprojection\Saved\TeleopReplay\<date> --methods GT2 M0 M1 M2 M3 M4f0 M4f1 M4f2
python Tools\replay_gifs.py   Reprojection\Saved\TeleopReplay\<date> docs\img\replay
python Tools\replay_plot.py   Reprojection\Saved\TeleopReplay\<date> docs\img\replay\misregistration.png
```

For the GT2 floor, add `"GT2|teleop.Method=5"` to `-Methods`. The metrics need numpy, opencv, torch + lpips and
matplotlib; the GIFs need ffmpeg. The per-frame values are in [`metrics_frames.csv`](metrics_frames.csv), and the
summaries in [`metrics.json`](metrics.json).
