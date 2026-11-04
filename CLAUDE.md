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
    The calibrated eye height is the robot's camera height. Auto recenter (`FTeleopAutoRecenter`) waits for a
    tracked, non-zero head pose at least 50 cm above the floor that stays valid for 0.5 s, and re-arms when the
    headset is put on and on a system recenter. On Link the first poses are all zero, even with `IsTracking()` true.
  - `ATeleopOperatorPawn`: records HMD/controller poses (tracking frame) into `FTeleopPoseHistory`; GT moves
    the whole pawn onto the station; world-locked HUD text with custom stencil 1. Robot camera marker
    (`teleop.Debug.ShowRobotCamera`, default off; user request 2026-11-04): orange point + view frustum at the robot
    camera rig now, mapped into the tracking frame (child of VROrigin, so right in the room and in GT), HUD stencil so
    it shows over every method, hidden in scene captures. Unlit colour (0.35, 0.07, 0): the observer's (1, 0.45, 0.05)
    saturates to yellow under the operator view's exposure.
  - `ARemoteHand` (S1b): the robot hand. Follows controller(t - L_cmd); delayed grab/release call the Template's
    `BP_GrabComponent.TryGrab(MotionController)` / `TryRelease()`; adds the hand velocity on release (throw).
    Looks like the VR Template hand (`SKM_MannyXR_left/right` + `ABP_MannequinsXR`, BP_XRPawn's grip offsets; left =
    mirror, set directly on the anim instance because the component is not a `BP_MannequinsXR`). The fingers play the
    operator's grip / trigger / trigger-touch / thumb-touch (the Template's `IMC_Hands` keys), recorded by the pawn
    in `FTeleopFingerHistory` and delayed by L_cmd; a held object keeps the grasp at 1. The robot has a hand, not a
    gripper (user, 2026-11-04): do not go back to placeholder shapes.
  - `ARobotHead` (S2): Base -> Waist -> NeckPivot -> CameraRig. Neck modes Fixed / Rot2DoF / Rot3DoF / Full6DoF /
    **Waist3Neck2 (default)**; neck tau 40 ms, 300 deg/s, 2000 deg/s^2. Waist3Neck2 = the Sharpa robot (user,
    2026-11-04: 2-DoF head on a 3-DoF body; HANDOFF §3 only mentions the 2-DoF neck): waist yaw/pitch/roll 55 cm below
    the neck pivot (tau 150 ms, 90 deg/s, 600 deg/s^2, limits pitch +-30, roll +-20, yaw +-90) leans so the neck pivot
    goes where the operator's neck is (eye target minus the camera mount; `TeleopMath::WaistLean`, test
    `Teleop.Math.Waist`); waist yaw = `teleop.Waist.YawShare` (default 0) x head yaw. So it follows sideways/forward
    head motion along a sphere around the waist, not height changes, and a sideways lean rolls the cameras.
    Camera mount 10 cm fwd / 5 cm up, baseline b, stereo `SceneCapture2D` at f_cam into `UFrameRingBuffer`
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
    mesh (mesh stencil 2 passes through the post process), GT = pawn moved to the station. M2 radius: per shown frame
    the median of the degraded depth in a central window (async GPU readback); target = median of those over the last
    `teleop.M2.WindowSec` (2 s), then eased with `teleop.M2.SmoothSec` (150 ms). HANDOFF only has the 150 ms smoothing;
    the user found that followed every glance (2026-11-04). M4 uses the same R. Also HUD, blind test
    (`FTeleopBlindTest`), CSV logs (`FTeleopLogger`, Saved/TeleopLogs/<session>/), `teleop.ScreenshotPair`.
  - `ATeleopObserver`: third-person view of the remote world for the PC window (recording, debugging); the HMD is
    unchanged. In VR the spectator screen is split: observer left (`teleop.Observer.Split`), operator eye right
    (`teleop.Observer` 1 split / 2 observer only / 0 off, leaving `vr.SpectatorScreenMode` alone). Scene capture at
    `teleop.Observer.Hz` (30) sized to its window region, fixed manual exposure `teleop.Observer.Exposure` (auto
    exposure adapted to the sky and left the robot black). Shows a grey robot body on the robot's joints, robot
    camera frustum now (orange), capture pose of the frame on the HMD (yellow), the operator undelayed (green ghost
    HMD + frustum + Template hands with current fingers), white gap lines (eye -> camera, ghost hand -> robot hand),
    the operator HUD and a legend. Presets `teleop.Observer.View` / key V: behind-right 45 deg (default: robot
    head + cameras, hands and desk objects in one frame), behind-left 45 deg, front-right over the desk, right side
    (the user found the pure lateral view awkward), top. Overlays are `bVisibleInSceneCaptureOnly` and in the robot cameras' HiddenActors; they cast no
    shadows. Text uses `M_TeleopTextUnlit` (the engine text material is lit). Without an HMD nothing is captured;
    `DebugCaptureNow()` + `RenderingLibrary.export_render_target` for tests (the first capture after start may miss
    translucency while shaders compile).
  - `ATeleopProps`: checkerboard (85 cm in front of the station, S5), 1 cm rods, sliding block, fan.
  - `TeleopMath.h` (`FTeleopPinhole`, ray-sphere) mirrors `Shaders/Teleop/Camera.ush`; tests `Teleop.Math.*`
    (`Automation RunTests Teleop.Math`).
  - `TeleopOvrpLogFilter`: drops a known-harmless OVRPlugin error that floods the log on the simulator (XR runtime
    section).
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
- Controls (HANDOFF §8, adapted so nothing collides with grabbing or the robot fingers): the session starts in GT and A
  cycles GT>M1>M2>M3>M4>M0 (user, 2026-11-04: starting head-locked in M0 was unpleasant; `teleop.Method` values are
  unchanged, 0 = M0 ... 5 = GT) (blind: confirm); B latency preset; X neck mode (Fixed > Rot2DoF > Rot3DoF > Full6DoF > Waist3+Neck2; keyboard N);
  Y jitter buffer (keyboard J); right stick X base latency -/+10 ms; right stick Y blind rating during the blind
  test, otherwise up or down toggles the robot camera marker (keyboard C);
  left stick click depth preset; left stick Y / X sphere R -/+10 cm, baseline -/+5 mm; left menu blind test;
  right stick click or R recenter; grips grab. Grips, triggers and touch drive the robot fingers, so **never bind
  a mode switch to a trigger**: on the Quest (2026-11-04), squeezing the triggers to curl the fingers cycled the neck
  mode 10 times in 11 s (the analog axis re-fires a bool action on every small release) and kept toggling the
  jitter buffer.
  PC keyboard V: observer camera preset; C: robot camera marker. Debug: `teleop.EyeTest` 0/1/2, `teleop.Debug.Head*`
  (desktop), `teleop.Debug.HideRobotHands`, `teleop.Debug.ShowRobotCamera`.
  Every control logs an `[input]` line and every resulting change a `[control]` line; a grab that finds nothing
  logs the nearest grabbable's offset; `teleop.Debug.LogHandsSec 0.5` logs remote hand positions and fingers
  (`[hand]`), the head pose (`[head]`) and the robot's waist / neck angles and camera position (`[robot]`).
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
- Controllers (2026-11-04, simulated controllers on the Meta XR Simulator -> OpenXR -> Enhanced Input, left and right
  separately): every control above fires its action; the left proxy hand was steered onto a Template cube with the
  simulated controller, grabbed, lifted and released it; the blind test ran on buttons only (start, rate, blind.csv, stop).
  This found a crash, now fixed: M2's GPU readback overflowed its staging texture when a depth preset change raised the
  depth resolution (D3D12 E_INVALIDARG, fatal).
- Quest 3 over Link (2026-11-04, VR Preview): all controls register, ~70 fps. The auto recenter fired on the all-zero
  pose before tracking started, so the robot cameras sat on the floor: M0 showed the floor view, and M1-M4 reprojected
  from 1.2 m below the eye (M2 radius 39 cm). Fixed (see `ATeleopGameMode`); `Teleop.Math.AutoRecenter` covers it.
- Head translation in VR (2026-11-04, simulator headset moved 15-18 cm sideways, Ideal latency, DepthGT): M3 matches
  GT (holes only behind objects), M1 shifts by t(1/d - 1/R) (background over-shifted at R = 1 m), M0 stays
  head-locked. `Full6DoF` moves the robot cameras with the head (M0 then matches GT); `Rot2DoF` (default, HANDOFF §3)
  does not translate. M4 as specified (sphere fills M3's holes) shows ghost duplicates of foreground objects in
  disocclusion holes. The camera never saw the background there, so the sphere samples the occluder's colour at its
  camera-view position. This is inherent to the sphere fill, not a coordinate bug.
  Test helper: the focus click presses both triggers (left = neck mode), so start one mode early.
- Waist3Neck2 and Template hands (2026-11-04). Desktop PIE, synthetic head offset, camera position in the station
  frame vs the analytic chain: 15 cm right -> waist roll 15.26 deg, camera +15.79 right / -2.11 down (predicted
  15.26 / 15.79 / -2.12); 20 cm forward -> pitch -19.98 deg, camera +18.80 / -3.31, camera stays level; 40 cm right ->
  roll at the 20 deg limit; Rot2DoF / Full6DoF unchanged (0 / 15.00 cm). Simulator (OpenXR input): left trigger cycles
  3 -> 4; grip -> both robot hands grasp 1.00 after L_cmd, release -> 0; right trigger -> right index curl 1.00 and
  point 0, left unchanged; headset 15.3 cm right -> waist roll 15.5 deg, camera 16.1 cm right. Both Template hands
  are visible in the robot frames (M0). The simulator sends no thumb-touch events, so thumb-up is untested.
  Cost: `teleop.Debug.HideRobotHands` A/B, alternating runs: hidden 60.3 / 60.1 fps, visible 60.5 / 58.8 (noise).
  That day the whole PC was slower: the plain Template baseline gave 59.8 fps (GPU 7.8 ms) instead of 72.0, with
  `OVRServer_x64` (Link) and Task Manager running, so compare fps only within one session.
  M3 with the waist (simulator, Ideal, DepthGT, headset 15.3 cm right): best-match shift vs GT 0 px in all regions
  (Waist3Neck2 and Rot2DoF). The disocclusion holes (dark, object-shaped regions beside near objects; users call
  them "shadows") stay with Rot2DoF and shrink as the waist catches up: mean |diff| vs GT beside the white box 31
  (Rot2DoF), 19 (waist, right after the move), 6.6 (waist, settled). The rolled camera (sideways lean) shows as a
  tilted frame edge; the content stays level. M3 renders the sky as a hole (dark), so auto exposure makes M3 a bit
  brighter than GT (flat wall: mean |diff| ~4 at 0 px shift).
- Observer view (2026-11-04, simulator, Bad latency): split screen applied at start, V cycles the presets; moving
  the headset puts the green operator frustum ahead of the orange (robot now) and yellow (shown frame) ones, moving a
  controller puts the green ghost hand ahead of the robot hand. Cost: alternating runs on 60.2 / 59.3 fps, off 63.2 /
  64.6 (render thread bound that day). Use `teleop.Observer 0` (or a lower `teleop.Observer.Hz`) for fps-critical runs.
  Test scripts that need both eyes on the spectator screen must pass `teleop.Observer 0` with `vr.SpectatorScreenMode 2`.
- Robot camera marker (2026-11-04, desktop PIE, Rot2DoF, recentered, then head 30 cm back / 8 cm right): point 30 cm
  ahead / 8 cm left with frustum and up tick, in M0, M3 (with the disocclusion holes of that offset) and GT. Enhanced
  Input injection (`EnhancedInputLocalPlayerSubsystem.inject_input_vector_for_action` on the pawn's `IA_*` actions,
  found with `unreal.find_object`): C and right stick flicks up / down toggle it; in the blind test the flick goes to the
  rating. Desktop test gotchas: the auto recenter fires ~0.5 s after PIE start, so set head offsets after it; and
  `teleop.Recenter` in the same script as an offset change still sees the old pose (poses are recorded on the next tick).
- Start in GT and the A cycle GT > M1 > M2 > M3 > M4 > M0 > GT (2026-11-04, desktop PIE, injected A presses).
  M2 window (desktop PIE, Ideal, DepthGT, display.csv `sphere_r_cm`): head yaw -30 -> 0 deg moves the centre from
  the wall (R 509 cm) through the doorway (1440 cm). `teleop.M2.WindowSec 2`: R holds 509 for 1.25 s, 660 at 1.5 s,
  1413 at 2 s; `WindowSec 0` (old behaviour): 616 at 0.2 s, 1326 at 0.5 s. Injected presses closer than a frame apart
  merge into one (space them ~0.5 s).
- How to measure: `teleop.Debug.QuitAfterSec N` exits cleanly; the module logs `[fps]` every 2 s (any game mode);
  the presenter logs `[stats]` (frame age + fps). Run-to-run spread on the simulator is +-3 fps; if a run hangs at
  frame 0, restart `MetaXRSimulator.exe`. Make sure `synth_env_server.exe` is not running (see the XR runtime section). Plain Template baseline:
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
- Simulated controllers from a script: the simulator's default key bindings (embedded in `SIMULATOR.dll`) are
  W/S/A/D/R/F move the selected device (~1 m/s at movement speed 1.0), Y/H/G/J thumbstick, I stick click, B = A/X,
  N = B/Y, T trigger, U grip, M menu, `]` cycles the selected device (both controllers -> headset -> left -> right).
  With "Use same input for both sides" off, keys go to the selected device only. Keys only arrive when the viewport has
  the simulator's internal keyboard focus (click it once; that click also presses the trigger). Posting WM_KEYDOWN /
  WM_KEYUP to the window's `Microsoft.UI.Content.DesktopChildSiteBridge` child works without foreground focus, so a
  test does not type into other windows. UI Automation reads the panel (device poses under "Input positioning"), but
  each query takes ~50 s.
- On every controller pose query, OculusXRInput asks OVRPlugin for the "detached controller" interaction profile.
  The simulator lacks `XR_META_detached_controllers`, so each call fails with `XR_ERROR_PATH_INVALID` and OVRPlugin
  logs it (~290 errors/s, ~9 MB of log per minute). `TeleopOvrpLogFilter` (module ticker) replaces OVRPlugin's log
  callback and drops repeats of that one message (summary once a minute); `-NoOvrpLogFilter` disables it. The Meta
  plugin logs through `GLog->CategorizedLogf`, so `log LogOVRPlugin off` does not work. A/B: no frame-rate effect
  (70.5 vs 70.1 fps); only log size (0.2 vs 12.5 MB per 80 s).
- The simulator starts `synth_env_server.exe` (synthetic environment for passthrough, "Bedroom") with its frontend.
  This app does not use passthrough, and the server costs ~4.5 ms GPU: 48 fps with it vs 70.5 without. Stop it before
  measuring (`Stop-Process -Name synth_env_server`); it is not restarted until the simulator frontend restarts.
  **It also leaks memory**: left running after a simulator test (2026-11-04), it reached 29 GB committed in 1.5 h, the
  PC had 0.7 GB RAM free, a later VR Preview on the Quest ran at 1-2 fps (GPU 240-660 ms per frame) and went black
  (EndFrame4 -1006) with a memory-pressure warning. After every simulator test, stop both `synth_env_server` and
  `MetaXRSimulator`.

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
