#pragma once

#include "CoreMinimal.h"

/**
 * Simulation time, seconds: everything that models the robot side (latency, captures, robot motion, frame selection,
 * logs) reads this instead of FPlatformTime::Seconds() directly.
 *
 * Normally it is the wall clock. In a fixed-step replay (FTeleopReplay, engine started with -UseFixedTimeStep -FPS=N)
 * it is Base + (frames since BeginFixedStep) x step, so every run of the same trajectory sees bit-identical times:
 * the same camera captures, latency draws and robot motion for every method, frame by frame.
 */
namespace TeleopClock
{
	REPROJECTION_API double Now();

	/** From the next frame on, time advances by StepSeconds per engine frame (call once at session start). */
	REPROJECTION_API void BeginFixedStep(double StepSeconds);
	REPROJECTION_API void EndFixedStep();
	REPROJECTION_API bool IsFixedStep();
}
