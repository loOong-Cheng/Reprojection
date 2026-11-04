#include "TeleopPresenter.h"

#include "FrameRingBuffer.h"
#include "Reprojection.h"
#include "RobotHead.h"
#include "TeleopCVars.h"
#include "TeleopDepthDegrader.h"
#include "TeleopGameMode.h"
#include "TeleopLatencyModel.h"
#include "TeleopLogger.h"
#include "TeleopOperatorPawn.h"

#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "RHIGPUReadback.h"
#include "RenderTimer.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UnrealClient.h"

#include <atomic>

/** Async GPU readback of the central depth window of the shown frame (M2). Shared with the render thread. */
struct ATeleopPresenter::FRadiusReadback
{
	TUniquePtr<FRHIGPUTextureReadback> Readback;
	FIntPoint Size = FIntPoint::ZeroValue;
	std::atomic<bool> bPending{ false };
	FCriticalSection Lock;
	TArray<float> Values;
	bool bHasResult = false;
};

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

	const TCHAR* MethodShort(ETeleopMethod Method)
	{
		static const TCHAR* Names[] = { TEXT("M0"), TEXT("M1"), TEXT("M2"), TEXT("M3"), TEXT("M4"), TEXT("GT") };
		return Names[FMath::Clamp(int32(Method), 0, 5)];
	}

	const TCHAR* NeckName(int32 Mode)
	{
		static const TCHAR* Names[] = { TEXT("Fixed"), TEXT("Rot2DoF"), TEXT("Rot3DoF"), TEXT("Full6DoF"), TEXT("Waist3+Neck2") };
		return Names[FMath::Clamp(Mode, 0, 4)];
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

	FLinearColor Vec(const FVector& V)
	{
		return FLinearColor(float(V.X), float(V.Y), float(V.Z), 0.f);
	}

	// Controller-driven changes are logged, so a session log shows what the operator changed and when.
	void SetCVar(TAutoConsoleVariable<int32>& CVar, int32 Value)
	{
		CVar->Set(Value, ECVF_SetByConsole);
		UE_LOG(LogTeleop, Log, TEXT("[control] %s = %d"), *IConsoleManager::Get().FindConsoleObjectName(CVar.AsVariable()), Value);
	}
	void SetCVar(TAutoConsoleVariable<float>& CVar, float Value)
	{
		CVar->Set(Value, ECVF_SetByConsole);
		UE_LOG(LogTeleop, Log, TEXT("[control] %s = %g"), *IConsoleManager::Get().FindConsoleObjectName(CVar.AsVariable()), Value);
	}

	ATeleopPresenter* FindPresenter(UWorld* World)
	{
		ATeleopGameMode* GM = World ? World->GetAuthGameMode<ATeleopGameMode>() : nullptr;
		return GM ? GM->GetPresenter() : nullptr;
	}
}

static FAutoConsoleCommandWithWorld GTeleopScreenshotPair(TEXT("teleop.ScreenshotPair"),
	TEXT("Save a screenshot of the current method and one of GT at the same pose into the session log folder."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W) { if (ATeleopPresenter* P = FindPresenter(W)) { P->RequestScreenshotPair(); } }));

static FAutoConsoleCommandWithWorld GTeleopBlindToggle(TEXT("teleop.Blind.Toggle"),
	TEXT("Start / stop the blind test (menu button)."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W) { if (ATeleopPresenter* P = FindPresenter(W)) { P->ToggleBlindTest(); } }));

static FAutoConsoleCommandWithWorldAndArgs GTeleopBlindRate(TEXT("teleop.Blind.Rate"),
	TEXT("teleop.Blind.Rate <delta>: change the current rating (stick up/down)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		if (ATeleopPresenter* P = FindPresenter(W)) { P->AdjustRating(Args.Num() ? FCString::Atoi(*Args[0]) : 1); }
	}));

static FAutoConsoleCommandWithWorld GTeleopBlindConfirm(TEXT("teleop.Blind.Confirm"),
	TEXT("Confirm the current rating / end the viewing phase (A)."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W) { if (ATeleopPresenter* P = FindPresenter(W)) { P->CycleMethod(); } }));

ATeleopPresenter::ATeleopPresenter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	PresentMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Teleop/Materials/M_TeleopPresent.M_TeleopPresent")));
	DepthMeshMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Teleop/Materials/M_TeleopDepthMesh.M_TeleopDepthMesh")));
}

void ATeleopPresenter::BeginPlay()
{
	Super::BeginPlay();
	Readback = MakeShared<FRadiusReadback, ESPMode::ThreadSafe>();
	AdaptiveRadiusCm = AdaptiveTargetCm = TeleopCVars::SphereRadiusCm.GetValueOnGameThread();

	if (UMaterialInterface* Base = PresentMaterial.LoadSynchronous())
	{
		PresentMID = UMaterialInstanceDynamic::Create(Base, this);
	}
	else
	{
		UE_LOG(LogTeleop, Error, TEXT("Presenter: material not found: %s"), *PresentMaterial.ToString());
	}
	if (UMaterialInterface* Base = DepthMeshMaterial.LoadSynchronous())
	{
		MeshMID = UMaterialInstanceDynamic::Create(Base, this);
	}
	else
	{
		UE_LOG(LogTeleop, Error, TEXT("Presenter: material not found: %s"), *DepthMeshMaterial.ToString());
	}
}

void ATeleopPresenter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FlushRenderingCommands(); // the readback state is shared with the render thread
	Readback.Reset();
	Super::EndPlay(EndPlayReason);
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

	// M3 mesh lives in the operator room: attached to the tracking origin, so its local space is the room frame.
	if (InPawn)
	{
		DepthMesh = NewObject<UProceduralMeshComponent>(this, TEXT("DepthMesh"));
		DepthMesh->SetupAttachment(InPawn->GetVROrigin());
		DepthMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DepthMesh->SetCastShadow(false);
		DepthMesh->SetRenderCustomDepth(true);
		DepthMesh->SetCustomDepthStencilValue(DepthMeshStencilValue);
		DepthMesh->SetBoundsScale(200.f); // WPO moves vertices far from the rest grid (HANDOFF §7.6)
		DepthMesh->SetVisibility(false);
		DepthMesh->RegisterComponent();
		DepthMesh->AttachToComponent(InPawn->GetVROrigin(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
}

bool ATeleopPresenter::GetShownFramePose(FTransform& OutLeft, FTransform& OutRight) const
{
	const UFrameRingBuffer* Buffer = RobotHead.IsValid() ? RobotHead->GetBuffer() : nullptr;
	if (!Buffer || ShownSlot == INDEX_NONE)
	{
		return false;
	}
	const FTeleopFrameMeta& Meta = Buffer->GetMeta(ShownSlot);
	OutLeft = Meta.EyeWorld[0];
	OutRight = Meta.EyeWorld[1];
	return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Method selection

bool ATeleopPresenter::UsesAdaptiveRadius(ETeleopMethod Method) const
{
	return Method == ETeleopMethod::M2_SphereAdaptive
		|| (Method == ETeleopMethod::M4_Hybrid && TeleopCVars::M4Sphere.GetValueOnGameThread() == 2);
}

ETeleopMethod ATeleopPresenter::ResolveRequestedMethod() const
{
	if (bPairForceGT)
	{
		return ETeleopMethod::GT;
	}
	if (BlindTest.IsActive())
	{
		return BlindTest.GetMethod();
	}
	return static_cast<ETeleopMethod>(FMath::Clamp(TeleopCVars::Method.GetValueOnGameThread(), 0, 5));
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
	const bool bMesh = Method == ETeleopMethod::M3_DepthMesh || Method == ETeleopMethod::M4_Hybrid;
	const bool bPostProcess = !bGT && Method != ETeleopMethod::M3_DepthMesh;

	P->SetRemoteView(bGT, GM->GetLocalToRemote());
	P->SetCameraBlendable(PresentMID, bPostProcess);
	if (DepthMesh)
	{
		DepthMesh->SetVisibility(bMesh);
	}
	if (PresentMID)
	{
		PresentMID->SetScalarParameterValue(TEXT("Mode"), Method == ETeleopMethod::M0_Naive ? 0.f : 1.f);
	}
	ActiveMethod = Method;
	bMethodApplied = true;
	UE_LOG(LogTeleop, Log, TEXT("Method: %s"), MethodName(Method));
}

// ---------------------------------------------------------------------------------------------------------------
// Tick

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
	const double Now = FPlatformTime::Seconds();
	if (SessionStart <= 0.0)
	{
		SessionStart = Now;
	}

	BlindTest.Tick(Now);
	UpdateScreenshotPair();

	const ETeleopMethod Method = ResolveRequestedMethod();
	if (!bMethodApplied || Method != ActiveMethod)
	{
		ApplyMethod(Method);
	}
	else if (Method == ETeleopMethod::GT)
	{
		P->SetRemoteView(true, GM->GetLocalToRemote()); // follow recentering
	}

	SelectFrame(Now);
	UpdateAdaptiveRadius(Now, DeltaSeconds);
	UpdateMaterialParams();

	// Thread / GPU frame times (the values `stat unit` shows; one frame behind).
	WindowGameMs.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
	WindowRenderMs.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
	WindowRhiMs.Add(FPlatformTime::ToMilliseconds(GRHIThreadTime));
	WindowGpuMs.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
	++WindowTicks;

	const float ProfileAfter = TeleopCVars::DebugProfileGPUAfterSec.GetValueOnGameThread();
	if (!bProfiledGPU && ProfileAfter > 0.f && Now - SessionStart >= ProfileAfter && GEngine)
	{
		bProfiledGPU = true;
		GEngine->Exec(GetWorld(), TEXT("ProfileGPU"));
	}

	LogDisplay(Now);
	UpdateStats(Now);
}

void ATeleopPresenter::SelectFrame(double Now)
{
	UFrameRingBuffer* Buffer = RobotHead->GetBuffer();

	// Receiver-side latency of newly arrived frames (known from the frame timestamp at arrival).
	for (int32 Slot = 0; Slot < Buffer->NumSlots(); ++Slot)
	{
		const FTeleopFrameMeta& M = Buffer->GetMeta(Slot);
		if (M.IsValid() && !M.bDropped && M.ArrivalTime <= Now && !CountedArrivals.Contains(M.FrameId))
		{
			CountedArrivals.Add(M.FrameId);
			ArrivalLatencies.Add({ M.ArrivalTime, float((M.ArrivalTime - M.CaptureTime) * 1000.0) });
		}
	}
	ArrivalLatencies.RemoveAll([Now](const TPair<double, float>& E) { return E.Key < Now - 2.0; });
	if (CountedArrivals.Num() > 256)
	{
		const int64 MinKeep = RobotHead->GetLastFrameId() - 128;
		for (auto It = CountedArrivals.CreateIterator(); It; ++It)
		{
			if (*It < MinKeep)
			{
				It.RemoveCurrent();
			}
		}
	}

	// Jitter buffer target: recent p95 latency (HANDOFF §4).
	{
		TArray<float> L;
		for (const TPair<double, float>& E : ArrivalLatencies)
		{
			L.Add(E.Value);
		}
		L.Sort();
		JitterBufferTargetMs = Percentile(L, 0.95f);
	}
	const bool bJitterBuffer = TeleopCVars::JitterBuffer.GetValueOnGameThread() != 0;
	const double Delay = bJitterBuffer ? JitterBufferTargetMs / 1000.0 : 0.0;

	// Newest eligible frame; never go back to an older one (out-of-order arrival drops the older frame).
	const int32 Candidate = Buffer->FindNewestArrived(Now, Delay);
	if (Candidate != INDEX_NONE && Buffer->GetMeta(Candidate).FrameId > ShownFrameId)
	{
		const FTeleopFrameMeta& Meta = Buffer->GetMeta(Candidate);
		ShownSlot = Candidate;
		ShownFrameId = Meta.FrameId;
		ShownCaptureTime = Meta.CaptureTime;
		++WindowNewFrames;
	}
	RobotHead->SetProtectedSlot(ShownSlot);

	CurrentAgeMs = ShownFrameId >= 0 ? float((Now - ShownCaptureTime) * 1000.0) : -1.f;
	if (CurrentAgeMs >= 0.f && ActiveMethod != ETeleopMethod::GT)
	{
		WindowAgesMs.Add(CurrentAgeMs);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Material parameters

void ATeleopPresenter::SetFrameParams(UMaterialInstanceDynamic* MID, const FTeleopFrameMeta& Meta, int32 Slot)
{
	UFrameRingBuffer* Buffer = RobotHead->GetBuffer();
	MID->SetTextureParameterValue(TEXT("LeftFrame"), Buffer->GetTexture(Slot, 0));
	MID->SetTextureParameterValue(TEXT("RightFrame"), Buffer->GetTexture(Slot, 1));
	if (UTextureRenderTarget2D* DL = Buffer->GetDepthTexture(Slot, 0))
	{
		MID->SetTextureParameterValue(TEXT("LeftDepth"), DL);
		MID->SetTextureParameterValue(TEXT("RightDepth"), Buffer->GetDepthTexture(Slot, 1));
	}

	const float TanX = FMath::Tan(FMath::DegreesToRadians(Meta.HorizontalFovDeg * 0.5f));
	const float TanY = TanX * Meta.Height / float(FMath::Max(Meta.Width, 1));
	MID->SetVectorParameterValue(TEXT("TanHalfFov"), FLinearColor(TanX, TanY, 0.f, 0.f));

	static const TCHAR* Suffix[2] = { TEXT("L"), TEXT("R") };
	for (int32 Eye = 0; Eye < 2; ++Eye)
	{
		const FTransform& T = Meta.EyeWorld[Eye];
		MID->SetVectorParameterValue(*FString::Printf(TEXT("CamPos%s"), Suffix[Eye]), Vec(T.GetLocation()));
		MID->SetVectorParameterValue(*FString::Printf(TEXT("CamFwd%s"), Suffix[Eye]), Vec(T.GetUnitAxis(EAxis::X)));
		MID->SetVectorParameterValue(*FString::Printf(TEXT("CamRight%s"), Suffix[Eye]), Vec(T.GetUnitAxis(EAxis::Y)));
		MID->SetVectorParameterValue(*FString::Printf(TEXT("CamUp%s"), Suffix[Eye]), Vec(T.GetUnitAxis(EAxis::Z)));
	}
}

void ATeleopPresenter::UpdateMaterialParams()
{
	ATeleopGameMode* GM = GameMode.Get();
	ATeleopOperatorPawn* P = Pawn.Get();
	UFrameRingBuffer* Buffer = RobotHead->GetBuffer();
	if (!GM || !P || ShownSlot == INDEX_NONE)
	{
		return;
	}
	const FTeleopFrameMeta& Meta = Buffer->GetMeta(ShownSlot);

	// Operator room -> remote world (remote = MapRot * (roomWorld - RoomSeat) + MapT). The room has no rotation.
	const FTransform Map = GM->GetLocalToRemote();
	const FVector RoomSeat = P->GetRoomTransform().GetLocation();
	const float Radius = UsesAdaptiveRadius(ActiveMethod) ? AdaptiveRadiusCm : TeleopCVars::SphereRadiusCm.GetValueOnGameThread();

	for (UMaterialInstanceDynamic* MID : { PresentMID.Get(), MeshMID.Get() })
	{
		if (!MID)
		{
			continue;
		}
		SetFrameParams(MID, Meta, ShownSlot);
		MID->SetVectorParameterValue(TEXT("MapX"), Vec(Map.TransformVectorNoScale(FVector::XAxisVector)));
		MID->SetVectorParameterValue(TEXT("MapY"), Vec(Map.TransformVectorNoScale(FVector::YAxisVector)));
		MID->SetVectorParameterValue(TEXT("MapZ"), Vec(Map.TransformVectorNoScale(FVector::ZAxisVector)));
		MID->SetVectorParameterValue(TEXT("MapT"), Vec(Map.GetLocation()));
		MID->SetVectorParameterValue(TEXT("RoomSeat"), Vec(RoomSeat));
		MID->SetScalarParameterValue(TEXT("SphereR"), Radius);
	}

	if (MeshMID && DepthMesh)
	{
		EnsureDepthMesh(Buffer->GetDepthWidth(), Buffer->GetDepthHeight());
		MeshMID->SetScalarParameterValue(TEXT("EdgeDispPx"), TeleopCVars::M3EdgeDispPx.GetValueOnGameThread());
		// Reference disparity scale f * b for the rubber-sheet test (64 mm reference baseline, so it also works with b = 0).
		const float FocalPx = (Meta.Width * 0.5f) / FMath::Tan(FMath::DegreesToRadians(Meta.HorizontalFovDeg * 0.5f));
		MeshMID->SetScalarParameterValue(TEXT("DispScale"), FocalPx * 6.4f);
		MeshMID->SetVectorParameterValue(TEXT("GridStep"),
			FLinearColor(1.f / FMath::Max(DepthMeshGrid.X, 1), 1.f / FMath::Max(DepthMeshGrid.Y, 1), 0.f, 0.f));
		// No valid depth for this frame yet (preset just changed): hide the mesh instead of showing stale geometry.
		const bool bMesh = ActiveMethod == ETeleopMethod::M3_DepthMesh || ActiveMethod == ETeleopMethod::M4_Hybrid;
		DepthMesh->SetVisibility(bMesh && Meta.bDepthValid);
	}
}

void ATeleopPresenter::EnsureDepthMesh(int32 DepthWidth, int32 DepthHeight)
{
	const FIntPoint Grid(FMath::Clamp(DepthWidth, 2, MaxGridSize.X), FMath::Clamp(DepthHeight, 2, MaxGridSize.Y));
	if (Grid == DepthMeshGrid || DepthWidth <= 0)
	{
		return;
	}
	DepthMeshGrid = Grid;

	// Vertices at the grid cell centers in uv; rest positions on a plane in front (WPO moves them).
	TArray<FVector> Vertices;
	TArray<FVector2D> UV0;
	TArray<FVector> Normals;
	TArray<int32> Triangles;
	Vertices.Reserve(Grid.X * Grid.Y);
	UV0.Reserve(Grid.X * Grid.Y);
	for (int32 j = 0; j < Grid.Y; ++j)
	{
		for (int32 i = 0; i < Grid.X; ++i)
		{
			const FVector2D UV((i + 0.5) / Grid.X, (j + 0.5) / Grid.Y);
			UV0.Add(UV);
			Vertices.Add(FVector(100.0, (UV.X - 0.5) * 200.0, (0.5 - UV.Y) * 150.0 + 120.0));
			Normals.Add(FVector(-1, 0, 0));
		}
	}
	Triangles.Reserve((Grid.X - 1) * (Grid.Y - 1) * 6);
	for (int32 j = 0; j + 1 < Grid.Y; ++j)
	{
		for (int32 i = 0; i + 1 < Grid.X; ++i)
		{
			const int32 A = j * Grid.X + i;
			const int32 B = A + 1;
			const int32 C = A + Grid.X;
			const int32 D = C + 1;
			Triangles.Append({ A, C, B, B, C, D });
		}
	}
	DepthMesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UV0, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
	if (MeshMID)
	{
		DepthMesh->SetMaterial(0, MeshMID);
	}
	UE_LOG(LogTeleop, Log, TEXT("M3 depth mesh: %dx%d vertices for %dx%d depth"), Grid.X, Grid.Y, DepthWidth, DepthHeight);
}

// ---------------------------------------------------------------------------------------------------------------
// M2 adaptive radius: per shown frame, the median of the valid degraded depth in a central window; the target is the
// median of those over the last teleop.M2.WindowSec (2 s), and R eases toward it (teleop.M2.SmoothSec).

void ATeleopPresenter::UpdateAdaptiveRadius(double Now, float DeltaSeconds)
{
	if (!Readback || ShownSlot == INDEX_NONE)
	{
		return;
	}

	// Consume a finished readback.
	{
		TArray<float> Values;
		{
			FScopeLock Lock(&Readback->Lock);
			if (Readback->bHasResult)
			{
				Values = MoveTemp(Readback->Values);
				Readback->bHasResult = false;
			}
		}
		if (Values.Num() > 0)
		{
			Values.Sort();
			RadiusSamples.Emplace(Now, Values[Values.Num() / 2]);
		}
	}

	// Window median: R only moves once most of the last WindowSec saw the new depth, not on every glance.
	const double WindowSec = FMath::Max(TeleopCVars::M2WindowSec.GetValueOnGameThread(), 0.f);
	RadiusSamples.RemoveAll([Now, WindowSec](const TPair<double, float>& S) { return Now - S.Key > WindowSec; });
	if (RadiusSamples.Num() > 0)
	{
		TArray<float> Medians;
		Medians.Reserve(RadiusSamples.Num());
		for (const TPair<double, float>& S : RadiusSamples)
		{
			Medians.Add(S.Value);
		}
		Medians.Sort();
		AdaptiveTargetCm = FMath::Clamp(Medians[Medians.Num() / 2], 20.f, 5000.f);
	}

	// Ease toward the target (time constant teleop.M2.SmoothSec).
	const float Tau = FMath::Max(TeleopCVars::M2SmoothSec.GetValueOnGameThread(), 1e-3f);
	AdaptiveRadiusCm += (AdaptiveTargetCm - AdaptiveRadiusCm) * (1.f - FMath::Exp(-DeltaSeconds / Tau));

	// Request a readback of the shown frame (one in flight at a time) while M2 is in use.
	const FTeleopFrameMeta& Meta = RobotHead->GetBuffer()->GetMeta(ShownSlot);
	UTextureRenderTarget2D* Depth = RobotHead->GetBuffer()->GetDepthTexture(ShownSlot, 0);
	if (!UsesAdaptiveRadius(ActiveMethod) || !Meta.bDepthValid || !Depth || Readback->bPending || Meta.FrameId == ReadbackFrameId)
	{
		if (Readback->bPending)
		{
			// Poll on the render thread; copy the window out when the GPU is done.
			ENQUEUE_RENDER_COMMAND(TeleopRadiusPoll)([State = Readback](FRHICommandListImmediate&)
			{
				if (!State->Readback || !State->Readback->IsReady())
				{
					return;
				}
				int32 RowPitch = 0;
				const float* Data = static_cast<const float*>(State->Readback->Lock(RowPitch));
				TArray<float> Values;
				if (Data)
				{
					for (int32 y = 0; y < State->Size.Y; ++y)
					{
						for (int32 x = 0; x < State->Size.X; ++x)
						{
							const float Z = Data[y * RowPitch + x];
							if (Z > 0.f)
							{
								Values.Add(Z);
							}
						}
					}
				}
				State->Readback->Unlock();
				{
					FScopeLock Lock(&State->Lock);
					State->Values = MoveTemp(Values);
					State->bHasResult = true;
				}
				State->bPending = false;
			});
		}
		return;
	}

	const float Frac = FMath::Clamp(TeleopCVars::M2WindowFrac.GetValueOnGameThread(), 0.01f, 1.f);
	const int32 W = FMath::Max(3, FMath::RoundToInt(Depth->SizeX * Frac));
	const int32 H = FMath::Max(3, FMath::RoundToInt(Depth->SizeY * Frac));
	const FIntPoint Origin((Depth->SizeX - W) / 2, (Depth->SizeY - H) / 2);
	FTextureRenderTargetResource* Resource = Depth->GameThread_GetRenderTargetResource();
	ReadbackFrameId = Meta.FrameId;
	Readback->bPending = true;
	ENQUEUE_RENDER_COMMAND(TeleopRadiusReadback)([State = Readback, Resource, Origin, W, H](FRHICommandListImmediate& RHICmdList)
	{
		if (!Resource || !Resource->GetRenderTargetTexture())
		{
			State->bPending = false;
			return;
		}
		// FRHIGPUTextureReadback sizes its staging texture on the first copy and reuses it, so a larger window
		// (depth preset with a higher depth resolution) would overflow it: D3D12 E_INVALIDARG, fatal. Recreate it
		// when the size changes. Safe here: a new copy is only enqueued when none is pending.
		if (!State->Readback || State->Size != FIntPoint(W, H))
		{
			State->Readback = MakeUnique<FRHIGPUTextureReadback>(TEXT("TeleopRadiusReadback"));
		}
		State->Size = FIntPoint(W, H);
		State->Readback->EnqueueCopy(RHICmdList, Resource->GetRenderTargetTexture(), FIntVector(Origin.X, Origin.Y, 0), 0, FIntVector(W, H, 1));
	});
}

// ---------------------------------------------------------------------------------------------------------------
// Screenshot pair (HANDOFF §7.8): current method, then GT at the same pose.

void ATeleopPresenter::RequestScreenshotPair()
{
	if (PairPhase == 0)
	{
		PairPhase = 1;
		PairWait = 0;
		++PairIndex;
	}
}

void ATeleopPresenter::UpdateScreenshotPair()
{
	if (PairPhase == 0)
	{
		return;
	}
	ATeleopGameMode* GM = GameMode.Get();
	const FString Dir = GM && GM->GetLogger() ? GM->GetLogger()->GetSessionDir() : FPaths::ProjectSavedDir();
	if (PairWait > 0)
	{
		--PairWait;
		return;
	}
	switch (PairPhase)
	{
	case 1: // current method
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("pair%02d_%s.png"), PairIndex, MethodShort(ActiveMethod))), false, false);
		PairPhase = 2;
		PairWait = 2;
		break;
	case 2: // switch to GT
		bPairForceGT = true;
		PairPhase = 3;
		PairWait = 3;
		break;
	case 3:
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("pair%02d_GT.png"), PairIndex)), false, false);
		PairPhase = 4;
		PairWait = 2;
		break;
	default:
		bPairForceGT = false;
		PairPhase = 0;
		UE_LOG(LogTeleop, Log, TEXT("Screenshot pair %d saved to %s"), PairIndex, *Dir);
		break;
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Controls (HANDOFF §8)

void ATeleopPresenter::CycleMethod()
{
	if (BlindTest.IsActive())
	{
		BlindTest.Confirm(FPlatformTime::Seconds());
		return;
	}
	// GT > M1 > M2 > M3 > M4 > M0 > GT: the session starts in GT, and the head-locked M0 comes last (user, 2026-11-04).
	static constexpr int32 Order[] = { 5, 1, 2, 3, 4, 0 };
	const int32 Current = FMath::Clamp(TeleopCVars::Method.GetValueOnGameThread(), 0, 5);
	int32 Index = 0;
	for (int32 i = 0; i < UE_ARRAY_COUNT(Order); ++i)
	{
		if (Order[i] == Current)
		{
			Index = i;
		}
	}
	SetCVar(TeleopCVars::Method, Order[(Index + 1) % UE_ARRAY_COUNT(Order)]);
}

void ATeleopPresenter::CycleLatencyPreset()
{
	const int32 Current = TeleopCVars::LatencyPreset.GetValueOnGameThread();
	SetCVar(TeleopCVars::LatencyPreset, Current >= FTeleopLatencyModel::NumNetworkPresets - 1 ? 0 : Current + 1);
}

void ATeleopPresenter::AdjustBaseLatency(int32 Direction)
{
	SetCVar(TeleopCVars::LatencyBaseOffsetMs, FMath::Clamp(TeleopCVars::LatencyBaseOffsetMs.GetValueOnGameThread() + 10.f * Direction, -200.f, 500.f));
}

void ATeleopPresenter::ToggleJitterBuffer()
{
	SetCVar(TeleopCVars::JitterBuffer, TeleopCVars::JitterBuffer.GetValueOnGameThread() ? 0 : 1);
}

void ATeleopPresenter::CycleDepthPreset()
{
	SetCVar(TeleopCVars::DepthPreset, (TeleopCVars::DepthPreset.GetValueOnGameThread() + 1) % UTeleopDepthDegrader::NumPresets);
}

void ATeleopPresenter::CycleNeckMode()
{
	SetCVar(TeleopCVars::NeckMode, (FMath::Clamp(TeleopCVars::NeckMode.GetValueOnGameThread(), 0, 4) + 1) % 5);
}

void ATeleopPresenter::AdjustSphereRadius(int32 Direction)
{
	SetCVar(TeleopCVars::SphereRadiusCm, FMath::Clamp(TeleopCVars::SphereRadiusCm.GetValueOnGameThread() + 10.f * Direction, 20.f, 1000.f));
}

void ATeleopPresenter::AdjustBaseline(int32 Direction)
{
	SetCVar(TeleopCVars::BaselineMm, FMath::Clamp(TeleopCVars::BaselineMm.GetValueOnGameThread() + 5.f * Direction, 40.f, 90.f));
}

void ATeleopPresenter::ToggleBlindTest()
{
	if (BlindTest.IsActive() || BlindTest.GetPhase() == FTeleopBlindTest::EPhase::Done)
	{
		BlindTest.Stop();
	}
	else
	{
		ATeleopGameMode* GM = GameMode.Get();
		BlindTest.Start(FPlatformTime::Seconds(), GM ? GM->GetLogger() : nullptr);
	}
}

void ATeleopPresenter::AdjustRating(int32 Direction)
{
	BlindTest.AdjustRating(Direction);
	UE_LOG(LogTeleop, Log, TEXT("[control] blind rating %+d (%s)"), Direction,
		BlindTest.GetPhase() == FTeleopBlindTest::EPhase::Rating ? TEXT("applied") : TEXT("ignored, not in the rating phase"));
}

// ---------------------------------------------------------------------------------------------------------------
// Logging, stats, HUD

FString ATeleopPresenter::MethodLabel() const
{
	if (BlindTest.IsActive())
	{
		return BlindTest.GetConditionCode(); // method hidden in blind mode
	}
	return MethodName(ActiveMethod);
}

void ATeleopPresenter::LogDisplay(double Now)
{
	ATeleopGameMode* GM = GameMode.Get();
	FTeleopLogger* Logger = GM ? GM->GetLogger() : nullptr;
	if (!Logger)
	{
		return;
	}
	FTeleopDisplayRow Row;
	Row.Time = Now;
	Row.ShownFrameId = ShownFrameId;
	Row.ShownCaptureTime = ShownCaptureTime;
	Row.AgeMs = CurrentAgeMs;
	Row.Method = MethodShort(ActiveMethod);
	Row.LatencyPreset = FTeleopLatencyModel::GetPresetName(TeleopCVars::LatencyPreset.GetValueOnGameThread());
	Row.BaseOffsetMs = TeleopCVars::LatencyBaseOffsetMs.GetValueOnGameThread();
	Row.bJitterBuffer = TeleopCVars::JitterBuffer.GetValueOnGameThread() != 0;
	Row.JitterBufferTargetMs = JitterBufferTargetMs;
	Row.DepthPreset = UTeleopDepthDegrader::GetActivePreset().Name;
	Row.SphereRadiusCm = UsesAdaptiveRadius(ActiveMethod) ? AdaptiveRadiusCm : TeleopCVars::SphereRadiusCm.GetValueOnGameThread();
	Row.BaselineMm = TeleopCVars::BaselineMm.GetValueOnGameThread();
	Row.NeckMode = TeleopCVars::NeckMode.GetValueOnGameThread();
	Row.CmdLatencyMs = float(GM->GetLatencyModel().GetCommandLatencySeconds() * 1000.0);
	Row.bBlind = BlindTest.IsActive();
	Row.BlindCondition = BlindTest.IsActive() ? BlindTest.GetConditionCode() : FString();
	Logger->LogDisplay(Row);
}

void ATeleopPresenter::UpdateStats(double Now)
{
	if (WindowStart <= 0.0)
	{
		WindowStart = Now;
	}
	if (Now - WindowStart < StatsWindowSeconds)
	{
		// The HUD still updates every frame in blind mode (countdown / rating prompt).
		if (BlindTest.GetPhase() != FTeleopBlindTest::EPhase::Idle && Pawn.IsValid())
		{
			Pawn->SetHudText(BlindTest.GetHudText(Now));
		}
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
	{
		TArray<float> L;
		for (const TPair<double, float>& E : ArrivalLatencies)
		{
			if (E.Key >= Now - StatsWindowSeconds)
			{
				L.Add(E.Value);
			}
		}
		L.Sort();
		Stats.LatencyP50Ms = Percentile(L, 0.5f);
		Stats.LatencyP95Ms = Percentile(L, 0.95f);
	}
	{
		UFrameRingBuffer* Buffer = RobotHead->GetBuffer();
		int32 Captured = 0, Lost = 0;
		for (int32 Slot = 0; Slot < Buffer->NumSlots(); ++Slot)
		{
			const FTeleopFrameMeta& M = Buffer->GetMeta(Slot);
			if (M.IsValid() && M.CaptureTime >= Now - StatsWindowSeconds - 0.2 && M.CaptureTime < Now - 0.2)
			{
				++Captured;
				Lost += M.bDropped ? 1 : 0;
			}
		}
		Stats.LossRate = Captured ? float(Lost) / Captured : 0.f;
	}
	LastStats = Stats;

	auto Median = [](TArray<float>& V) { V.Sort(); return V.Num() ? V[V.Num() / 2] : 0.f; };
	const float Fps = WindowTicks / float(Now - WindowStart);
	const float CamHz = RobotHead.IsValid() ? RobotHead->GetMeasuredCamHz() : 0.f;
	const int32 Preset = TeleopCVars::LatencyPreset.GetValueOnGameThread();
	const float Offset = TeleopCVars::LatencyBaseOffsetMs.GetValueOnGameThread();
	const bool bJB = TeleopCVars::JitterBuffer.GetValueOnGameThread() != 0;
	const float Radius = UsesAdaptiveRadius(ActiveMethod) ? AdaptiveRadiusCm : TeleopCVars::SphereRadiusCm.GetValueOnGameThread();

	UE_LOG(LogTeleop, Log, TEXT("[stats] method=%s preset=%s%+.0f jb=%d latency p50=%.1f p95=%.1f loss=%.1f%% age p50=%.1f p95=%.1f min=%.1f max=%.1f ms shown=%.1f fps cam=%.1f Hz R=%.0f depth=%s | fps=%.1f game=%.2f render=%.2f rhi=%.2f gpu=%.2f ms"),
		MethodShort(ActiveMethod), *FTeleopLatencyModel::GetPresetName(Preset), Offset, bJB ? 1 : 0,
		Stats.LatencyP50Ms, Stats.LatencyP95Ms, Stats.LossRate * 100.f, Stats.AgeP50Ms, Stats.AgeP95Ms, Stats.AgeMinMs, Stats.AgeMaxMs,
		Stats.ShownFps, CamHz, Radius, UTeleopDepthDegrader::GetActivePreset().Name,
		Fps, Median(WindowGameMs), Median(WindowRenderMs), Median(WindowRhiMs), Median(WindowGpuMs));

	if (ATeleopOperatorPawn* P = Pawn.Get())
	{
		if (BlindTest.GetPhase() != FTeleopBlindTest::EPhase::Idle)
		{
			P->SetHudText(BlindTest.GetHudText(Now));
		}
		else
		{
			const bool bSphere = ActiveMethod == ETeleopMethod::M1_SphereFixed || ActiveMethod == ETeleopMethod::M2_SphereAdaptive || ActiveMethod == ETeleopMethod::M4_Hybrid;
			const ATeleopGameMode* GM = GameMode.Get();
			const FString Warning = GM ? GM->GetCalibrationWarning() : FString();
			P->SetHudText((Warning.IsEmpty() ? FString() : Warning + TEXT("\n")) +
				FString::Printf(TEXT("%s%s   age %.0f ms (p95 %.0f)\n%s %+.0f ms  JB %s  lat p50 %.0f / p95 %.0f  loss %.1f%%\ndepth %s   b %.0f mm   neck %s   cam %.0f Hz"),
				*MethodLabel(), bSphere ? *FString::Printf(TEXT("  R %.0f cm"), Radius) : TEXT(""),
				Stats.AgeP50Ms, Stats.AgeP95Ms,
				*FTeleopLatencyModel::GetPresetName(Preset), Offset, bJB ? *FString::Printf(TEXT("on (%.0f)"), JitterBufferTargetMs) : TEXT("off"),
				Stats.LatencyP50Ms, Stats.LatencyP95Ms, Stats.LossRate * 100.f,
				UTeleopDepthDegrader::GetActivePreset().Name, TeleopCVars::BaselineMm.GetValueOnGameThread(),
				NeckName(TeleopCVars::NeckMode.GetValueOnGameThread()), CamHz));
		}
	}

	if (ATeleopGameMode* GM = GameMode.Get())
	{
		if (FTeleopLogger* Logger = GM->GetLogger())
		{
			Logger->Flush();
		}
	}

	WindowAgesMs.Reset();
	WindowNewFrames = 0;
	WindowGameMs.Reset();
	WindowRenderMs.Reset();
	WindowRhiMs.Reset();
	WindowGpuMs.Reset();
	WindowTicks = 0;
	WindowStart = Now;
}
