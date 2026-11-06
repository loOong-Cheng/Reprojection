# Reprojection

A simulator for latency compensation in VR video see-through (VST) teleoperation, built with Unreal Engine 5.7 (C++).

It models the path from a stereo camera on a robot's head to the operator's headset:
- network latency;
- degraded stereo depth;
- the robot's neck and waist dynamics.

It implements five presentation methods (M0–M4). You can switch them live in the headset, or evaluate them offline
against a ground-truth render (GT).

![All methods replaying the same operator trajectory, synchronized](docs/img/replay/compare.gif)

*All methods replay the same 10 s operator trajectory (WiFi latency, ZEDlike depth). Top left is a third-person view.
The number next to each label is that frame's misregistration against GT.*

The methods use only data a real system has: degraded colour and depth, capture timestamps, robot kinematics and the
HMD pose. Ground truth is used only for GT, the metrics and the degradation models. The design brief, in Chinese, is
[`HANDOFF.md`](HANDOFF.md).

## Features

- **Latency model** (`FTeleopLatencyModel`):
  - Gilbert-Elliott burst loss, AR(1) log-normal jitter and fixed pipeline stages, plus a separate command channel.
  - Presets Ideal, LAN, WiFi, Remote and Bad, with a measured p50 of 28 to 281 ms.
- **Depth model** (`UTeleopDepthDegrader`):
  - block-sparse stereo depth with holes at edges, disparity noise, 1/8 px quantization and a range limit;
  - presets DepthGT, ZEDlike, MobileStereo and Sparse.
- **Robot** (`ARobotHead`):
  - a 3-DoF waist under a 2-DoF neck, with lag and rate and acceleration limits, and an optional driving base;
  - 60 Hz stereo capture with a 64 mm baseline;
  - hands (`ARemoteHand`) that follow the controllers after the command latency and grab scene objects.
- **Presentation:** M0–M4 and GT, switchable at runtime (see [Methods](#methods)).
- **Evaluation:**
  - deterministic replay of a trajectory on a fixed 72 Hz time step;
  - per-frame LPIPS, PSNR, misregistration, slip and flicker;
  - GIF output;
  - a test matrix of motion × robot location × latency.
- **Tooling:**
  - a third-person observer view in the PC window;
  - an in-headset blind test that logs to CSV;
  - every tunable exposed as a `teleop.*` console variable (`TeleopCVars.cpp`).

## Methods

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

| Method | Description | Implementation |
|---|---|---|
| **M0** head-locked | The newest frame as received, with no compensation. This is the baseline. | `Present.ush` (`M_TeleopPresent`) |
| **M1** sphere, R = 1 m | Reprojects the frame onto a sphere of radius R around the capture camera. Exact for rotation; with a translation t, the error is about t·(1/d − 1/R). | `Present.ush`, `Camera.ush` (mirrored and unit-tested in `TeleopMath.h`) |
| **M2** adaptive sphere | M1 with R set to the median depth at the centre of the view, taken over 2 s and eased over 150 ms. | `ATeleopPresenter::UpdateAdaptiveRadius` |
| **M3** depth mesh | A grid mesh displaced by the degraded depth, rendered from the current eye. Triangles across depth discontinuities are cut, and pixels without depth stay holes. | `DepthMesh.ush` (`M_TeleopDepthMesh`) |
| **M4** mesh + fill | M3 over M2's sphere. `teleop.M4.Fill` picks the hole fill: 0 = the sphere (default), 1 = the nearest background pixel, 2 = keyframe memory (up to 16 earlier frames kept as depth meshes). | `Present.ush`, `UTeleopMemory`, `Memory.ush` |
| **GT** | The remote scene rendered from the current eye. Reference only. | The operator pawn is placed at the robot station. |

## Requirements

| | |
|---|---|
| Engine | Unreal Engine 5.7.4. Do not open the project with 5.8, which converts it. |
| Compiler | Visual Studio 2022 with the C++ game workload (MSVC 14.38) |
| Headset | Meta Quest 3 over Link or Air Link, through OpenXR. PC VR only. |
| XR plugins | Epic OpenXR, plus Meta XR (`OculusXR` 1.201.0) installed in the engine. `XrApi=OVRPluginOpenXR` |
| Without a headset | Meta XR Simulator v205, or desktop Play-In-Editor |
| Evaluation | Python 3 with numpy, scipy, pillow, opencv, matplotlib, torch and lpips; ffmpeg |
| Optional | [mcp-unreal](https://github.com/remiphilippe/mcp-unreal) for agent-driven editor automation (`Plugins/MCPUnreal`, `.mcp.json`) |
| Tested on | Windows 11, RTX 3080 Ti, i9-12900KF, 32 GB RAM |

Binary assets are stored in Git LFS.

## Build and run

```powershell
git lfs install
git clone https://github.com/loOong-Cheng/Reprojection.git
cd Reprojection
& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" ReprojectionEditor Win64 Development "-Project=$PWD\Reprojection\Reprojection.uproject" -WaitMutex
```

Open `Reprojection/Reprojection.uproject`. The default map, `VRTemplateMap`, is the remote world. `TeleopGameMode`
builds the rest of the session when play starts.

- **Quest 3:** connect through Meta Horizon Link, then choose **Play ▸ VR Preview**. The session starts in GT and
  recenters once the headset is tracked.
- **Simulator:**
  - Set `XR_RUNTIME_JSON=C:\Program Files\MetaXRSimulator\v205.0\meta_openxr_simulator.json`, then start the editor
    from that shell.
  - After the simulator starts, stop `synth_env_server.exe`: it costs about 4.5 ms of GPU time per frame and leaks
    memory.
- **Desktop PIE:** drive the head with the `teleop.Debug.Head*` console variables.

### Controls

| Input | Action |
|---|---|
| A | Cycle methods: GT → M1 → M2 → M3 → M4 → M0 |
| B | Latency preset |
| X | Neck mode: Fixed / Rot2DoF / Rot3DoF / Full6DoF / Waist3+Neck2 (default) |
| Y | Jitter buffer on/off |
| Right stick X | Base latency −/+10 ms |
| Right stick Y | Robot camera marker on/off (during the blind test, the rating instead) |
| Left stick click | Depth preset |
| Left stick Y / X | Sphere radius ±10 cm / stereo baseline ±5 mm |
| Left menu | Blind test: hidden method names, 1–7 ratings written to CSV |
| Right stick click | Recenter |
| Grips, triggers, touch | Robot hand: grab, release and finger poses |
| PC keys V / C / R | Observer camera preset / camera marker / recenter |

## Results

The table covers the 10 s trajectory replay: WiFi latency, ZEDlike depth, the default robot, the left eye on the
desktop. Each value is a per-frame mean against GT; misregistration and slip are given as mean / p95.

| Method | LPIPS ↓ | PSNR dB ↑ | Misregistration ° ↓ | Slip °/s ↓ | Flicker ↓ |
|---|---|---|---|---|---|
| M0 | 0.367 | 13.3 | 3.24 / 9.59 | 23.4 / 103 | 5.16 |
| M1 | 0.206 | 17.2 | 1.07 / 2.50 | 2.8 / 7.9 | 1.79 |
| M2 | 0.211 | 17.6 | 1.07 / 2.54 | 2.4 / 5.7 | 1.77 |
| M3 | 0.315 | 15.8 | **0.03** / **0.04** | 2.1 / 4.6 | 2.38 |
| M4 fill 0 | **0.161** | **20.0** | 0.04 / 0.05 | 1.9 / 4.0 | **1.63** |
| M4 fill 1 | 0.197 | 19.4 | 0.04 / 0.05 | 2.1 / 4.6 | 2.31 |
| M4 fill 2 | 0.185 | 19.9 | 0.04 / 0.05 | **1.7** / **3.5** | 1.75 |

- **Misregistration:** the angular offset of the content shown against GT. It is measured with optical flow
  initialized from a homography, over content the method actually shows.
- **Slip:** the method's frame-to-frame image motion minus GT's.
- **Flicker:** the warping error between consecutive frames.
- **Noise floor:** a repeated GT run scores 0.46 flicker and zero on every other metric.

![Misregistration over time](docs/img/replay/misregistration.png)

Findings:
- **M0 fails in motion, not at rest.** At rest it is 1.1° off; during head turns it peaks at 17° and slips at over
  100°/s.
- **The spheres (M1, M2)** remove most of the rotation error. They leave parallax that depends on the scene's depth.
- **The depth mesh (M3, M4)** puts what it shows in place to under a pixel, given an exact camera pose (see
  [Limitations](#limitations-and-next-steps)). M3's holes make its image metrics poor, and the M4 fills trade
  ghosting (fill 0) against streaks (fill 1) and stale content (fill 2).
- **No single metric ranks the methods.** For example, misregistration ranks M3 first, while LPIPS ranks it
  second-worst.

<details>
<summary>Per-method GIFs (third-person view + left eye)</summary>

In the third-person view, green is the operator now, orange is the robot camera now, and yellow is the capture pose of
the frame on screen.

**GT**: reference.

![GT](docs/img/replay/GT.gif)

**M0**: the world turns with the head until newer frames arrive.

![M0](docs/img/replay/M0.gif)

**M1**: rotation compensated. Near and far content slip against each other during the lean.

![M1](docs/img/replay/M1.gif)

**M2**: here R settles on the desk (82–89 cm), so M2 behaves almost like M1.

![M2](docs/img/replay/M2.gif)

**M3**: registered, with holes: beyond the stereo range, at depth edges, and disocclusions.

![M3](docs/img/replay/M3.gif)

**M4 fill 0**: the sphere fills M3's holes. Foreground objects show up doubled ("ghosts") in disocclusions.

![M4 fill 0](docs/img/replay/M4f0.gif)

**M4 fill 1**: the nearest background pixel. No ghosts, but streaks that change from frame to frame.

![M4 fill 1](docs/img/replay/M4f1.gif)

**M4 fill 2**: keyframe memory. The background is correct, but moving objects leave stale fragments.

![M4 fill 2](docs/img/replay/M4f2.gif)

</details>

Detailed reports:
- **[Trajectory replay](docs/eval/2026-11-05-replay/README.md):** per-segment tables, the determinism check, metric
  definitions and validation.
- **[Test matrix](docs/eval/2026-11-05-matrix/README.md):** 8 motions (including the robot's joint limits and a
  driving base) × 4 robot locations × 4 latency levels × every method, 128 configurations.
- **[Frozen-frame evaluation](docs/eval/2026-11-04-methods/README.md):** 12 static scenes, errors per region, and VR
  frame rate. M4 fill 2 with 16 keyframes drops to 57 fps in VR, against 68.6 fps for GT.

## Limitations and next steps

- **Idealized depth and camera pose.** The methods never read ground-truth depth. But their depth is ground truth
  with modelled errors (`DepthDegrade.ush`), not the output of a stereo matcher. This favours M3 and M4:
  - **The errors don't depend on image content.** Real stereo fails on textureless, repetitive, specular,
    transparent, dark and thin structures. Here, holes come only from depth discontinuities, the 0.3–5 m range and
    random blobs.
  - **The disparity noise is independent per pixel** (σ = 0.2 px, re-drawn every frame). It is not spatially
    correlated, so it shows as speckle and flicker rather than as warped surfaces.
  - **Each frame carries the exact camera pose and intrinsics.** There is no pose or hand-eye calibration error,
    timestamp offset, lens distortion or rolling shutter.
  - **The colour images are clean:** no sensor noise, motion blur or compression.

  So the sub-pixel registration of M3 and M4 holds for an exact pose; on a real robot it will be worse.
  Planned steps:
  - per-frame pose and timestamp errors;
  - depth dropout that depends on image texture;
  - real stereo matching on the rendered pair (e.g. SGBM);
  - comparison against recorded ZED depth.
- **Mono, desktop and scripted.** All evaluations are mono, on the desktop, with scripted trajectories.
  - Binocular effects are not measured, such as different holes in each eye or wrong vertical disparity.
  - Real motion can be recorded with `teleop.Replay.Record` and replayed.
- **No user data yet.** The blind test is implemented; ratings and task measures (grasp time, success) are not
  collected yet.
- **Keyframe memory (M4 fill 2):**
  - It needs the robot's own hands masked out and stale content rejected.
  - It costs about 7 ms of GPU time in VR with 16 keyframes, so keyframe culling is needed.
- **Hole fills** need temporal stabilization, and depth-aware inpainting is still open.
- **Moving objects** keep their latency under any reprojection. Interaction latency and a predicted "ghost hand" are
  milestone S8 in the brief.
- **The latency and robot models need validation against real data:** network latency traces, and the robot's neck
  and waist dynamics.

## Repository layout

```
Reprojection/                  UE 5.7 project (Reprojection.uproject)
  Source/Reprojection/         game module: game mode, operator pawn, robot head, presenter, latency and depth models,
                               keyframe memory, observer view, trajectory replay, CVars
  Source/ReprojectionShaders/  maps Shaders/ to /Reprojection so material Custom nodes can #include plain HLSL
  Shaders/Teleop/              camera model, presentation (M0-M2, M4 fills), depth mesh (M3), depth degradation, memory
  Content/Python/              material generation (teleop_materials.py), evaluation and test scripts
  Plugins/MCPUnreal/           editor plugin for the mcp-unreal MCP server
Tools/                         evaluation: frozen-scene metrics and report, trajectory replay (replay_*),
                               test matrix (replay_matrix*)
docs/                          evaluation reports (eval/) and README images (img/)
HANDOFF.md                     design brief (Chinese)
CLAUDE.md                      notes for coding agents: code map, conventions, verified results
```
