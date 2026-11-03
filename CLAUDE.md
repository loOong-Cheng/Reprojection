# Reprojection: remote VST teleoperation simulator (UE 5.7, C++)

Simulates the chain "robot-head stereo camera -> operator headset" with realistic, noisy latency and sparse,
imperfect depth, and lets the operator switch presentation/compensation methods (M0-M4, GT) live in the headset.
**The full design brief is `HANDOFF.md` (Chinese). Read it before working on features**; section numbers
(§4 latency model, §5 depth model, §6 methods, §7 pitfalls, §9 milestones S1-S8) are referenced below.

## Layout and build

- Repo root: `D:\Projects\Reprojection`. The UE project is in `Reprojection/` (`Reprojection.uproject`,
  runtime module `Source/Reprojection`).
- Engine: **UE 5.7** at `C:\Program Files\Epic Games\UE_5.7`. Do not open with 5.8 (it converts the project).
- Target: Windows PC VR, Quest 3 over Link / Air Link, OpenXR. PC only for now, no standalone Quest build.
- Build (editor closed, or Live Coding off):
  ```
  & "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" ReprojectionEditor Win64 Development "-Project=D:\Projects\Reprojection\Reprojection\Reprojection.uproject" -WaitMutex
  ```
  With the editor open, use Live Coding instead (Ctrl+Alt+F11, or the MCP `live_compile` endpoint).

## Code map

- `Source/Reprojection`: game module.
  - `ATeleopGameMode` (project default): session coordinator. Spawns `ATeleopOperatorPawn` in
    `ATeleopOperatorRoom` (closed dark box at (-100000, 0, 0)), and at the robot station (60, 0, 0) in the
    Template (40 cm in front of the desk) `ARobotHead`, two `ARemoteHand` and `ATeleopPresenter`. Owns
    `T_map` (remote = local * Calibration * Station; recenter keeps floor height 1:1) and `FTeleopLatencyModel`.
  - `ATeleopOperatorPawn`: records HMD/controller poses (tracking frame) into `FTeleopPoseHistory`; GT moves
    the whole pawn onto the station; world-locked HUD text with custom stencil 1.
  - `ARemoteHand` (S1b): follows controller(t - L_cmd); delayed grab/release call the Template's
    `BP_GrabComponent.TryGrab(MotionController)` / `TryRelease()`; adds the hand velocity on release (throw).
  - `ARobotHead` (S2): neck (Fixed / Rot2DoF / Rot3DoF / Full6DoF, tau 40 ms, 300 deg/s, 2000 deg/s^2),
    camera mount 10 cm fwd / 5 cm up, baseline b, stereo `SceneCapture2D` at f_cam into `UFrameRingBuffer`
    (24 slots x 2 eyes, RGBA16f: RGB scene color, A depth cm).
  - `UTeleopDepthDegrader` (S6, §5): per captured frame and eye, two material passes into the slot's R32f depth
    (capture res / K): block min/max, then fattening, edge holes (disparity jump between adjacent samples),
    disparity noise + 1/8 px quantization (full-resolution focal), range, blocky holes. Presets DepthGT / ZEDlike /
    MobileStereo / Sparse; lower depth rate = copy of the last depth (color/depth misalignment).
  - `FTeleopLatencyModel` (S4, §4): Gilbert-Elliott + AR(1) log-normal jitter + loss + fixed stages; presets Ideal /
    LAN / WiFi / Remote / Bad (+ 5 = Constant `teleop.LatencyMs`); jittered command channel. `teleop.LatencyTest`
    prints p50/p95/p99/loss/histogram per preset.
  - `ATeleopPresenter`: frame selection (newest with A_i <= now, jitter buffer max(A_i, C_i + recent p95), never
    older than shown) and all methods: M0 and M1/M2 in `M_TeleopPresent` (post process), M3 = procedural grid mesh
    (`M_TeleopDepthMesh`, WPO in the vertex shader, attached to the pawn's VROrigin = room frame), M4 = sphere +
    mesh (mesh stencil 2 passes through the post process), GT = pawn moved to the station. M2 radius = median of
    the shown frame's degraded depth in a central window (async GPU readback), smoothed 150 ms. Also HUD, blind test
    (`FTeleopBlindTest`), CSV logs (`FTeleopLogger`, Saved/TeleopLogs/<session>/), `teleop.ScreenshotPair`.
  - `ATeleopProps`: checkerboard (85 cm in front of the station, S5), 1 cm rods, sliding block, fan.
  - `TeleopMath.h` (`FTeleopPinhole`, ray-sphere) mirrors `Shaders/Teleop/Camera.ush`; tests `Teleop.Math.*`
    (`Automation RunTests Teleop.Math`).
  - All tunables are `teleop.*` console variables in `TeleopCVars.cpp`; commands `teleop.Recenter`,
    `teleop.LatencyTest`, `teleop.ScreenshotPair`, `teleop.Blind.Toggle / Rate <d> / Confirm`.
- `Source/ReprojectionShaders`: loads at PostConfigInit and maps `Reprojection/Shaders` to the virtual
  path `/Reprojection`. Put HLSL there (`/Reprojection/Teleop/*.ush`) and `#include` it from material
  Custom nodes, so shader logic is plain text in git.
- `Content/Python/teleop_materials.py`: builds the materials in `/Game/Teleop/Materials` from code.
  Rebuild with `import importlib, teleop_materials; importlib.reload(teleop_materials); teleop_materials.build_all()`.
  Change materials there, not by hand in the material editor. **Never run it during PIE** (editor asset
  functions refuse in PIE, and create_asset then blocks the editor with an "Overwrite Existing Object" dialog).
- `Content/Python/teleop_tests.py`: PIE test helpers (proxy-hand grab/lift/throw on a Template cube).
- Controls (HANDOFF §8, adapted so nothing collides with grabbing): A / X cycle M0>M1>M2>M3>M4>GT (blind: confirm);
  B / Y latency preset; right stick X base latency -/+10 ms; right trigger jitter buffer; right stick Y blind rating;
  left stick click depth preset; left stick Y / X sphere R -/+10 cm, baseline -/+5 mm; left menu blind test;
  right stick click or R recenter; grips grab. Debug: `teleop.EyeTest` 0/1/2, `teleop.Debug.Head*` (desktop).
- Shader conventions (`Shaders/Teleop/*.ush`): per-pixel eye dir from `ResolvedView.ClipToView` +
  `ViewToTranslatedWorld`; eye position = `DFHackToFloat(ResolvedView.WorldCameraOrigin) - RoomSeat`; room -> remote via
  MapX/Y/Z/T params; robot frames are already pre-exposed like the operator view (do not multiply by `PreExposure`;
  M0 == GT brightness was measured). DXC: no `?:` on structs. After editing a `.ush` with the editor open, run
  `recompileshaders changed` before rebuilding materials.
- Interpretations of HANDOFF that are not obvious: disparity noise/quantization in full-resolution pixels (stereo
  matching runs on the camera images); edge holes in depth pixels (so Sparse, K = 16, is very holey); M3 rubber
  sheet = disparity jump > `teleop.M3.EdgeDispPx` at a 64 mm reference baseline (works with b = 0); a zero baseline
  gives no stereo depth (all holes) except DepthGT.

## Verified so far (2026-11-03)

- S1: per-eye red/green (pixel + vertex stage) in `-emulatestereo` and Meta XR Simulator; room fully closed.
- S1b (desktop PIE, GT): proxy hand grabs a Template cube via `TryGrab`, lifts it, throws it (~250 cm/s).
- S2: frame age min = teleop.LatencyMs, p50 = L + ~half a camera period (50/100/200 ms tested); M0 == GT at
  zero latency/offset (luma ratio 1.006, mean |diff| 1.8/255); neck lag 40 ms vs head (L_cmd 10 + tau).
- S3 (desktop PIE, camera at the eye, zero latency): M1 == GT with a centered head (mean |diff| 3.5/255); with the
  neck Fixed and the head yawed 20 deg, M1 vs GT inside the camera FOV 5.0/255 (M0: 63.5); 0 px offset.
- S4: `teleop.LatencyTest` p50/p95 (ms): Ideal 28.5/29.8, LAN 38.4/43.8, WiFi 53.6/89.4, Remote 93.7/190, Bad 281/365;
  loss and bad-state share match the chain's stationary values; in-run capture.csv WiFi 53.1/90.0, loss 0.83%.
  Jitter buffer (WiFi): age p50 ~62 -> ~80 ms, p95 - p50 spread ~15-45 -> ~10 ms.
- S5: head translated 5 cm, checkerboard at d = 90.6 cm, M1 shift vs GT: R 50 / 90 / 100 / 1000 cm ->
  -30 / 0 / +4 / +34 px, predicted t(1/d - 1/R) f = -28.7 / -0.2 / +3.3 / +32.1 px.
- S6: DepthGT + zero latency + 5 cm translation: M3 at the GT position (0 px), only disocclusion holes; mean |diff|
  vs GT: M1 14.3, M3 6.7, M4 4.8. ZEDlike / MobileStereo / Sparse show increasing holes and distortion.
- S7: M2 radius 507 cm vs 508 expected (wall); M4 fills M3 holes from the sphere; blind test writes blind.csv;
  screenshot pairs; capture.csv / display.csv per session.
- Performance (Meta XR Simulator at 72 Hz, editor closed, `UnrealEditor.exe -game -vr`, OVRPlugin backend):
  plain Template 72.0 fps (GPU 4.6 ms); full rig with both robot cameras at 60 Hz 71.5 fps (min 69.5, GPU 5.1 ms);
  rig without cameras 72.0. The rig itself costs ~0.5 ms GPU per frame. The game thread mostly waits in WaitFrame.
- How to measure: `teleop.Debug.QuitAfterSec N` exits cleanly; the module logs `[fps]` every 2 s (any game mode);
  the presenter logs `[stats]` (frame age + fps). Run-to-run spread on the simulator is +-3 fps; if a run hangs at
  frame 0, restart `MetaXRSimulator.exe`. Plain Template baseline:
  `/Game/VRTemplate/VRTemplateMap?game=/Game/XRFramework/Blueprints/BP_XRGameMode.BP_XRGameMode_C`.

## XR runtime: Quest 3 or Meta XR Simulator

- Plugins: Epic `OpenXR` + Meta XR `OculusXR` 1.201.0 (installed in the engine, `Engine\Plugins\Marketplace\MetaXR`,
  not in the repo). Backend `XrApi=OVRPluginOpenXR`, like the i105 project. Do not switch to `NativeOpenXR`: on this
  PC it ran ~10% slower (61-66 fps vs 72 on the simulator) and later hung at startup with
  "Failed to get an OpenXR graphics binding".
- Meta XR Simulator v205 is installed at `C:\Program Files\MetaXRSimulator\v205.0`. A process uses it when the env var
  `XR_RUNTIME_JSON` points at `meta_openxr_simulator.json` there (this is all the Meta toolbar toggle does, plus a
  restart). To test in the simulator, launch the editor or game from a shell with that variable set; launching
  normally uses the real runtime (Meta Horizon Link) again.
- Headless check without a headset: run the game standalone, `UnrealEditor.exe <uproject> /Game/VRTemplate/VRTemplateMap
  -game -vr -windowed -ExecCmds="teleop.EyeTest 1, vr.SpectatorScreenMode 2"` with `XR_RUNTIME_JSON` set. Spectator
  mode 2 shows both eyes side by side in the game window. `-emulatestereo` (no runtime) also works for quick checks.

## Driving the editor (MCP)

`.mcp.json` starts `%USERPROFILE%\go\bin\mcp-unreal.exe`, which talks to the editor plugin
`Reprojection/Plugins/MCPUnreal` (HTTP 127.0.0.1:8090) and the Remote Control API (port 30010). Both only work
while the editor is running with this project. Plugin source: github.com/remiphilippe/mcp-unreal.

## Conventions (from HANDOFF §1, §7)

- UE coordinates: X forward, Y right, Z up, left-handed, **cm**. Cameras look along +X.
- The **remote world is the VR Template's `VRTemplateMap`** as-is (its grabbable objects stay). Do not build a
  new environment. The operator sits in a closed dark room around `(-100000, 0, 0)` cm and never sees the
  Template scene directly; remote pose = `T_map · local pose` (operator seat -> robot station at the table).
- Compensation methods M0-M4 may only use what a real system has (degraded color/depth, capture timestamps,
  robot kinematics, HMD pose). Ground truth and scene knowledge are only for GT/error metrics and for the
  degradation models (HANDOFF §6.1).
- `SCS_SceneColorSceneDepth` alpha = **view-space z in cm along the view axis, not radial distance**.
- Mind reversed-Z and the uv y-flip. Every projection/unprojection path gets a round-trip unit test
  (error < 0.1 mm) before it is used in a material.
- Time: `FPlatformTime::Seconds()` everywhere.
- All tunables are `teleop.*` console variables (HANDOFF suggested a DataAsset; CVars were enough so far).
- Per-eye selection in materials uses `ResolvedView.StereoPassIndex`. It works with Instanced Stereo **on**
  (project default; the fps difference to off was within run-to-run noise) in both pixel and vertex stages;
  HANDOFF §7.1's "turn ISR off" precaution is not needed on UE 5.7. Re-check with `teleop.EyeTest 1` / `2`.

## Git

- Git LFS for `.uasset`/`.umap` and binary media (see `.gitattributes`). `Binaries/`, `Intermediate/`,
  `Saved/`, `DerivedDataCache/` and generated IDE files are ignored.
- Commit email is a repo-local GitHub noreply address (the repo is public).
- Save all assets in the editor before committing.
