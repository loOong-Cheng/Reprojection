#include "TeleopCVars.h"

namespace TeleopCVars
{
	// --- Presentation -------------------------------------------------------------------------------------

	TAutoConsoleVariable<int32> Method(
		TEXT("teleop.Method"), 0,
		TEXT("Presentation method (HANDOFF §6): 0 = M0 naive head-locked, 1 = M1 sphere (fixed R), 2 = M2 sphere (adaptive R),\n")
		TEXT("3 = M3 depth mesh, 4 = M4 hybrid (sphere + mesh), 5 = GT. A / X cycles."),
		ECVF_Default);

	TAutoConsoleVariable<float> SphereRadiusCm(
		TEXT("teleop.SphereRadiusCm"), 100.f,
		TEXT("M1 sphere radius R, cm (HANDOFF §6: default 1 m). Left stick Y adjusts."),
		ECVF_Default);

	TAutoConsoleVariable<float> M2SmoothSec(
		TEXT("teleop.M2.SmoothSec"), 0.15f,
		TEXT("M2: time constant of the adaptive radius smoothing, s (HANDOFF §6: ~150 ms)."),
		ECVF_Default);

	TAutoConsoleVariable<float> M2WindowFrac(
		TEXT("teleop.M2.WindowFrac"), 0.1f,
		TEXT("M2: size of the central depth window (fraction of the image) whose valid-depth median sets R."),
		ECVF_Default);

	TAutoConsoleVariable<int32> M4Sphere(
		TEXT("teleop.M4.Sphere"), 2,
		TEXT("M4 background sphere: 1 = M1 (fixed R), 2 = M2 (adaptive R)."),
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
		TEXT("B / Y cycles 0-4."),
		ECVF_Default);

	TAutoConsoleVariable<float> LatencyBaseOffsetMs(
		TEXT("teleop.LatencyBaseOffsetMs"), 0.f,
		TEXT("Added to both network base latencies (good and bad state), ms. Right stick X adjusts in 10 ms steps."),
		ECVF_Default);

	TAutoConsoleVariable<int32> JitterBuffer(
		TEXT("teleop.JitterBuffer"), 0,
		TEXT("1 = jitter buffer: frame i is shown at max(A_i, C_i + D_target), D_target = recent p95 latency (HANDOFF §4).\n")
		TEXT("Right trigger toggles."),
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
		TEXT("teleop.NeckMode"), 1,
		TEXT("Robot neck (HANDOFF §3): 0 = Fixed, 1 = Rot2DoF (yaw+pitch, default), 2 = Rot3DoF, 3 = Full6DoF."),
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

	TAutoConsoleVariable<float> DebugQuitAfterSec(
		TEXT("teleop.Debug.QuitAfterSec"), 0.f,
		TEXT("Test automation: exit the process cleanly this many seconds after startup (flushes logs and traces). 0 = off."),
		ECVF_Default);
}
