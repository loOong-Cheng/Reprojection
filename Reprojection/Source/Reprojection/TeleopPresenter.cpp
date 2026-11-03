#include "TeleopPresenter.h"

#include "FrameRingBuffer.h"
#include "Reprojection.h"
#include "RobotHead.h"
#include "TeleopCVars.h"
#include "TeleopGameMode.h"
#include "TeleopOperatorPawn.h"

#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderTimer.h"

namespace
{
	const TCHAR* MethodName(ETeleopMethod Method)
	{
		switch (Method)
		{
		case ETeleopMethod::M0_Naive: return TEXT("M0 naive");
		case ETeleopMethod::M1_SphereFixed: return TEXT("M1 sphere");
		case ETeleopMethod::M2_SphereAdaptive: return TEXT("M2 sphere adaptive");
		case ETeleopMethod::M3_DepthMesh: return TEXT("M3 depth mesh");
		case ETeleopMethod::M4_Hybrid: return TEXT("M4 hybrid");
		case ETeleopMethod::GT: return TEXT("GT");
		}
		return TEXT("?");
	}

	const TCHAR* NeckName(int32 Mode)
	{
		static const TCHAR* Names[] = { TEXT("Fixed"), TEXT("Rot2DoF"), TEXT("Rot3DoF"), TEXT("Full6DoF") };
		return Names[FMath::Clamp(Mode, 0, 3)];
	}

	float Percentile(TArray<float>& Sorted, float P)
	{
		if (Sorted.Num() == 0)
		{
			return 0.f;
		}
		const float Index = P * (Sorted.Num() - 1);
		const int32 Lo = FMath::FloorToInt(Index);
		const int32 Hi = FMath::Min(Lo + 1, Sorted.Num() - 1);
		return FMath::Lerp(Sorted[Lo], Sorted[Hi], Index - Lo);
	}
}

ATeleopPresenter::ATeleopPresenter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	PresentMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Teleop/Materials/M_TeleopPresent.M_TeleopPresent")));
}

void ATeleopPresenter::BeginPlay()
{
	Super::BeginPlay();

	if (UMaterialInterface* Base = PresentMaterial.LoadSynchronous())
	{
		PresentMID = UMaterialInstanceDynamic::Create(Base, this);
	}
	else
	{
		UE_LOG(LogTeleop, Error, TEXT("Presenter: material not found: %s"), *PresentMaterial.ToString());
	}
}

void ATeleopPresenter::Init(ATeleopGameMode* InGameMode, ATeleopOperatorPawn* InPawn, ARobotHead* InRobotHead)
{
	GameMode = InGameMode;
	Pawn = InPawn;
	RobotHead = InRobotHead;
	if (InRobotHead)
	{
		AddTickPrerequisiteActor(InRobotHead); // see frames captured this tick (zero-latency case)
	}
}

void ATeleopPresenter::ApplyMethod(ETeleopMethod Method)
{
	ATeleopOperatorPawn* P = Pawn.Get();
	ATeleopGameMode* GM = GameMode.Get();
	if (!P || !GM)
	{
		return;
	}
	const bool bGT = Method == ETeleopMethod::GT;
	P->SetRemoteView(bGT, GM->GetLocalToRemote());
	P->SetCameraBlendable(PresentMID, !bGT);
	ActiveMethod = Method;
	bMethodApplied = true;
	UE_LOG(LogTeleop, Log, TEXT("Method: %s"), MethodName(Method));
}

void ATeleopPresenter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ATeleopOperatorPawn* P = Pawn.Get();
	ARobotHead* Head = RobotHead.Get();
	ATeleopGameMode* GM = GameMode.Get();
	if (!P || !Head || !GM || !Head->GetBuffer())
	{
		return;
	}

	int32 Requested = TeleopCVars::Method.GetValueOnGameThread();
	ETeleopMethod Method = Requested == static_cast<int32>(ETeleopMethod::GT) ? ETeleopMethod::GT : ETeleopMethod::M0_Naive; // M1-M4: not yet
	if (!bMethodApplied || Method != ActiveMethod)
	{
		ApplyMethod(Method);
	}
	else if (Method == ETeleopMethod::GT)
	{
		P->SetRemoteView(true, GM->GetLocalToRemote()); // follow recentering
	}

	const double Now = FPlatformTime::Seconds();
	UFrameRingBuffer* Buffer = Head->GetBuffer();

	// Newest arrived frame; never go back to an older one (out-of-order arrival drops the older frame).
	const int32 Candidate = Buffer->FindNewestArrived(Now);
	if (Candidate != INDEX_NONE && Buffer->GetMeta(Candidate).FrameId > ShownFrameId)
	{
		const FTeleopFrameMeta& Meta = Buffer->GetMeta(Candidate);
		ShownSlot = Candidate;
		ShownFrameId = Meta.FrameId;
		ShownCaptureTime = Meta.CaptureTime;
		++WindowNewFrames;

		if (PresentMID)
		{
			PresentMID->SetTextureParameterValue(TEXT("LeftFrame"), Buffer->GetTexture(Candidate, 0));
			PresentMID->SetTextureParameterValue(TEXT("RightFrame"), Buffer->GetTexture(Candidate, 1));
			const float TanX = FMath::Tan(FMath::DegreesToRadians(Meta.HorizontalFovDeg * 0.5f));
			const float TanY = TanX * Meta.Height / float(FMath::Max(Meta.Width, 1));
			PresentMID->SetVectorParameterValue(TEXT("TanHalfFov"), FLinearColor(TanX, TanY, 0.f, 0.f));
		}
	}
	Head->SetProtectedSlot(ShownSlot);

	CurrentAgeMs = ShownFrameId >= 0 ? float((Now - ShownCaptureTime) * 1000.0) : -1.f;
	if (CurrentAgeMs >= 0.f && Method != ETeleopMethod::GT)
	{
		WindowAgesMs.Add(CurrentAgeMs);
	}

	// Thread / GPU frame times (the values `stat unit` shows; one frame behind).
	WindowGameMs.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
	WindowRenderMs.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
	WindowRhiMs.Add(FPlatformTime::ToMilliseconds(GRHIThreadTime));
	WindowGpuMs.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
	++WindowTicks;

	if (SessionStart <= 0.0)
	{
		SessionStart = Now;
	}
	const float ProfileAfter = TeleopCVars::DebugProfileGPUAfterSec.GetValueOnGameThread();
	if (!bProfiledGPU && ProfileAfter > 0.f && Now - SessionStart >= ProfileAfter && GEngine)
	{
		bProfiledGPU = true;
		UE_LOG(LogTeleop, Log, TEXT("Running ProfileGPU (teleop.Debug.ProfileGPUAfterSec=%.1f)"), ProfileAfter);
		GEngine->Exec(GetWorld(), TEXT("ProfileGPU"));
	}

	UpdateStats(Now);
}

void ATeleopPresenter::UpdateStats(double Now)
{
	if (WindowStart <= 0.0)
	{
		WindowStart = Now;
	}
	if (Now - WindowStart < StatsWindowSeconds)
	{
		return;
	}

	FTeleopDisplayStats Stats;
	WindowAgesMs.Sort();
	Stats.Samples = WindowAgesMs.Num();
	if (Stats.Samples > 0)
	{
		Stats.AgeP50Ms = Percentile(WindowAgesMs, 0.5f);
		Stats.AgeP95Ms = Percentile(WindowAgesMs, 0.95f);
		Stats.AgeMinMs = WindowAgesMs[0];
		Stats.AgeMaxMs = WindowAgesMs.Last();
	}
	Stats.ShownFps = WindowNewFrames / float(Now - WindowStart);
	LastStats = Stats;

	const float LatencyMs = TeleopCVars::LatencyMs.GetValueOnGameThread();
	const float CamHz = RobotHead.IsValid() ? RobotHead->GetMeasuredCamHz() : 0.f;
	const int32 Neck = TeleopCVars::NeckMode.GetValueOnGameThread();

	auto Median = [](TArray<float>& V) { V.Sort(); return V.Num() ? V[V.Num() / 2] : 0.f; };
	const float Fps = WindowTicks / float(Now - WindowStart);
	UE_LOG(LogTeleop, Log, TEXT("[stats] method=%s latency=%.0fms age p50=%.1f p95=%.1f min=%.1f max=%.1f ms shown=%.1f fps cam=%.1f Hz | fps=%.1f game=%.2f render=%.2f rhi=%.2f gpu=%.2f ms"),
		MethodName(ActiveMethod), LatencyMs, Stats.AgeP50Ms, Stats.AgeP95Ms, Stats.AgeMinMs, Stats.AgeMaxMs, Stats.ShownFps, CamHz,
		Fps, Median(WindowGameMs), Median(WindowRenderMs), Median(WindowRhiMs), Median(WindowGpuMs));
	WindowGameMs.Reset();
	WindowRenderMs.Reset();
	WindowRhiMs.Reset();
	WindowGpuMs.Reset();
	WindowTicks = 0;

	if (ATeleopOperatorPawn* P = Pawn.Get())
	{
		P->SetHudText(FString::Printf(TEXT("%s   age %.0f ms (p95 %.0f)\nL %.0f ms   cmd %.0f ms   cam %.0f Hz   neck %s"),
			MethodName(ActiveMethod), Stats.AgeP50Ms, Stats.AgeP95Ms, LatencyMs,
			TeleopCVars::CmdLatencyMs.GetValueOnGameThread(), CamHz, NeckName(Neck)));
	}

	WindowAgesMs.Reset();
	WindowNewFrames = 0;
	WindowStart = Now;
}
