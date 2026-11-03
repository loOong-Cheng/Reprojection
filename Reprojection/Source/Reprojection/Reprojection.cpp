// Fill out your copyright notice in the Description page of Project Settings.

#include "Reprojection.h"

#include "TeleopCVars.h"

#include "Containers/Ticker.h"
#include "CoreGlobals.h"
#include "DynamicRHI.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogTeleop);

/**
 * Primary game module. Besides the default behavior it runs a core ticker for test automation:
 * teleop.Debug.QuitAfterSec exits the process cleanly (so traces/logs are flushed), independent of the game mode.
 */
class FReprojectionModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		StartTime = FPlatformTime::Seconds();
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FReprojectionModule::Tick), 0.5f);
	}

	virtual void ShutdownModule() override
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	}

private:
	bool Tick(float)
	{
		// Game-mode independent frame-rate log (also works with the plain Template game mode).
		const double Now = FPlatformTime::Seconds();
		if (Now - FpsWindowStart >= 2.0)
		{
			if (FpsWindowStart > 0.0)
			{
				const double Fps = (GFrameCounter - FpsWindowFrame) / (Now - FpsWindowStart);
				UE_LOG(LogTeleop, Log, TEXT("[fps] %.1f fps, gpu %.2f ms"), Fps, FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
			}
			FpsWindowStart = Now;
			FpsWindowFrame = GFrameCounter;
		}

		const float QuitAfter = TeleopCVars::DebugQuitAfterSec.GetValueOnGameThread();
		if (QuitAfter > 0.f && !bQuitRequested && FPlatformTime::Seconds() - StartTime >= QuitAfter)
		{
			bQuitRequested = true;
			UE_LOG(LogTeleop, Log, TEXT("teleop.Debug.QuitAfterSec=%.0f reached, exiting"), QuitAfter);
			FPlatformMisc::RequestExit(false, TEXT("teleop.Debug.QuitAfterSec"));
		}
		return true;
	}

	double StartTime = 0.0;
	bool bQuitRequested = false;
	double FpsWindowStart = 0.0;
	uint64 FpsWindowFrame = 0;
	FTSTicker::FDelegateHandle TickerHandle;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FReprojectionModule, Reprojection, "Reprojection");
