# More tests: motion × robot location × latency × method (2026-11-05)

The [main comparison](../2026-11-05-replay/README.md) replays one trajectory at one latency, with the robot at its
default place by the desk. This matrix repeats that replay for:
- 8 motions;
- 4 robot locations;
- 4 latency levels.

That makes 128 configurations, each run as GT plus the 7 presentations: 896 runs.

Four of the motions go past what the robot's joints can follow. Two of them drive the robot's base.

**In short:**
- **Depth keeps the world in place under every condition.** What M3 and M4 show is off by 0.01–0.21° in all 128
  configurations, at every latency.
  - M4 has the best LPIPS in all 128.
  - Its sphere fill leads near the desk and in the room; its keyframe memory leads at the far location.
- **The fixed 1 m sphere (M1) is worse than no compensation for translation when the scene is far.** This covers the
  driving base, the big lean and standing up: M1 loses to M0 in 12–14 of 16 configurations per motion. The adaptive
  sphere (M2) beats M0 in 62 of 64.
- **Latency hurts mostly through rotation.**
  - In the turn, M0 goes from 3° off at 28 ms to 7° at 281 ms.
  - In the fast turn at Bad latency, M0 shows the wrong place, with under 5% of the real view in it, in 9–16% of
    frames.
  - The gaps from the robot's joint limits don't depend on latency: standing up gives 15 cm of height, a head roll 14°
    of roll.
- **For M3 and M4, latency shows in the holes, not in the registration.** M4 + sphere's LPIPS, averaged per motion, rises from
  0.12–0.22 at Ideal to 0.14–0.25 at Bad.

## Setup

Every configuration is one **motion** at one **robot location** with one **latency level**. It is replayed once per
method, plus GT.

The replay is deterministic, as in the main comparison. Each method runs in its own standalone desktop process with a
fixed 72 Hz time step, so frame *i* of every method shows the same instant.

All configurations use:
- ZEDlike depth;
- the Waist3Neck2 robot;
- M4 on the adaptive sphere.

Frames are rendered at 1280×720 and scored at 640×360.

### Motions

The robot is a 3-DoF waist under a 2-DoF neck. Four motions are chosen to go past what it can follow:
- the neck turns at most 300°/s and 2000°/s², and has no roll;
- the waist leans at most 30° pitch and 20° roll, which is about 19 cm sideways and 27 cm forward;
- the waist doesn't follow height at all.

| Motion | Length | What the operator does | Robot limit it hits |
|---|---|---|---|
| `turn` | 5.0 s | yaw 40° left, 80° right, back (peak 125°/s) | none: within the neck's limits |
| `fastturn` | 5.0 s | snap 90° right and back (peak 375°/s, 2560°/s²), then a fast 35° nod | neck speed and acceleration |
| `biglean` | 6.5 s | lean 40 cm right, back, lean in 30 cm | waist roll (about 19 cm) and pitch (about 27 cm) |
| `standup` | 5.0 s | rise 25 cm looking down at the desk, sit back | no height: a 25 cm gap while standing |
| `tilt` | 5.5 s | roll the head 25° right, then left | no neck roll: a 25° gap |
| `strafe` | 6.0 s | sit still while the robot base drives 80 cm right and back (peak 75 cm/s) | none; the base moves the cameras |
| `drive` | 7.0 s | sit still while the base backs up 100 cm and returns | none |
| `desk` | 9.0 s | the main comparison's task: grab and carry a cube, lean, quick look | none |

**The driving base.** The operator commands the base, and the robot executes the command after the command latency,
as the neck does. GT and the presentation use the base pose as commanded now. So a driving robot is, to the
presentation, one more source of camera motion between capture and display.

### Robot locations

![The four robot locations: the eye view and two third-person views](img/stations.jpg)

| Location | Station (x, y cm; yaw) | Depth in view |
|---|---|---|
| `desk` | 60, 0; 0° (default) | near clutter, 0.5–5 m |
| `side` | 150, −170; 90° | beside the desk, looking along it: layered, 0.5–4 m |
| `room` | −250, 0; 0° | 3 m back, the desk 3.5 m away: mid to far |
| `far` | −60, 0; 180° | facing away from the desk into the open courtyard: dark walls 4–10 m, sky |

The props (checkerboard, rods, fan, sliding block) and the Template's cubes stay by the desk at every location.

### Latency levels

These are `teleop.LatencyPreset` values, end-to-end p50 / p95 as measured. They include jitter and loss
(Gilbert-Elliott bursts, AR(1) jitter).

| Level | p50 | p95 |
|---|---|---|
| **Ideal** | 28 ms | 30 ms |
| **WiFi** | 54 ms | 89 ms |
| **Remote** | 94 ms | 190 ms |
| **Bad** | 281 ms | 365 ms |

## Metrics

The metrics are the main comparison's; see [its definitions](../2026-11-05-replay/README.md#metrics). Each value
below is the mean over the frames of one configuration, then over the configurations named in the table.

- **Misregistration (deg):** how far the content the method shows is from where it is in GT's frame.
- **Slip (deg/s):** how fast the shown world moves against the real one.
- **Flicker:** frame-to-frame change beyond GT's own.
- **LPIPS and PSNR:** image similarity to GT.
- **View lost:** frames where under 5% of GT's textured view is found in the method's image. A method loses the
  view in two ways:
  - it shows a complete image in the wrong place (M0);
  - the eye has turned past the edge of the old frame, which leaves black (the others).

  Misregistration is averaged over the other frames only. Where M0 often loses the view, its mean understates it.
- **Gap:** distance and angle between the eye now and the camera of the shown frame. This is what each method has to
  make up.

The misregistration metric was corrected while this matrix ran. The first version lost track of gaps above about 15°
and scored flow that was guessed inside holes. The matrix was rerun with the corrected metric, and the main
comparison's numbers were updated.

## Results

All tables, including slip, flicker and PSNR and every configuration, are in [`tables.md`](tables.md). The data is in
[`matrix.csv`](matrix.csv) (one row per configuration and method) and [`gaps.csv`](gaps.csv).

### What each motion does to the gap

Mean / max over the motion, mean over locations.

| Motion | Ideal | WiFi | Remote | Bad | Ideal | WiFi | Remote | Bad |
|---|---|---|---|---|---|---|---|---|
| | **cm** | | | | **deg** | | | |
| turn | 2.7 / 3.3 | 2.8 / 4.0 | 2.9 / 5.0 | 3.3 / 6.9 | 2.9 / 11.1 | 3.7 / 13.4 | 5.0 / 19.6 | 7.2 / 30.2 |
| fastturn | 2.9 / 7.1 | 3.1 / 7.9 | 3.5 / 10.4 | 4.3 / 16.2 | 5.1 / 34.3 | 6.3 / 39.3 | 8.5 / 52.9 | 13.8 / 89.4 |
| biglean | 9.5 / 20.3 | 9.8 / 20.3 | 10.5 / 22.6 | 11.4 / 25.4 | 5.6 / 18.6 | 5.6 / 18.8 | 5.8 / 19.3 | 6.1 / 19.7 |
| standup | 15.0 / 27.6 | 15.0 / 27.6 | 15.0 / 27.6 | 15.0 / 27.6 | 1.0 / 3.0 | 1.2 / 4.6 | 1.5 / 7.2 | 2.1 / 10.0 |
| tilt | 3.0 / 4.1 | 3.0 / 4.1 | 3.0 / 4.2 | 3.0 / 4.2 | 14.3 / 25.9 | 14.3 / 25.9 | 14.3 / 26.0 | 14.4 / 26.0 |
| strafe | 3.2 / 5.7 | 3.7 / 8.7 | 4.8 / 15.8 | 6.5 / 25.2 | 0.5 / 1.0 | 0.5 / 1.0 | 0.5 / 1.0 | 0.5 / 1.0 |
| drive | 3.2 / 5.6 | 3.7 / 8.5 | 4.7 / 14.3 | 6.8 / 24.5 | 0.5 / 1.0 | 0.5 / 1.0 | 0.5 / 1.0 | 0.5 / 1.0 |
| desk | 3.1 / 8.3 | 3.2 / 8.8 | 3.4 / 9.7 | 3.9 / 13.9 | 3.0 / 12.6 | 3.4 / 15.9 | 4.3 / 24.9 | 5.4 / 30.4 |

Some 2.5–3 cm of every gap is there at rest: the camera's offset from the eye.

- **Latency gaps** grow with latency: turns in angle, the driving base in distance.
- **Joint-limit gaps** stay the same at every latency: standup's height, tilt's roll, most of biglean.
- **fastturn** has both: the neck's speed limit adds to the latency, up to 89° at Bad.

### Misregistration

![Misregistration against latency, one panel per motion, log scale](misregistration_latency.png)

By motion (mean over locations and latencies):

| Method | turn | fastturn | biglean | standup | tilt | strafe | drive | desk |
|---|---|---|---|---|---|---|---|---|
| **M0** head-locked | 4.42 | 5.18 | 3.79 | 4.07 | 6.15 | 0.88 | 0.62 | 3.33 |
| M1 sphere 1 m | 1.11 | 1.29 | 3.56 | 4.89 | 1.19 | 1.79 | 1.33 | 1.28 |
| M2 sphere adaptive | 0.29 | 0.48 | 1.46 | 1.83 | 0.36 | 0.73 | 0.39 | 0.42 |
| M3 mesh | 0.03 | 0.04 | 0.03 | 0.05 | 0.03 | 0.04 | 0.04 | 0.03 |
| M4 + sphere | 0.05 | 0.07 | 0.08 | 0.07 | 0.08 | 0.09 | 0.08 | 0.06 |
| M4 + nearest bg | 0.06 | 0.07 | 0.08 | 0.07 | 0.08 | 0.09 | 0.08 | 0.06 |
| M4 + memory | 0.05 | 0.06 | 0.07 | 0.07 | 0.07 | 0.08 | 0.07 | 0.05 |

By robot location (mean over motions and latencies):

| Method | desk | side | room | far |
|---|---|---|---|---|
| **M0** head-locked | 4.67 | 3.48 | 3.26 | 2.80 |
| M1 sphere 1 m | 1.50 | 2.13 | 2.34 | 2.24 |
| M2 sphere adaptive | 1.47 | 0.83 | 0.42 | 0.27 |
| M3 mesh | 0.04 | 0.04 | 0.04 | 0.03 |
| M4 + sphere | 0.04 | 0.10 | 0.13 | 0.02 |
| M4 + nearest bg | 0.04 | 0.10 | 0.13 | 0.03 |
| M4 + memory | 0.04 | 0.08 | 0.11 | 0.03 |

By latency (mean over motions and locations):

| Method | Ideal (28 ms) | WiFi (54 ms) | Remote (94 ms) | Bad (281 ms) |
|---|---|---|---|---|
| **M0** head-locked | 3.08 | 3.35 | 3.70 | 4.08 |
| M1 sphere 1 m | 1.89 | 1.95 | 2.08 | 2.30 |
| M2 sphere adaptive | 0.68 | 0.70 | 0.76 | 0.84 |
| M3 mesh | 0.04 | 0.04 | 0.04 | 0.04 |
| M4 + sphere | 0.07 | 0.07 | 0.07 | 0.08 |
| M4 + nearest bg | 0.07 | 0.07 | 0.08 | 0.08 |
| M4 + memory | 0.06 | 0.07 | 0.07 | 0.07 |

### Image quality (LPIPS)

![LPIPS against latency, one panel per motion](lpips_latency.png)

By motion (mean over locations and latencies):

| Method | turn | fastturn | biglean | standup | tilt | strafe | drive | desk |
|---|---|---|---|---|---|---|---|---|
| **M0** head-locked | 0.310 | 0.320 | 0.369 | 0.393 | 0.439 | 0.214 | 0.187 | 0.315 |
| M1 sphere 1 m | 0.208 | 0.254 | 0.339 | 0.364 | 0.197 | 0.250 | 0.221 | 0.221 |
| M2 sphere adaptive | 0.160 | 0.214 | 0.243 | 0.303 | 0.152 | 0.183 | 0.155 | 0.173 |
| M3 mesh | 0.346 | 0.349 | 0.343 | 0.332 | 0.327 | 0.354 | 0.378 | 0.328 |
| M4 + sphere | 0.145 | 0.192 | 0.187 | 0.222 | 0.131 | 0.154 | 0.145 | 0.151 |
| M4 + nearest bg | 0.162 | 0.211 | 0.200 | 0.235 | 0.151 | 0.172 | 0.161 | 0.172 |
| M4 + memory | 0.151 | 0.188 | 0.178 | 0.215 | 0.137 | 0.166 | 0.157 | 0.161 |

By robot location (mean over motions and latencies):

| Method | desk | side | room | far |
|---|---|---|---|---|
| **M0** head-locked | 0.382 | 0.339 | 0.288 | 0.263 |
| M1 sphere 1 m | 0.253 | 0.270 | 0.273 | 0.230 |
| M2 sphere adaptive | 0.263 | 0.220 | 0.161 | 0.147 |
| M3 mesh | 0.326 | 0.447 | 0.462 | 0.144 |
| M4 + sphere | 0.193 | 0.194 | 0.149 | 0.128 |
| M4 + nearest bg | 0.224 | 0.208 | 0.171 | 0.129 |
| M4 + memory | 0.209 | 0.197 | 0.151 | 0.120 |

### Slip by latency

Mean over motions and locations, in deg/s.

| Method | Ideal | WiFi | Remote | Bad |
|---|---|---|---|---|
| **M0** head-locked | 19.1 | 19.7 | 25.7 | 31.3 |
| M1 sphere 1 m | 4.3 | 4.4 | 5.3 | 6.8 |
| M2 sphere adaptive | 2.2 | 2.2 | 2.5 | 2.9 |
| M3 mesh | 1.9 | 1.7 | 1.8 | 1.6 |
| M4 + sphere | 1.5 | 1.6 | 1.6 | 1.6 |
| M4 + nearest bg | 1.7 | 1.6 | 1.7 | 1.6 |
| M4 + memory | 1.5 | 1.5 | 1.6 | 1.6 |

### View lost

Share of frames (%). Fast turn: mean over locations. Tilt: at `far`, the range over the latency levels.

| Method | fastturn Ideal | fastturn WiFi | fastturn Remote | fastturn Bad | tilt at `far` |
|---|---|---|---|---|---|
| **M0** head-locked | 0.8 | 2.1 | 6.7 | 12.2 | 3.8–12.3 (a metric limit, see below) |
| M1 sphere 1 m | 0.0 | 0.0 | 0.0 | 2.8 | 0 |
| M2 sphere adaptive | 0.0 | 0.0 | 0.0 | 1.1 | 0 |
| M3 mesh | 0.0 | 0.1 | 0.1 | 0.3 | 0 |
| M4 (any fill) | 0.0 | 0.0 | 0.0–0.1 | 0.3–0.4 | 0–0.2 |

In the other motions only M0 loses over 1% of frames, and only up to 2%: turn at `far`/Remote and desk at
`far`/Bad.

### Best method per configuration

Number of the 128 configurations in which each method has the best mean.

| Metric | M0 | M1 | M2 | M3 | M4 + sphere | M4 + nearest bg | M4 + memory |
|---|---|---|---|---|---|---|---|
| Misregistration | 0 | 0 | 0 | 76 | 46 | 0 | 6 |
| Slip | 0 | 0 | 12 | 21 | 24 | 0 | 71 |
| Flicker | 4 | 6 | 68 | 18 | 9 | 0 | 23 |
| LPIPS | 0 | 0 | 0 | 0 | 64 | 1 | 63 |
| PSNR | 0 | 0 | 0 | 23 | 17 | 1 | 87 |

## In motion

All methods replay the same motion at the default robot location (`desk`). Each GIF has:
- the third-person view and every method, synchronized;
- the live misregistration in each tile;
- the operator's current action in the footer.

**Fast turn, WiFi.** A 90° snap at 375°/s and back, then a nod. M0 swings with the head while the others hold the
world. The black wedge at the image edge is where the old frame ends.

![fastturn, desk, WiFi](img/fastturn_wifi.gif)

**Fast turn, Bad latency.** Frames are 200 ms old on average and up to 500 ms. At 1.9 s the eye has turned past the old frame. M0 shows a
complete desk in the wrong place; every other method shows black beyond the frame's edge.

![fastturn, desk, Bad](img/fastturn_bad.gif)

**Big lean, WiFi.** The waist stops at about 19 cm of the 40 cm lean. On the way back, M0 is 7° off, M1 6.5° and
M2 4° (parallax). M3 opens large holes beside the cubes, and M4 fills them.

![biglean, desk, WiFi](img/biglean_wifi.gif)

**Stand up, WiFi.** The operator rises 25 cm and the robot stays seated, so M0 is off by up to 15°. M3 and M4 put
everything in place. But the camera never saw the desk from above, so the holes are large and the sphere fill shows
duplicate cubes in them.

![standup, desk, WiFi](img/standup_wifi.gif)

**Tilt, WiFi.** The head rolls 25° and the robot can't. In M0 the world rolls with the head (10° off). The others roll
the image back; the frame's edge shows as a tilted border.

![tilt, desk, WiFi](img/tilt_wifi.gif)

**Strafe, WiFi.** The operator sits still while the base drives 80 cm right and back. The scene is mostly 1–5 m away,
so the parallax is small: M0–M2 are 1.2–1.6° off on average.

![strafe, desk, WiFi](img/strafe_wifi.gif)

**Drive, WiFi.** The base backs up 100 cm and returns. M3 shows holes beside the objects as the view moves in.

![drive, desk, WiFi](img/drive_wifi.gif)

## Findings

### 1. Depth keeps the world in place everywhere

M3 and M4 are 0.01–0.21° off in every configuration, and latency doesn't change that (M3 0.04° at every level).

M4 reads higher than M3 at `side` and `room` (0.10–0.13° vs 0.04°). This is not a worse registration. M4 also shows
its sphere fill where depth is missing, and the fill carries the sphere's error. At those two locations the back walls
have no ZEDlike depth, so M3 shows them black and they aren't scored. The same black walls make M3's LPIPS the worst
there (0.45–0.46).

### 2. Spheres fix rotation and can make translation worse

In the turning motions (turn, fastturn, tilt, desk), M1 cuts M0's misregistration 2.6–5× and M2 8–17×.

For translation, M1 is worse than M0 in most configurations:

| Motion | M1 worse than M0 | M2 worse than M0 |
|---|---|---|
| strafe | 14 of 16 | 2 of 16 |
| drive | 12 of 16 | 0 of 16 |
| biglean | 12 of 16 | 0 of 16 |
| standup | 12 of 16 | 0 of 16 |

Most of M1's losses are at `side`, `room` and `far`. The reason is geometric, for a gap *t*:
- a point at depth *d* really moves by *t*/*d*;
- M0 doesn't move it, so its error is *t*/*d*;
- a sphere of radius *R* moves it by *t*/*R*, so its error is *t*·|1/*d* − 1/*R*|.

The sphere's error is the larger one when *d* > 2*R*: everything beyond 2 m for M1.

M2 sets *R* to the scene's depth and loses to M0 only twice: strafe at the desk, at Remote and Bad latency. There the
base passes a near object, and M2's central window sets *R* from it, dropping from 88 to 60 cm while most of the view
is farther away. At Bad latency, M2 is 3.11° off, M1 2.24° and M0 1.78°.

### 3. Joint limits leave gaps that only depth closes

| Motion | The limit and the gap | M0 | M1 | M2 | M4 |
|---|---|---|---|---|---|
| tilt | no neck roll: 14° on average at every latency | 6.2° | 1.2° | 0.36° | ≤ 0.16° |
| standup | no height: 15 cm on average, 28 cm at most | 4.1° | 4.9° | 1.8° | ≤ 0.13° |
| biglean | the waist reaches about 19 cm of 40: 9.5–11.4 cm on average, up to 25 cm | 3.8° | 3.6° | 1.5° | ≤ 0.21° |
| fastturn | the neck's 300°/s limit adds to the latency: up to 34° at Ideal and 89° at Bad | 5.2° | 1.3° | 0.48° | ≤ 0.11° |

M0, M1 and M2 are means over locations and latencies; M4 is the largest value over every configuration and fill.

- **tilt:** rotation compensation handles the roll the robot can't make.
- **standup:** M1 is worse than M0 here, because the gap is translation (finding 2). M4 still has its highest LPIPS
  of all motions (0.22). From 25 cm lower, the camera never saw the tops of the desk objects, so the holes are large.
- **fastturn:** M0 slips at 70 → 113°/s on average from Ideal to Bad; its p95 at the desk goes from 323 to 452°/s.

### 4. Latency: rotation errors grow, joint-limit errors don't

M0's misregistration in the turn goes from 3.1° to 7.5° at the desk, from Ideal to Bad. Its slip goes from 19 to
31°/s over all configurations. M1 and M2 grow slowly, and M3 and M4 not at all.

What latency costs M4 is image quality, through larger holes and more of the view outside the old frame. M4 + sphere's
LPIPS from Ideal to Bad:

| Motion | Ideal | Bad |
|---|---|---|
| fastturn | 0.157 | 0.245 |
| turn | 0.126 | 0.173 |
| standup | 0.217 | 0.228 (the gap doesn't depend on latency) |

### 5. Losing the view

In the fast turn at Remote and Bad latency, the eye turns past the edge of the shown frame:
- **M0** still shows a complete, plausible image, in the wrong place. It is lost in 5–9% of frames at Remote and 9–16%
  at Bad.
- **The other methods** show black beyond the frame's edge, and are lost in 0–3.6% of frames.

M0's mean misregistration leaves those frames out, so it understates M0 there. At `side`, for example, it reads
7.07° at Remote and 5.02° at Bad.

### 6. The robot's location

The farther the scene, the less a given gap matters: translation parallax shrinks, and so does the camera's offset
from the eye. M0 is 4.7° off at the desk and 2.8° at `far`.

M2's lead over M1 grows with distance:
- **At the desk** they are about equal, because *R* settles at about 85 cm, close to 1 m. M2 has the better
  misregistration in 24 of 32 configurations; M1 has the better LPIPS in 24 of 32.
- **At every other location** M2 is better in all 32 configurations, on both metrics. At `far` it is 0.27° off
  against M1's 2.24°.

### 7. Choosing M4's fill

M4 has the best LPIPS in all 128 configurations. Among its fills:

| Fill | Where it leads |
|---|---|
| Sphere (fill 0) | LPIPS in 63 of the 96 configurations at `desk`, `side` and `room` |
| Memory (fill 2) | LPIPS in 29 of 32 at `far`; the lowest slip in 71 of 128; the best PSNR in 87 of 128 |
| Nearest background (fill 1) | never best on LPIPS, except in one configuration |

Memory's known cost, from the main comparison, is stale fragments of things that moved: the fan and the robot hands.

### 8. No single metric ranks the methods

- **M3** has the best misregistration in 76 configurations, but its LPIPS is worse than M0's in 68 of 128, because of
  its holes.
- **M2** has the least flicker in 68 configurations: a sphere has no holes opening and closing.

## Limits

- **Idealized depth and pose.** The depth is ground truth with modelled errors, not stereo matching. The errors don't
  depend on image content, and each frame carries its exact camera pose. So the M3 and M4 registration (0.01–0.21°)
  holds for an exact pose and is optimistic for a real robot; see the
  [repo README's limitations](../../../README.md#limitations-and-next-steps).
- **One run per configuration.** The replay is deterministic, so a rerun gives the same numbers. But each
  configuration samples one realization of the latency's jitter and loss bursts; another seed would move individual
  frames.
- **The props stay by the desk.** At other locations, the desk motion's hands reach into empty space: the grab
  misses (at `side` the nearest cube is 51 cm from the hand), so nothing is carried.
- **The tilt/`far` lost frames are a metric limit.** M0 loses 4–12% of frames there at every latency. The view at
  `far` is mostly repetitive floor and wall tiles, and when M0's image is rolled 24° against GT's, the feature
  matcher finds too few matches. M0's image there is complete and plainly the same view, rolled. So the M0
  misregistration for tilt at `far` (about 6°) leaves out its worst frames.
- **Near large holes the backward flow is unreliable.** So "matched" undercounts correct content there. It is not a
  quality metric.
- **Narrow setup.** Desktop left eye only, 72 Hz fixed step, ZEDlike depth, the Waist3Neck2 robot, M4 on the
  adaptive sphere. Frames are scored at 640×360. No user saw these runs; the metrics stand in for what an operator
  would notice.
- **Small M4 fill differences.** The fills are close (LPIPS 0.15–0.17 on average), and the ranking can flip with the
  scene.

## Reproduce

```powershell
# all 896 runs and scoring, resumable (~4.6 h here: 3 game processes at a time, scoring overlaps)
python Tools\replay_matrix.py --parallel 3 --keep desk/wifi desk/bad --metrics-python <venv>\Scripts\python.exe
python Tools\replay_matrix_report.py Reprojection\Saved\TeleopReplay\matrix docs\eval\2026-11-05-matrix
python Tools\replay_gifs.py Reprojection\Saved\TeleopReplay\matrix\fastturn\desk\wifi <out> --only compare --grid-tile 200
```

- **Stations, latency levels and the method list** are at the top of `Tools/replay_matrix.py`. The motions are in
  `Tools/replay_trajectory.py --motion`.
- **Frames** are kept, at about 0.85 GB per configuration (108 GB for the whole matrix). `--delete-frames`
  removes them after scoring.
- **`--keep`** names the station/latency pairs that also save observer frames, for the GIFs.
- **Scoring** needs numpy, opencv, torch + lpips and matplotlib; the GIFs need ffmpeg.
