#include "TeleopCVars.h"

namespace TeleopCVars
{
	TAutoConsoleVariable<int32> Method(
		TEXT("teleop.Method"), 0,
		TEXT("Presentation method (HANDOFF §6): 0 = M0 naive head-locked, 5 = GT (remote world rendered at the current eye pose).\n")
		TEXT("M1-M4 (1-4) are not implemented yet and fall back to M0. Right A / left X cycles."),
		ECVF_Default);

	TAutoConsoleVariable<int32> EyeTest(
		TEXT("teleop.EyeTest"), 0,
		TEXT("Per-eye check (HANDOFF S1). 0 = off, 1 = post process left red / right green (pixel stage),\n")
		TEXT("2 = panel colored in the vertex shader (vertex stage)."),
		ECVF_Default);

	TAutoConsoleVariable<float> LatencyMs(
		TEXT("teleop.LatencyMs"), 100.f,
		TEXT("End-to-end (glass-to-glass) latency of robot camera frames, ms. Constant in S2; S4 adds the noisy model."),
		ECVF_Default);

	TAutoConsoleVariable<float> CmdLatencyMs(
		TEXT("teleop.CmdLatencyMs"), 10.f,
		TEXT("Command latency (operator pose and grab commands -> robot), ms (HANDOFF §4 L_cmd)."),
		ECVF_Default);

	TAutoConsoleVariable<float> CamHz(
		TEXT("teleop.CamHz"), 60.f,
		TEXT("Robot camera frame rate f_cam, Hz (HANDOFF §3: 30, 60, 72). Independent of the HMD refresh rate."),
		ECVF_Default);

	TAutoConsoleVariable<float> BaselineMm(
		TEXT("teleop.BaselineMm"), 64.f,
		TEXT("Robot stereo baseline b, mm (HANDOFF §3: 40-90)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CamMountOffset(
		TEXT("teleop.CamMountOffset"), 1,
		TEXT("1 = cameras mounted 10 cm in front of and 5 cm above the neck pivot (HANDOFF §3), 0 = at the pivot."),
		ECVF_Default);

	TAutoConsoleVariable<int32> NeckMode(
		TEXT("teleop.NeckMode"), 1,
		TEXT("Robot neck (HANDOFF §3): 0 = Fixed, 1 = Rot2DoF (yaw+pitch, default), 2 = Rot3DoF, 3 = Full6DoF."),
		ECVF_Default);

	TAutoConsoleVariable<float> DebugHeadYawAmpDeg(
		TEXT("teleop.Debug.HeadYawAmpDeg"), 0.f,
		TEXT("Desktop only (no HMD): sinusoidal synthetic head yaw amplitude, degrees. 0 = off."),
		ECVF_Default);

	TAutoConsoleVariable<float> DebugHeadYawHz(
		TEXT("teleop.Debug.HeadYawHz"), 0.25f,
		TEXT("Desktop only: frequency of the synthetic head yaw, Hz."),
		ECVF_Default);

	TAutoConsoleVariable<float> DebugProfileGPUAfterSec(
		TEXT("teleop.Debug.ProfileGPUAfterSec"), 0.f,
		TEXT("If > 0: run ProfileGPU once, this many seconds after the session starts (dumps per-pass GPU timings to the log)."),
		ECVF_Default);
}
