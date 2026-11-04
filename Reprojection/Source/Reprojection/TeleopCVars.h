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
	extern TAutoConsoleVariable<float> SphereRadiusCm;
	extern TAutoConsoleVariable<float> M2WindowSec;
	extern TAutoConsoleVariable<float> M2SmoothSec;
	extern TAutoConsoleVariable<float> M2WindowFrac;
	extern TAutoConsoleVariable<int32> M4Sphere;
	extern TAutoConsoleVariable<int32> M4Fill;
	extern TAutoConsoleVariable<float> FillSearchPx;
	extern TAutoConsoleVariable<int32> MemoryKeyframes;
	extern TAutoConsoleVariable<float> MemoryMinMoveCm;
	extern TAutoConsoleVariable<float> MemoryMinTurnDeg;
	extern TAutoConsoleVariable<float> MemoryBandPx;
	extern TAutoConsoleVariable<int32> MemoryGridStride;
	extern TAutoConsoleVariable<float> MemoryPush;
	extern TAutoConsoleVariable<float> M3EdgeDispPx;
	extern TAutoConsoleVariable<int32> EyeTest;

	// Latency (HANDOFF §4).
	extern TAutoConsoleVariable<int32> LatencyPreset;
	extern TAutoConsoleVariable<float> LatencyBaseOffsetMs;
	extern TAutoConsoleVariable<int32> JitterBuffer;
	extern TAutoConsoleVariable<float> LatencyMs;
	extern TAutoConsoleVariable<float> CmdLatencyMs;

	// Depth (HANDOFF §5).
	extern TAutoConsoleVariable<int32> DepthPreset;

	// Robot cameras and neck (HANDOFF §3).
	extern TAutoConsoleVariable<float> CamHz;
	extern TAutoConsoleVariable<float> BaselineMm;
	extern TAutoConsoleVariable<int32> CamMountOffset;
	extern TAutoConsoleVariable<int32> NeckMode;
	extern TAutoConsoleVariable<float> WaistYawShare;

	// Study tools (HANDOFF §7.8, §8).
	extern TAutoConsoleVariable<int32> Log;
	extern TAutoConsoleVariable<float> BlindConditionSec;

	// Desktop test helpers (only when no HMD is active).
	extern TAutoConsoleVariable<float> DebugHeadYawAmpDeg;
	extern TAutoConsoleVariable<float> DebugHeadYawHz;
	extern TAutoConsoleVariable<float> DebugHeadYawDeg;
	extern TAutoConsoleVariable<float> DebugHeadOffsetX;
	extern TAutoConsoleVariable<float> DebugHeadOffsetY;
	extern TAutoConsoleVariable<float> DebugHeadOffsetZ;

	// Performance / automation.
	extern TAutoConsoleVariable<float> DebugProfileGPUAfterSec;
	extern TAutoConsoleVariable<float> DebugLogHandsSec;
	extern TAutoConsoleVariable<int32> DebugHideRobotHands;
	extern TAutoConsoleVariable<int32> DebugShowRobotCamera;
	extern TAutoConsoleVariable<int32> Observer;
	extern TAutoConsoleVariable<int32> ObserverView;
	extern TAutoConsoleVariable<float> ObserverHz;
	extern TAutoConsoleVariable<float> ObserverSplit;
	extern TAutoConsoleVariable<float> ObserverExposure;
	extern TAutoConsoleVariable<float> DebugQuitAfterSec;
}
