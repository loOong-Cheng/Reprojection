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

- `Source/Reprojection`: game module. `ATeleopGameMode` (project default game mode) spawns
  `ATeleopOperatorPawn` (HMD + controllers, no locomotion) in `ATeleopOperatorRoom` (closed dark box,
  spawned at (-100000, 0, 0) if the level has none).
- `Source/ReprojectionShaders`: loads at PostConfigInit and maps `Reprojection/Shaders` to the virtual
  path `/Reprojection`. Put HLSL there (`/Reprojection/Teleop/*.ush`) and `#include` it from material
  Custom nodes, so shader logic is plain text in git.
- `Content/Python/teleop_materials.py`: builds the materials in `/Game/Teleop/Materials` from code.
  Rebuild with `import importlib, teleop_materials; importlib.reload(teleop_materials); teleop_materials.build_all()`.
  Change materials there, not by hand in the material editor.
- Debug: `teleop.EyeTest` 0 off / 1 post-process red-green (pixel stage) / 2 vertex-shader panel; right
  controller A or keyboard T cycles it.

## XR runtime: Quest 3 or Meta XR Simulator

- Plugins: Epic `OpenXR` + Meta XR `OculusXR` 1.201.0 (installed in the engine, `Engine\Plugins\Marketplace\MetaXR`,
  not in the repo). Backend `XrApi=NativeOpenXR` (Meta's recommended; the i105 project uses legacy `OVRPluginOpenXR`).
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
- All tunables live in one `UTeleopSettings` DataAsset and are mirrored as console variables.
- Per-eye selection in materials uses `ResolvedView.StereoPassIndex`, which needs Instanced Stereo **off**.

## Git

- Git LFS for `.uasset`/`.umap` and binary media (see `.gitattributes`). `Binaries/`, `Intermediate/`,
  `Saved/`, `DerivedDataCache/` and generated IDE files are ignored.
- Commit email is a repo-local GitHub noreply address (the repo is public).
- Save all assets in the editor before committing.
