#include "TeleopCVars.h"

namespace TeleopCVars
{
	// --- Presentation -------------------------------------------------------------------------------------

	TAutoConsoleVariable<int32> Method(
		TEXT("teleop.Method"), 5,
		TEXT("Presentation method (HANDOFF §6): 0 = M0 naive head-locked, 1 = M1 sphere (fixed R), 2 = M2 sphere (adaptive R),\n")
		TEXT("3 = M3 depth mesh, 4 = M4 hybrid (sphere + mesh), 5 = GT. Starts in GT; A cycles GT > M1 > M2 > M3 > M4 > M0 > GT\n")
		TEXT("(user, 2026-11-04: starting in the head-locked M0 was unpleasant)."),
		ECVF_Default);

	TAutoConsoleVariable<float> SphereRadiusCm(
		TEXT("teleop.SphereRadiusCm"), 100.f,
		TEXT("M1 sphere radius R, cm (HANDOFF §6: default 1 m). Left stick Y adjusts."),
		ECVF_Default);

	TAutoConsoleVariable<float> M2WindowSec(
		TEXT("teleop.M2.WindowSec"), 2.f,
		TEXT("M2: the radius target is the median of the per-frame depth medians of the last this many seconds, s\n")
		TEXT("(user, 2026-11-04: HANDOFF's 150 ms smoothing alone followed every glance; 2 s keeps R on what you look at)."),
		ECVF_Default);

	TAutoConsoleVariable<float> M2SmoothSec(
		TEXT("teleop.M2.SmoothSec"), 0.15f,
		TEXT("M2: R eases toward the window median with this time constant, s (removes the step when the majority of the\n")
		TEXT("window changes depth)."),
		ECVF_Default);

	TAutoConsoleVariable<float> M2WindowFrac(
		TEXT("teleop.M2.WindowFrac"), 0.1f,
		TEXT("M2: size of the central depth window (fraction of the image) whose valid-depth median sets R."),
		ECVF_Default);

	TAutoConsoleVariable<int32> M4Sphere(
		TEXT("teleop.M4.Sphere"), 2,
		TEXT("M4 background sphere: 1 = M1 (fixed R), 2 = M2 (adaptive R)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> M4Fill(
		TEXT("teleop.M4.Fill"), 0,
		TEXT("M4: what fills the M3 mesh's holes. 0 = sphere (HANDOFF §6; paints copies of near objects into disocclusions),\n")
		TEXT("1 = nearest background pixel (walk from the sphere's sample to the background beyond the occluder's edge),\n")
		TEXT("2 = memory of earlier frames (keyframe meshes), fill 1 where memory has nothing."),
		ECVF_Default);

	TAutoConsoleVariable<float> FillSearchPx(
		TEXT("teleop.Fill.SearchPx"), 128.f,
		TEXT("M4 fill 1: how far to walk for the background, robot camera pixels (a hole is at most ~t*f*(1/d_near - 1/d_far) wide)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> MemoryKeyframes(
		TEXT("teleop.Memory.Keyframes"), 16, TEXT("M4 fill 2: number of keyframes kept (1..32; changing it clears the memory)."), ECVF_Default);

	TAutoConsoleVariable<float> MemoryMinMoveCm(
		TEXT("teleop.Memory.MinMoveCm"), 3.f,
		TEXT("M4 fill 2: a frame becomes a keyframe if the robot camera moved more than this (or turned more than MinTurnDeg)\n")
		TEXT("since every stored keyframe, cm."),
		ECVF_Default);

	TAutoConsoleVariable<float> MemoryMinTurnDeg(
		TEXT("teleop.Memory.MinTurnDeg"), 5.f, TEXT("M4 fill 2: see teleop.Memory.MinMoveCm, degrees."), ECVF_Default);

	TAutoConsoleVariable<float> MemoryBandPx(
		TEXT("teleop.Memory.BandPx"), 64.f,
		TEXT("M4 fill 2: a keyframe keeps only depth on the background side of depth discontinuities, within this many robot\n")
		TEXT("camera pixels (where holes open; user idea 2026-11-04). 0 = keep the whole frame. Measured (ZEDlike, desktop):\n")
		TEXT("32 / 64 px keep 18 / 32 % of the texels with the same hole error vs GT as whole frames."),
		ECVF_Default);

	TAutoConsoleVariable<int32> MemoryGridStride(
		TEXT("teleop.Memory.GridStride"), 2, TEXT("M4 fill 2: one keyframe-mesh vertex per this many depth texels (1..8)."), ECVF_Default);

	TAutoConsoleVariable<float> MemoryPush(
		TEXT("teleop.Memory.Push"), 0.01f,
		TEXT("M4 fill 2: keyframe meshes are pushed back along the eye ray by this factor x (1 + age rank), so the current\n")
		TEXT("mesh and then newer keyframes win the depth test."),
		ECVF_Default);

	TAutoConsoleVariable<float> M3EdgeDispPx(
		TEXT("teleop.M3.EdgeDispPx"), 3.f,
		TEXT("M3: disparity jump (px, reference f * b of a 64 mm camera pair) between neighbor vertices above which\n")
		TEXT("triangles are discarded (rubber sheet). Disparity noise is depth independent, so one threshold fits all depths."),
		ECVF_Default);

	TAutoConsoleVariable<int32> EyeTest(
		TEXT("teleop.EyeTest"), 0,
		TEXT("Per-eye check (HANDOFF S1). 0 = off, 1 = post process left red / right green (pixel stage),\n")
		TEXT("2 = panel colored in the vertex shader (vertex stage)."),
		ECVF_Default);

	// --- Latency ------------------------------------------------------------------------------------------

	TAutoConsoleVariable<int32> LatencyPreset(
		TEXT("teleop.LatencyPreset"), 2,
		TEXT("Latency preset (HANDOFF §4): 0 = Ideal, 1 = LAN, 2 = WiFi, 3 = Remote, 4 = Bad, 5 = Constant (teleop.LatencyMs, no loss).\n")
		TEXT("B cycles 0-4."),
		ECVF_Default);

	TAutoConsoleVariable<float> LatencyBaseOffsetMs(
		TEXT("teleop.LatencyBaseOffsetMs"), 0.f,
		TEXT("Added to both network base latencies (good and bad state), ms. Right stick X adjusts in 10 ms steps."),
		ECVF_Default);

	TAutoConsoleVariable<int32> JitterBuffer(
		TEXT("teleop.JitterBuffer"), 0,
		TEXT("1 = jitter buffer: frame i is shown at max(A_i, C_i + D_target), D_target = recent p95 latency (HANDOFF §4).\n")
		TEXT("Y toggles."),
		ECVF_Default);

	TAutoConsoleVariable<float> LatencyMs(
		TEXT("teleop.LatencyMs"), 100.f,
		TEXT("End-to-end latency for teleop.LatencyPreset 5 (Constant), ms."),
		ECVF_Default);

	TAutoConsoleVariable<float> CmdLatencyMs(
		TEXT("teleop.CmdLatencyMs"), 10.f,
		TEXT("Command channel base latency (operator poses and grab commands -> robot), ms (HANDOFF §4 L_cmd).\n")
		TEXT("Presets 0-4 add correlated jitter on top; preset 5 uses it as is."),
		ECVF_Default);

	// --- Depth --------------------------------------------------------------------------------------------

	TAutoConsoleVariable<int32> DepthPreset(
		TEXT("teleop.DepthPreset"), 1,
		TEXT("Depth preset (HANDOFF §5): 0 = DepthGT, 1 = ZEDlike, 2 = MobileStereo, 3 = Sparse. Left stick click cycles."),
		ECVF_Default);

	// --- Robot --------------------------------------------------------------------------------------------

	TAutoConsoleVariable<float> CamHz(
		TEXT("teleop.CamHz"), 60.f,
		TEXT("Robot camera frame rate f_cam, Hz (HANDOFF §3: 30, 60, 72). Independent of the HMD refresh rate."),
		ECVF_Default);

	TAutoConsoleVariable<float> BaselineMm(
		TEXT("teleop.BaselineMm"), 64.f,
		TEXT("Robot stereo baseline b, mm (HANDOFF §3: 40-90). Left stick X adjusts in 5 mm steps."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CamMountOffset(
		TEXT("teleop.CamMountOffset"), 1,
		TEXT("1 = cameras mounted 10 cm in front of and 5 cm above the neck pivot (HANDOFF §3), 0 = at the pivot."),
		ECVF_Default);

	TAutoConsoleVariable<int32> NeckMode(
		TEXT("teleop.NeckMode"), 4,
		TEXT("Robot neck (HANDOFF §3): 0 = Fixed, 1 = Rot2DoF (yaw+pitch), 2 = Rot3DoF, 3 = Full6DoF, ")
		TEXT("4 = Waist3Neck2 (default: 3-DoF waist leaning to follow head translation + 2-DoF neck, as on Sharpa)."),
		ECVF_Default);

	TAutoConsoleVariable<float> WaistYawShare(
		TEXT("teleop.Waist.YawShare"), 0.f,
		TEXT("Waist3Neck2: share of the head yaw the waist takes (0..1); the neck turns the rest. 0 = torso faces forward."),
		ECVF_Default);

	// --- Study tools --------------------------------------------------------------------------------------

	TAutoConsoleVariable<int32> Log(
		TEXT("teleop.Log"), 1,
		TEXT("1 = write per-frame CSV logs (capture.csv, display.csv) to Saved/TeleopLogs/<session>/ (HANDOFF §7.8)."),
		ECVF_Default);

	TAutoConsoleVariable<float> BlindConditionSec(
		TEXT("teleop.Blind.ConditionSec"), 30.f,
		TEXT("Blind test: viewing time per condition before rating, s (HANDOFF §8)."),
		ECVF_Default);

	// --- Desktop test helpers -----------------------------------------------------------------------------

	TAutoConsoleVariable<float> DebugHeadYawAmpDeg(
		TEXT("teleop.Debug.HeadYawAmpDeg"), 0.f,
		TEXT("Desktop only (no HMD): sinusoidal synthetic head yaw amplitude, degrees. 0 = off."),
		ECVF_Default);

	TAutoConsoleVariable<float> DebugHeadYawHz(
		TEXT("teleop.Debug.HeadYawHz"), 0.25f,
		TEXT("Desktop only: frequency of the synthetic head yaw, Hz."),
		ECVF_Default);

	TAutoConsoleVariable<float> DebugHeadYawDeg(
		TEXT("teleop.Debug.HeadYawDeg"), 0.f, TEXT("Desktop only: static head yaw added to the sweep, degrees."), ECVF_Default);

	TAutoConsoleVariable<float> DebugHeadPitchDeg(
		TEXT("teleop.Debug.HeadPitchDeg"), 0.f, TEXT("Desktop only: static head pitch, degrees (+ = up)."), ECVF_Default);

	TAutoConsoleVariable<int32> DebugDesktopEye(
		TEXT("teleop.Debug.DesktopEye"), 1,
		TEXT("Desktop only: where the single desktop view renders from. 1 = the left eye (head - baseline/2 to the right), like an\n")
		TEXT("HMD's left eye: a mono view uses the left robot frame (eye index 0), so rendering from the head centre put the eye\n")
		TEXT("b/2 = 3.2 cm beside its camera in every desktop test (found 2026-11-04). 0 = the head centre (old behaviour)."),
		ECVF_Default);

	TAutoConsoleVariable<float> DebugHeadOffsetX(
		TEXT("teleop.Debug.HeadOffsetX"), 0.f, TEXT("Desktop only: head translation in the tracking frame, X (forward), cm."), ECVF_Default);
	TAutoConsoleVariable<float> DebugHeadOffsetY(
		TEXT("teleop.Debug.HeadOffsetY"), 0.f, TEXT("Desktop only: head translation in the tracking frame, Y (right), cm."), ECVF_Default);
	TAutoConsoleVariable<float> DebugHeadOffsetZ(
		TEXT("teleop.Debug.HeadOffsetZ"), 0.f, TEXT("Desktop only: head translation in the tracking frame, Z (up), cm."), ECVF_Default);

	// --- Performance / automation -------------------------------------------------------------------------

	TAutoConsoleVariable<float> DebugProfileGPUAfterSec(
		TEXT("teleop.Debug.ProfileGPUAfterSec"), 0.f,
		TEXT("If > 0: run ProfileGPU once, this many seconds after the session starts (dumps per-pass GPU timings to the log)."),
		ECVF_Default);

	// --- Observer view (PC window) ---------------------------------------------------------------------------
	TAutoConsoleVariable<int32> Observer(
		TEXT("teleop.Observer"), 1,
		TEXT("Third-person observer view on the PC window in VR (the HMD is unchanged): 1 = split screen, observer left,\n")
		TEXT("operator eye view right; 2 = observer only; 0 = off (the spectator screen is left alone, e.g. vr.SpectatorScreenMode)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> ObserverView(
		TEXT("teleop.Observer.View"), 0,
		TEXT("Observer camera preset: 0 = behind-right 45 deg (default), 1 = behind-left 45 deg, 2 = front-right over the desk,\n")
		TEXT("3 = right side, 4 = top. Key V cycles."),
		ECVF_Default);

	TAutoConsoleVariable<float> ObserverHz(
		TEXT("teleop.Observer.Hz"), 30.f, TEXT("Observer view capture rate, Hz."), ECVF_Default);

	TAutoConsoleVariable<float> ObserverExposure(
		TEXT("teleop.Observer.Exposure"), 3.5f, TEXT("Observer view: fixed (manual) exposure compensation, EV."), ECVF_Default);

	TAutoConsoleVariable<float> ObserverSplit(
		TEXT("teleop.Observer.Split"), 0.5f, TEXT("Split screen: share of the PC window width for the observer (0.2..0.8)."), ECVF_Default);

	TAutoConsoleVariable<int32> DebugHideRobotHands(
		TEXT("teleop.Debug.HideRobotHands"), 0,
		TEXT("1 = hide the robot hand meshes and stop their animation (performance A/B). Grabbing still works."),
		ECVF_Default);

	TAutoConsoleVariable<int32> DebugShowRobotCamera(
		TEXT("teleop.Debug.ShowRobotCamera"), 0,
		TEXT("1 = show the robot camera's real pose now in the operator view (HMD): orange point + view frustum, overlaid on\n")
		TEXT("every method. Right stick up/down (outside the blind test) or key C toggles."),
		ECVF_Default);

	TAutoConsoleVariable<float> DebugLogHandsSec(
		TEXT("teleop.Debug.LogHandsSec"), 0.f,
		TEXT("If > 0: every this many seconds, log the operator head pose ([head]), each remote hand's position, fingers and\n")
		TEXT("the nearest grabbable within 1 m ([hand]; used to steer simulated controllers in tests), and the robot's waist,\n")
		TEXT("neck and camera position ([robot]). 0 = off."),
		ECVF_Default);

	// --- Evaluation (teleop_eval.py) ------------------------------------------------------------------------

	TAutoConsoleVariable<int32> DebugFreeze(
		TEXT("teleop.Debug.Freeze"), 0,
		TEXT("1 = freeze the remote side for image comparisons: the props stop, the robot neck / waist and its cameras stop, the\n")
		TEXT("presenter keeps the shown frame, M2's radius and the keyframe memory stop updating. The operator's head still moves,\n")
		TEXT("so every method (and GT) can be rendered for the same frozen world, frame and eye pose."),
		ECVF_Default);

	TAutoConsoleVariable<int32> DebugHideHud(
		TEXT("teleop.Debug.HideHud"), 0, TEXT("1 = hide the operator HUD text (screenshots for metrics)."), ECVF_Default);

	TAutoConsoleVariable<int32> DebugMaskView(
		TEXT("teleop.Debug.MaskView"), 0,
		TEXT("Evaluation, M4 only: 1 = show a class map instead of the image: green = the depth mesh covers the pixel, red = a hole\n")
		TEXT("inside the shown frame, blue = outside the shown frame. With teleop.Debug.MeshGTDepth 1, red = disocclusion."),
		ECVF_Default);

	TAutoConsoleVariable<int32> DebugMeshGTDepth(
		TEXT("teleop.Debug.MeshGTDepth"), 0,
		TEXT("Evaluation: 1 = the M3 / M4 mesh uses the capture's exact depth (frame alpha) instead of the degraded depth. Ground\n")
		TEXT("truth, for the disocclusion mask only (HANDOFF §6.1: methods may not use it)."),
		ECVF_Default);

	TAutoConsoleVariable<float> DebugQuitAfterSec(
		TEXT("teleop.Debug.QuitAfterSec"), 0.f,
		TEXT("Test automation: exit the process cleanly this many seconds after startup (flushes logs and traces). 0 = off."),
		ECVF_Default);
}
