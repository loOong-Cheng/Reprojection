#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

/**
 * All runtime tunables, as teleop.* console variables (set in the console, via -ExecCmds, or in
 * DefaultEngine.ini [ConsoleVariables]). Defaults follow HANDOFF.md; section numbers in the help text.
 */
namespace TeleopCVars
{
	// Presentation (HANDOFF §6). Values match ETeleopMethod.
	extern TAutoConsoleVariable<int32> Method;
	extern TAutoConsoleVariable<int32> EyeTest;

	// Latency (HANDOFF §4). S2: constant end-to-end and command latency; the noisy model comes in S4.
	extern TAutoConsoleVariable<float> LatencyMs;
	extern TAutoConsoleVariable<float> CmdLatencyMs;

	// Robot cameras and neck (HANDOFF §3).
	extern TAutoConsoleVariable<float> CamHz;
	extern TAutoConsoleVariable<float> BaselineMm;
	extern TAutoConsoleVariable<int32> CamMountOffset;
	extern TAutoConsoleVariable<int32> NeckMode;

	// Desktop test helpers (only when no HMD is active).
	extern TAutoConsoleVariable<float> DebugHeadYawAmpDeg;
	extern TAutoConsoleVariable<float> DebugHeadYawHz;

	// Performance: run ProfileGPU once, this many seconds after the session starts (per-pass GPU timings in the log).
	extern TAutoConsoleVariable<float> DebugProfileGPUAfterSec;
}
