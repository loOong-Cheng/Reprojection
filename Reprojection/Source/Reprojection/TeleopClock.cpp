#include "TeleopClock.h"

#include "CoreGlobals.h"
#include "HAL/PlatformTime.h"

namespace
{
	bool bFixedStep = false;
	uint64 StartFrame = 0;
	double Step = 0.0;
	// Any constant works; a small one keeps full double precision in the frame arithmetic.
	constexpr double FixedBase = 1000.0;
}

double TeleopClock::Now()
{
	return bFixedStep ? FixedBase + double(GFrameCounter - StartFrame) * Step : FPlatformTime::Seconds();
}

void TeleopClock::BeginFixedStep(double StepSeconds)
{
	bFixedStep = StepSeconds > 0.0;
	StartFrame = GFrameCounter;
	Step = StepSeconds;
}

void TeleopClock::EndFixedStep()
{
	bFixedStep = false;
}

bool TeleopClock::IsFixedStep()
{
	return bFixedStep;
}
