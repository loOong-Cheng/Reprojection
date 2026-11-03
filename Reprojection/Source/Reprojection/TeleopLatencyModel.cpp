#include "TeleopLatencyModel.h"

#include "TeleopCVars.h"

FTeleopLatencyModel::FFrameSample FTeleopLatencyModel::SampleFrame()
{
	FFrameSample Sample;
	Sample.LatencySeconds = FMath::Max(0.f, TeleopCVars::LatencyMs.GetValueOnGameThread()) / 1000.0;
	Sample.bDropped = false;
	return Sample;
}

double FTeleopLatencyModel::GetCommandLatencySeconds() const
{
	return FMath::Max(0.f, TeleopCVars::CmdLatencyMs.GetValueOnGameThread()) / 1000.0;
}
