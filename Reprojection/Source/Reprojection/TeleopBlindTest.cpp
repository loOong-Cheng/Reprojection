#include "TeleopBlindTest.h"

#include "Reprojection.h"
#include "TeleopCVars.h"
#include "TeleopDepthDegrader.h"
#include "TeleopLatencyModel.h"
#include "TeleopLogger.h"

#include "Math/RandomStream.h"

namespace
{
	const TCHAR* GQuestions[3] = { TEXT("Stability"), TEXT("Comfort"), TEXT("Depth credibility") };

	const TCHAR* MethodCode(ETeleopMethod M)
	{
		switch (M)
		{
		case ETeleopMethod::M0_Naive: return TEXT("M0");
		case ETeleopMethod::M1_SphereFixed: return TEXT("M1");
		case ETeleopMethod::M2_SphereAdaptive: return TEXT("M2");
		case ETeleopMethod::M3_DepthMesh: return TEXT("M3");
		case ETeleopMethod::M4_Hybrid: return TEXT("M4");
		default: return TEXT("GT");
		}
	}
}

void FTeleopBlindTest::Start(double Now, FTeleopLogger* InLogger)
{
	Logger = InLogger;
	Conditions = { ETeleopMethod::M0_Naive, ETeleopMethod::M1_SphereFixed, ETeleopMethod::M2_SphereAdaptive,
		ETeleopMethod::M3_DepthMesh, ETeleopMethod::M4_Hybrid };
	FRandomStream Rng(int32(FDateTime::Now().GetTicks() & 0x7fffffff));
	for (int32 i = Conditions.Num() - 1; i > 0; --i)
	{
		Conditions.Swap(i, Rng.RandRange(0, i));
	}
	Index = 0;
	Phase = EPhase::Viewing;
	ViewStart = Now;
	UE_LOG(LogTeleop, Log, TEXT("Blind test started (%d conditions)"), Conditions.Num());
}

void FTeleopBlindTest::Stop()
{
	if (IsActive())
	{
		UE_LOG(LogTeleop, Log, TEXT("Blind test stopped at condition %d/%d"), Index + 1, Conditions.Num());
	}
	Phase = EPhase::Idle;
}

void FTeleopBlindTest::Tick(double Now)
{
	if (Phase == EPhase::Viewing && Now - ViewStart >= TeleopCVars::BlindConditionSec.GetValueOnGameThread())
	{
		Confirm(Now);
	}
}

void FTeleopBlindTest::AdjustRating(int32 Delta)
{
	if (Phase == EPhase::Rating)
	{
		Value = FMath::Clamp(Value + Delta, 1, 7);
	}
}

void FTeleopBlindTest::Confirm(double Now)
{
	if (Phase == EPhase::Viewing)
	{
		Phase = EPhase::Rating;
		ViewEnd = Now;
		Question = 0;
		Value = 4;
		return;
	}
	if (Phase != EPhase::Rating)
	{
		return;
	}
	Ratings[Question] = Value;
	if (++Question < 3)
	{
		Value = 4;
		return;
	}
	WriteResult(Now);
	if (++Index < Conditions.Num())
	{
		Phase = EPhase::Viewing;
		ViewStart = Now;
	}
	else
	{
		Phase = EPhase::Done;
		UE_LOG(LogTeleop, Log, TEXT("Blind test finished"));
	}
}

ETeleopMethod FTeleopBlindTest::GetMethod() const
{
	return Conditions.IsValidIndex(Index) ? Conditions[Index] : ETeleopMethod::M0_Naive;
}

FString FTeleopBlindTest::GetConditionCode() const
{
	return FString::Printf(TEXT("C%d"), Index + 1);
}

FString FTeleopBlindTest::GetHudText(double Now) const
{
	switch (Phase)
	{
	case EPhase::Viewing:
		return FString::Printf(TEXT("Blind %s/%d   %.0f s left   (A: rate now)"), *GetConditionCode(), Conditions.Num(),
			FMath::Max(0.0, TeleopCVars::BlindConditionSec.GetValueOnGameThread() - (Now - ViewStart)));
	case EPhase::Rating:
	{
		FString Scale;
		for (int32 v = 1; v <= 7; ++v)
		{
			Scale += v == Value ? FString::Printf(TEXT("[%d]"), v) : FString::Printf(TEXT(" %d "), v);
		}
		return FString::Printf(TEXT("Blind %s/%d   %s (%d/3)\n%s   stick up/down, A = ok"),
			*GetConditionCode(), Conditions.Num(), GQuestions[Question], Question + 1, *Scale);
	}
	case EPhase::Done:
		return TEXT("Blind test done - thank you (menu: exit)");
	default:
		return FString();
	}
}

void FTeleopBlindTest::WriteResult(double Now)
{
	const FString Line = FString::Printf(TEXT("%d,%s,%s,%s,%.1f,%d,%s,%d,%d,%d,%.6f,%.6f,%.6f"),
		Index + 1, *GetConditionCode(), MethodCode(GetMethod()),
		*FTeleopLatencyModel::GetPresetName(TeleopCVars::LatencyPreset.GetValueOnGameThread()),
		TeleopCVars::LatencyBaseOffsetMs.GetValueOnGameThread(), TeleopCVars::JitterBuffer.GetValueOnGameThread(),
		UTeleopDepthDegrader::GetActivePreset().Name, Ratings[0], Ratings[1], Ratings[2], ViewStart, ViewEnd, Now);
	UE_LOG(LogTeleop, Log, TEXT("[blind] %s"), *Line);
	if (Logger)
	{
		Logger->LogBlind(Line);
	}
}
