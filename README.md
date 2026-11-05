# Reprojection: a remote VST teleoperation simulator

An operator wears a headset and sees through the stereo cameras on a robot's head (video see-through, VST). This
project simulates that chain in Unreal Engine 5.7: the robot, its cameras, a noisy network and imperfect depth. The
operator can switch between presentation methods live in the headset (M0–M4, GT), and the same methods can be
compared offline against ground truth.

![The same 10 s of operator motion with every method, synchronized](docs/img/replay/compare.gif)

*The same 10 s of operator motion, replayed with every method in sync. Top left: a third-person view. Next to it is
GT, what the eye should see now, and the M0 baseline, the newest frame shown as received. The number after each name
is how far that frame puts the world from where it really is (misregistration). Per-method GIFs and the metrics are
under [Methods comparison](#methods-comparison).*

## Motivation

In VR teleoperation the operator's head motion is **local and almost instant**. The camera images are **remote, old
and late by a varying amount**: capture, encoding, the network and the robot's neck add tens to hundreds of
milliseconds. If the newest frame is simply shown in front of the eyes, the remote world turns and slides with every
head movement. That ruins depth perception and causes motion sickness. Reprojection can hide much of this:

- re-render the old frame for the current head pose: a sphere for rotation, depth for translation;
- fill what the old camera never saw.

**Why simulate an imperfect world instead of testing on the robot?**

- **Ground truth.** The simulator can render what the operator *should* see from the current eye. With that, every
  method can be measured, pixel by pixel.
- **Control and repeatability.** Latency (mean, jitter, loss), depth quality, camera baseline and neck dynamics are
  each one switch. The same operator motion can be replayed with every method, frame for frame.
- **Realism where it matters.** A method tuned on perfect depth and constant latency fails on a real robot. So the
  simulator deliberately degrades what a real system has:
  - latency: Gilbert-Elliott bursts, AR(1) jitter, frame loss;
  - depth: block-sparse stereo depth with edge holes, disparity noise and quantization, a limited range;
  - the robot: a waist and neck that lag the head.
- **No cheating.** M0–M4 may use only what a real system has: degraded colour and depth, capture timestamps, robot
  kinematics and the headset pose. Ground truth and scene knowledge are used only for GT, the metrics and the
  degradation models. Methods that work here should then work on hardware.

The original design brief (in Chinese) is [`HANDOFF.md`](HANDOFF.md).

## Environment and hardware

| | |
|---|---|
| Engine | **Unreal Engine 5.7.4**, C++ project. Do not open it with 5.8, which converts the project. |
| Compiler | Visual Studio 2022 with the C++ game workload (built with MSVC 14.38) |
| Headset | **Meta Quest 3** over Link or Air Link. PC VR only through OpenXR, no standalone Quest build. |
| XR plugins | Epic OpenXR + Meta XR plugin (`OculusXR` 1.201.0, installed in the engine). Backend `XrApi=OVRPluginOpenXR`. |
| No headset | Meta XR Simulator v205 (an OpenXR runtime), or desktop Play-In-Editor |
| Editor automation | [mcp-unreal](https://github.com/remiphilippe/mcp-unreal): an MCP server plus an editor plugin (`Reprojection/Plugins/MCPUnreal`, configured in `.mcp.json`), so a coding agent can drive the editor. Also the Python Editor Script plugin and the Remote Control API. |
| Evaluation tools | Python 3 with numpy, scipy, pillow, opencv, matplotlib, and torch + lpips for LPIPS; ffmpeg for the GIFs (`Tools/`) |
| Developed on | Windows 11, RTX 3080 Ti, i9-12900KF, 32 GB |

Binary assets are stored with Git LFS.

### How to run

1. Install UE 5.7 and the Meta XR plugin for it (Fab / Epic launcher). Install Git LFS.
2. Clone and build:
   ```powershell
   git lfs install
   git clone https://github.com/loOong-Cheng/Reprojection.git
   cd Reprojection
   & "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" ReprojectionEditor Win64 Development "-Project=$PWD\Reprojection\Reprojection.uproject" -WaitMutex
   ```
3. Open `Reprojection/Reprojection.uproject`. It opens `VRTemplateMap`, which is the remote world. The game mode
   (`TeleopGameMode`) builds the rest when play starts.
4. **Quest 3:** connect it with Meta Horizon Link (Link or Air Link) and choose **Play ▸ VR Preview**. The session
   starts in GT and recenters once the headset is tracked.
5. **Without a headset:**
   - Set `XR_RUNTIME_JSON=C:\Program Files\MetaXRSimulator\v205.0\meta_openxr_simulator.json` and start the editor
     from that shell.
   - Stop `synth_env_server.exe` after the simulator starts it: it costs about 4.5 ms of GPU time and leaks memory.
   - Desktop Play-In-Editor also works for quick checks; the head is moved with the `teleop.Debug.Head*` console
     variables.

**Headset controls** (all tunables are also `teleop.*` console variables, listed in `TeleopCVars.cpp`):

| Control | Action |
|---|---|
| A | Cycle the method: GT → M1 → M2 → M3 → M4 → M0 |
| B | Latency preset: Ideal / LAN / WiFi / Remote / Bad |
| X | Neck mode: Fixed / Rot2DoF / Rot3DoF / Full6DoF / Waist3+Neck2 (default) |
| Y | Jitter buffer on or off |
| Right stick X | Base latency −/+10 ms |
| Right stick up/down | Robot camera marker on or off (blind test: the rating) |
| Left stick click | Depth preset: DepthGT / ZEDlike / MobileStereo / Sparse |
| Left stick Y / X | Sphere radius R −/+10 cm / camera baseline −/+5 mm |
| Left menu | Blind test (hidden method names, 1–7 ratings written to CSV) |
| Right stick click | Recenter |
| Grips, triggers, touch | Robot hand: grab, release and finger poses (delayed like the head) |
| PC keys V / C / R | Observer camera preset / camera marker / recenter |

`teleop.M4.Fill 0|1|2` selects M4's hole fill. `teleop.Observer 0|1|2` turns the PC window's observer view off, split
or full.

## Methods overview

```
HMD pose ──(command latency)──> robot waist + neck ──> stereo cameras, 60 Hz, 1280×960
                                                          │ colour + depth (scene capture)
                                                          ▼
                                  depth degradation (DepthGT / ZEDlike / MobileStereo / Sparse)
                                                          ▼
                     frame ring buffer {capture time, camera pose, colour, depth} ── network latency model
                                                          ▼
                      newest frame that has arrived (optional jitter buffer) ──> method M0–M4 ──> headset
GT: the remote world rendered directly from the current eye (reference only)
```

What gets simulated:

- **Latency** (`FTeleopLatencyModel`): five presets, from Ideal to Bad. Measured p50 / p95: WiFi 54 / 89 ms,
  Remote 94 / 190 ms, Bad 281 / 365 ms.
- **Robot** (`ARobotHead`): a 3-DoF waist under a 2-DoF neck, with lag and rate limits. Stereo cameras with a 64 mm
  baseline.
- **Depth** (`UTeleopDepthDegrader`): ZEDlike is the default degradation.
- **The scene:** the VR Template map, plus props for parallax and motion:
  - a checkerboard;
  - 1 cm rods;
  - a sliding block and a fan, which move.
- **The robot's hands** follow the controllers with the command latency and grab the Template objects remotely.

| Method | Idea | Implementation |
|---|---|---|
| **M0** head-locked | Show the newest frame fixed in front of each eye, no compensation. | `M_TeleopPresent` post process, `Shaders/Teleop/Present.ush` |
| **M1** sphere, R = 1 m | Per pixel, intersect the eye ray with a sphere of radius R around the capture camera, then project the hit into the old frame. Exact for pure rotation. With translation t the error is about t·(1/d − 1/R). | `Present.ush`, camera model `Camera.ush` (mirrored and unit-tested in `TeleopMath.h`) |
| **M2** sphere, adaptive R | Like M1, with R = the median depth in the centre of the frame (async GPU readback), medianed over 2 s and eased over 150 ms. Quest 3 has no eye tracking, so the centre of the view stands in for gaze. | `ATeleopPresenter` (`UpdateAdaptiveRadius`) |
| **M3** depth mesh | A grid mesh whose vertex shader unprojects the degraded depth into the remote world, rendered from the current eye. Triangles across depth jumps are cut, and pixels without depth stay holes. | `M_TeleopDepthMesh`, `DepthMesh.ush` |
| **M4** mesh + fill | M3 in front of M2's sphere. M3's holes are filled by one of: **fill 0** the sphere (HANDOFF design, default); **fill 1** the nearest background pixel, found by walking from the sphere's sample along the eye–camera offset to the first clearly farther depth; **fill 2** keyframe memory: up to 16 earlier frames, kept as depth meshes on the background side of depth edges, with fill 1 for the rest. | `Present.ush` (`Teleop_PresentBackgroundFill`), `UTeleopMemory`, `Memory.ush` |
| **GT** | The remote world rendered from the eye now. Not available to a real system. | the operator pawn is moved onto the robot station |

## Methods comparison

### Same motion, every method

A still screenshot flatters the baseline. A frozen M0 frame is a complete, sharp image; it is just from the wrong place
and time. So every method replays **the same 10 s operator trajectory**
([`Tools/replay_trajectory.py`](Tools/replay_trajectory.py)):

1. turn the head 45° right, to the purple cubes;
2. reach with the right hand and grab the top cube;
3. carry it to the front while looking back, and drop it;
4. lean 15 cm right and back;
5. look 30° left and back, quickly (up to 144°/s).

Each method runs in its own process on a fixed 72 Hz time step ([`Tools/replay_run.ps1`](Tools/replay_run.ps1)). So
the robot, the latency draws and the shown frames are identical in every run, frame for frame. Settings:
- WiFi latency: the shown frame is 56–125 ms old;
- ZEDlike depth;
- the Waist3Neck2 robot;
- the desktop view from the left eye, with a 90° field of view.

In each GIF:
- **Left: a third-person view** (PC window only).
  - Green is the operator now.
  - Orange is the robot camera now.
  - Yellow is where the shown frame was taken.
  - The robot body is grey and its hands are dark.
- **Right: the operator's left eye.**
- **Footer:** what the operator is doing, how old the shown frame is, and the misregistration.

They play at about real time (12.5 fps).

**GT: the reference.** What the eye should see now; no real system has it. The world stays still while the head
moves. In the third-person view the robot follows the green operator with a lag, and its hand trails the operator's
by the command latency as it picks up the cube.

![GT](docs/img/replay/GT.gif)

**M0, head-locked: the baseline.** The picture is glued to the face. On every turn the world turns with the head and
then catches up when newer frames arrive: up to 15° off on the quick look. During the lean, the view moves only when
the robot's waist follows.

![M0](docs/img/replay/M0.gif)

**M1: sphere, R = 1 m.** Turns are compensated, so the world stays put. On the quick look, a black band appears at the
left edge, which the robot camera hasn't seen yet. Because everything is placed at 1 m, near and far things slip
against each other during the lean (up to about 4°).

![M1](docs/img/replay/M1.gif)

**M2: sphere, adaptive R.** Like M1, but R is the median depth in the centre of the view. Here that is the desk, at
82–89 cm, so M2 behaves almost exactly like M1.

![M2](docs/img/replay/M2.gif)

**M3: depth mesh.** Everything with depth stays in place (0.2°). But there are holes:
- the sky and the room behind the doorway, which are beyond stereo range, stay black;
- speckles at depth edges flicker;
- during the lean, dark "shadows" open beside the cube, the hands and the rods.

![M3](docs/img/replay/M3.gif)

**M4 fill 0: mesh + sphere (default).** The sphere fills M3's holes. This gives the best image scores and the least
flicker. During the lean, the disocclusions show ghost copies of what is in front, such as a doubled fan blade and
doubled rods.

![M4 fill 0](docs/img/replay/M4f0.gif)

**M4 fill 1: mesh + nearest background.** No ghosts. Instead, background colour is smeared into the holes. It shows as
streaks beside the rods and hands that change from frame to frame.

![M4 fill 1](docs/img/replay/M4f1.gif)

**M4 fill 2: mesh + keyframe memory.** Earlier keyframes fill the disocclusions with the real background, and the image
is the most world-locked (0.10°). But the memory cannot know what moved, so fragments of the spinning fan and of the
robot hand stay where they were when a keyframe was taken.

![M4 fill 2](docs/img/replay/M4f2.gif)

### Metrics over the whole motion

| Method | LPIPS ↓ | PSNR dB ↑ | Misregistration ° ↓ (mean / p95) | Slip °/s ↓ (mean / p95) | Flicker ↓ |
|---|---|---|---|---|---|
| **M0** (baseline) | 0.367 | 13.3 | 3.75 / 11.2 | 24.3 / 106 | 5.18 |
| M1 (R 1 m) | 0.206 | 17.2 | 1.28 / 2.92 | 4.2 / 14.6 | 1.82 |
| M2 (adaptive R) | 0.211 | 17.6 | 1.34 / 3.06 | 4.5 / 15.4 | 1.79 |
| M3 | 0.315 | 15.8 | 0.22 / 0.34 | 3.2 / 7.7 | 2.40 |
| M4 fill 0 | **0.161** | **20.0** | 0.12 / 0.22 | 2.4 / 5.7 | **1.64** |
| M4 fill 1 | 0.197 | 19.4 | 0.14 / 0.22 | 2.7 / 6.4 | 2.32 |
| M4 fill 2 | 0.185 | 19.9 | **0.10** / **0.14** | **2.1** / **4.6** | 1.76 |

A second GT run, the noise floor, scores flicker 0.47 and zero on everything else. A repeated M4 fill 2 run matches
its first run to the precision shown.

Every frame *i* is compared with GT's frame *i*:
- **LPIPS** (learned perceptual distance) and **PSNR**: the image error of each frame.
- **Misregistration:** the median optical-flow distance from GT's frame to the method's, over textured pixels the method
  covers. This is "the world is in the wrong place". For M0 it tracks the logged eye–camera rotation (correlation 0.89).
- **Slip:** how fast the image slides against the world, that is, the method's frame-to-frame motion minus GT's.
  World-locked is 0. A head-locked image slips at the head's speed, and judder shows up here too.
- **Flicker:** temporal warping error. The previous frame is carried along GT's motion and compared with this one, as
  mean |RGB| (0–255). It rises with holes that open and close, popping content and judder.

![Misregistration over time](docs/img/replay/misregistration.png)

What the numbers say:
- **The baseline fails in motion, not at rest.** With the head still, M0 is 1.2° off, close to the spheres. In the turns
  it averages 5–6.5°, peaks at 15° and slips at over 100°/s. A still screenshot shows none of this.
- **Rotation compensation is the largest single gain.** The spheres cut misregistration 3×, slip 6× and flicker 3×.
  What is left is parallax, and it doubles during the lean.
- **Depth fixes the registration.** M4 is about 10× better than the spheres. But M3 alone leaves large holes, which
  make it the second worst on LPIPS.
- **Among the M4 fills, motion and image metrics disagree.** Fill 2 (memory) is the most world-locked. Fill 0 (sphere)
  has the best LPIPS and the least flicker: memory flickers more at rest and shows stale fan and hand fragments.
- **No single number ranks the methods.** Misregistration puts M3 above the spheres, while LPIPS and PSNR put it below
  them.

The full write-up, with per-segment tables, the determinism check and the limits, is
[`docs/eval/2026-11-05-replay/README.md`](docs/eval/2026-11-05-replay/README.md). To reproduce, run
`Tools/replay_run.ps1`, then `replay_metrics.py`, `replay_gifs.py` and `replay_plot.py`.

### Frozen-frame metrics (12 scenes)

The earlier evaluation freezes the remote side at 12 instants with larger eye–camera offsets (4–20 cm, 0–15°) and
renders every method for exactly that state. It adds region-specific errors and the VR frame rate. Mean over the 11
scenes with an offset, ZEDlike depth:

| Method | LPIPS ↓ | PSNR dB ↑ | Disocclusion error ↓ | Misregistration px ↓ | False edges % ↓ | VR fps (72 Hz cap) |
|---|---|---|---|---|---|---|
| M0 | 0.434 | 15.0 | 53.1 | 44.9 | 60.5 | 69.1 |
| M1 (R 1 m) | 0.325 | 16.6 | 37.1 | 34.1 | 46.1 | 69.1 |
| M2 (adaptive R) | 0.256 | 18.0 | 40.5 | 25.1 | 31.3 | 70.0 |
| M3 | 0.264 | 20.3 | 73.8 | 0.6 | 19.0 | 69.5 |
| M4 fill 0 | **0.145** | 23.4 | 39.7 | **0.5** | 10.1 | 69.1 |
| M4 fill 1 | 0.153 | 23.7 | 34.5 | **0.5** | 11.6 | 69.0 |
| M4 fill 2 | 0.146 | **24.6** | **25.2** | 0.6 | **9.1** | 57.1 |

- **Disocclusion error** is the mean |RGB| difference (0–255) in the holes that remain even with exact depth.
- **False edges** are edges with no GT edge nearby, such as ghosts and hole borders, as a % of GT edges.
- **The cost of memory:** with 16 keyframes, M4 fill 2 drops to 57 fps in VR (GT: 68.6 fps on the same setup).
- **The full write-up** is [`docs/eval/2026-11-04-methods/README.md`](docs/eval/2026-11-04-methods/README.md).

**What the metrics don't capture yet:**
- **Binocular effects.** Each eye is reprojected on its own, so the two eyes can see different holes or ghosts
  (rivalry, wrong vertical disparity). Both evaluations are mono.
- **Where the operator looks.** Errors at the centre of view and on the manipulated object matter more than in the
  periphery.
- **Real motion.** The trajectory is scripted. `teleop.Replay.Record` records a real one on the Quest for replay.
- **People.** Ratings from the in-headset blind test (stability, comfort, depth plausibility on 1–7 scales) and task
  measures with the robot hand, such as grasp time and success.

## Open points

- **Make memory safe (M4 fill 2).**
  - Keep the robot's own hands out of keyframes. Their pose is known from the robot's kinematics, which is allowed.
  - Drop memory where the current frame disagrees.
  - The brief (§6.1) ruled out multi-frame fusion because of moving objects, and the evaluation shows exactly that
    failure.
- **Cap the cost of memory.** Fill 2 with 16 keyframes costs about 7 ms of GPU time in VR. Cull keyframes that cannot
  help the current eye, or draw fewer of them.
- **Ghosts of the sphere fill (M4 fill 0).** They are inherent; fill 1 trades them for streaks. A better
  depth-aware inpainting is open.
- **Binocular evaluation, real motion and a user study.** The temporal metrics cover one scripted trajectory on the
  desktop (mono). Next: record real operator motion on the Quest (`teleop.Replay.Record`), replay it, and measure
  both eyes. See the metric gaps above.
- **Flicker of the fills.** Memory (fill 2) flickers more than the sphere fill while the head is still, and fill 1's
  streaks flicker most. Both need temporal stabilization.
- **Moving objects.** No reprojection can remove their latency. That is the next step in the brief (S8): interaction
  latency and a predicted "ghost hand".
- **Validate the models against real data.** Latency traces from the real network, depth from the real stereo camera
  (e.g. ZED), and the real robot's neck and waist dynamics.
- **Measure in the headset.** Both evaluations ran on the desktop (mono); VR frame rates came from the simulator.

## Repository layout

```
Reprojection/                  UE 5.7 project (Reprojection.uproject)
  Source/Reprojection/         game module: game mode, operator pawn, robot head, presenter, latency and depth models,
                               keyframe memory, observer view, trajectory replay, CVars
  Source/ReprojectionShaders/  maps Shaders/ to /Reprojection so material Custom nodes can #include plain HLSL
  Shaders/Teleop/              camera model, presentation (M0-M2, M4 fills), depth mesh (M3), depth degradation, memory
  Content/Python/              builds the materials from code (teleop_materials.py); evaluation and test scripts
  Plugins/MCPUnreal/           editor plugin for the mcp-unreal MCP server
Tools/                         evaluation: frozen-scene metrics and report; trajectory replay (replay_*: trajectory,
                               runs, temporal metrics, GIFs, plot)
docs/                          evaluation write-ups (eval/) and README images (img/replay/)
HANDOFF.md                     original design brief (Chinese)
CLAUDE.md                      working notes for coding agents: code map, conventions, verified results
```
