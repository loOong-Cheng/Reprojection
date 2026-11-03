#pragma once

#include "CoreMinimal.h"

/**
 * Per-frame end-to-end latency and frame loss (HANDOFF §4), plus the command-channel latency.
 * S2: constant values from teleop.LatencyMs / teleop.CmdLatencyMs, no loss.
 * S4 replaces the internals with the Gilbert-Elliott + AR(1) model and presets; callers stay the same.
 */
class FTeleopLatencyModel
{
public:
	struct FFrameSample
	{
		double LatencySeconds = 0.0;
		bool bDropped = false;
	};

	/** Called once per captured camera frame. */
	FFrameSample SampleFrame();

	/** Command latency (operator -> robot), seconds. */
	double GetCommandLatencySeconds() const;
};
