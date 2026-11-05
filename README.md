# Reprojection: a remote VST teleoperation simulator

An operator wears a headset and sees through the stereo cameras on a robot's head (video see-through, VST). This
project simulates that chain in Unreal Engine 5.7: the robot, its cameras, a noisy network and imperfect depth. The
operator can switch between presentation methods live in the headset (M0–M4, GT), and the same methods can be
compared offline against ground truth.

![Spectator view in VR: third-person observer on the left, the operator's left eye on the right (M3 depth mesh)](docs/img/move_M3.jpg)

*The PC window during a VR session. Left: a third-person observer view. The operator is in green, and the robot
camera is in orange. Yellow is where the frame now on the headset was taken. Right: what the operator's left eye
sees. Here the operator has moved 15 cm to the right and the robot hasn't followed yet, so M3 leaves dark holes where
the old camera position could not see.*

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
  each one switch. The same frozen instant can be shown with every method.
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
| Evaluation tools | Python 3 with numpy, scipy, pillow, and torch + lpips for LPIPS (`Tools/`) |
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

### Screenshots

These are the PC window during VR Preview on the Meta XR Simulator, in the spectator layout:

- **Left:** the third-person observer, which the operator never sees. Green is the operator now (head, view and
  hands). Orange is the robot camera now. Yellow is the capture pose of the frame on the headset. Grey is the robot's
  body.
- **Right:** the operator's left eye.

**Setup.** WiFi latency and ZEDlike depth. Before freezing, the operator swayed for 3 s, so M2's radius (4.4 m) and the
memory (9 keyframes) built up as they would in use.

**How the shots were made.** Every method shows the same instant: `teleop.Debug.Freeze` holds the robot, the props and
the shown frame.
- **The move.** The operator was shifted against the remote world with `teleop.Debug.MapOffset*` (a scripted headset
  move wasn't possible), so the robot hands moved with the operator.
- **The image.** Each picture combines the two images the spectator screen shows: the observer render target and the
  left eye buffer.
- **The HUD.** While frozen, its latency figures read 0.
- **Scripts.** `Tools/readme_shots.ps1` and `Tools/readme_compose.py`.

#### Sideways move: the operator 15 cm to the right; the shown frame is 111 ms old

**GT: the reference.** The green frustum is 15 cm right of the yellow one. The robot camera (orange, behind the
yellow) hasn't followed.

![GT](docs/img/move_GT.jpg)

**M0: head-locked.** This is the old frame as the camera saw it, 15 cm to the left. Near things (ball,
checkerboard, hands) sit several degrees away from where GT has them. In motion, the whole world moves with the head.

![M0](docs/img/move_M0.jpg)

**M1: sphere, R = 1 m.** Objects about 1 m away land roughly right. The wall, about 4–5 m away, is over-shifted and
bent (its grid lines curve), because everything is treated as if it were 1 m away.

![M1](docs/img/move_M1.jpg)

**M2: sphere, adaptive R (4.4 m here).** The wall and the doorway are straight and in place. Now the near ball, hands
and checkerboard are misplaced instead: a sphere can be right at one distance only.

![M2](docs/img/move_M2.jpg)

**M3: depth mesh** (the image at the top). Everything with depth is where GT has it, but the old camera saw nothing in
these dark areas:
- the "shadows" to the right of the hands, the ball, the checkerboard and the box. These are disocclusions, revealed
  by moving right.
- the sky and the room behind the doorway, which are beyond stereo range;
- speckles where ZEDlike depth has holes.

**M4 fill 0: mesh + sphere.** The sky and the doorway are back, but each disocclusion shows a **ghost copy of the
object in front of it**: a second left hand, a doubled fan, an extra strip of checkerboard. The sphere samples the
old frame where the occluder was. This is inherent to the sphere fill, not a bug.

![M4 fill 0](docs/img/move_M4f0.jpg)

**M4 fill 1: mesh + nearest background.** No ghost copies. Instead, background colour is smeared across each hole,
which shows as streaks next to the hands.

![M4 fill 1](docs/img/move_M4f1.jpg)

**M4 fill 2: mesh + keyframe memory.** Earlier frames saw most of the revealed background, so the ball and
checkerboard holes are filled with what is really there. What remains are **stale fragments of the robot hands**: they
moved after those keyframes were taken, and the memory cannot know that.

![M4 fill 2](docs/img/move_M4f2.jpg)

#### Head turn: the operator turned 25° to the right; the shown frame is 83 ms old

**GT.**

![GT turn](docs/img/turn_GT.jpg)

**M0.** The old frame stays in front of the face, so the remote world turns with the head. Of all the errors, this
is the one that makes people sick.

![M0 turn](docs/img/turn_M0.jpg)

**M1** (M2 to M4 behave the same here). The old frame is put back where it belongs in the world, nearly exactly for a
pure turn. The part of the new view that the robot camera hasn't seen yet is black, at the right edge. It fills in as
the neck catches up.

![M1 turn](docs/img/turn_M1.jpg)

### Metrics

Each frozen state is rendered with every method and compared with GT. The comparison covers 12 desktop scenes,
11 presentations, two depth models and a VR frame-rate run per method. Regions come from geometry, so they are the
same for every method:
- holes even with exact depth (disocclusions);
- holes only from degraded depth;
- pixels outside the frame.

Mean over the 11 scenes with an offset (4–20 cm, 0–15°), ZEDlike depth:

| Method | LPIPS ↓ | PSNR dB ↑ | Disocclusion error ↓ | Misregistration px ↓ | False edges % ↓ | VR fps (72 Hz cap) |
|---|---|---|---|---|---|---|
| M0 | 0.434 | 15.0 | 53.1 | 44.9 | 60.5 | 69.1 |
| M1 (R 1 m) | 0.325 | 16.6 | 37.1 | 34.1 | 46.1 | 69.1 |
| M2 (adaptive R) | 0.256 | 18.0 | 40.5 | 25.1 | 31.3 | 70.0 |
| M3 | 0.264 | 20.3 | 73.8 | 0.6 | 19.0 | 69.5 |
| M4 fill 0 | **0.145** | 23.4 | 39.7 | **0.5** | 10.1 | 69.1 |
| M4 fill 1 | 0.153 | 23.7 | 34.5 | **0.5** | 11.6 | 69.0 |
| M4 fill 2 | 0.146 | **24.6** | **25.2** | 0.6 | **9.1** | 57.1 |

What the columns measure:
- **LPIPS:** learned perceptual distance.
- **PSNR:** whole-frame pixel fidelity.
- **Disocclusion error:** mean |RGB| difference (0–255) in the holes that remain even with exact depth.
- **Misregistration:** the median block-matching shift where real data exists, about 0.09° per px.
- **False edges:** edges with no GT edge nearby (ghosts, hole borders), as a % of GT edges.

GT itself runs at 68.6 fps on the same setup, which topped out at about 69–70 fps that day.

What these numbers say:
- **M4 is best on every image metric.**
- **The three fills are close on LPIPS.**
  - Memory (fill 2) fills disocclusions best and has the best PSNR.
  - But it pastes stale content, such as old robot hands and fan blades. At zero offset it is the only method worse
    than M0.
  - With a full memory (16 keyframes) it drops to 57 fps in VR.
- **No single metric ranks the methods.** PSNR puts M3 well above M2, while LPIPS rates them about equal: M3 places
  everything exactly but leaves dark holes.

The full write-up has the protocol, all 12 scenes, the per-scene tables, the screenshot sheets and the limits:
[`docs/eval/2026-11-04-methods/README.md`](docs/eval/2026-11-04-methods/README.md). Reproduce it with
`Content/Python/teleop_eval.py`, `Tools/eval_metrics.py` and `Tools/eval_report.py`.

**What the metrics don't capture yet, and could:**
- **Time.** Flicker, swimming during head motion, the jump when a new frame arrives, and judder from dropped frames.
  These need a video metric (e.g. temporal warping error) on recorded motion, not frozen frames.
- **Binocular effects.** Each eye is reprojected on its own, so the two eyes can see different holes or ghosts
  (rivalry, wrong vertical disparity).
- **Where the operator looks.** Errors at the centre of view and on the manipulated object matter more than in the
  periphery.
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
- **Temporal and binocular evaluation, and a user study.** See the metric gaps above.
- **Moving objects.** No reprojection can remove their latency. That is the next step in the brief (S8): interaction
  latency and a predicted "ghost hand".
- **Validate the models against real data.** Latency traces from the real network, depth from the real stereo camera
  (e.g. ZED), and the real robot's neck and waist dynamics.
- **Measure in the headset.** The method evaluation ran on the desktop (mono); VR frame rates came from the simulator.

## Repository layout

```
Reprojection/                  UE 5.7 project (Reprojection.uproject)
  Source/Reprojection/         game module: game mode, operator pawn, robot head, presenter, latency and depth models,
                               keyframe memory, observer view, CVars
  Source/ReprojectionShaders/  maps Shaders/ to /Reprojection so material Custom nodes can #include plain HLSL
  Shaders/Teleop/              camera model, presentation (M0-M2, M4 fills), depth mesh (M3), depth degradation, memory
  Content/Python/              builds the materials from code (teleop_materials.py); evaluation and test scripts
  Plugins/MCPUnreal/           editor plugin for the mcp-unreal MCP server
Tools/                         evaluation metrics and the HTML report generator
docs/                          evaluation write-ups and README images
HANDOFF.md                     original design brief (Chinese)
CLAUDE.md                      working notes for coding agents: code map, conventions, verified results
```
