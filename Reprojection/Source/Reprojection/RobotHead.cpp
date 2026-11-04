#include "RobotHead.h"

#include "FrameRingBuffer.h"
#include "Reprojection.h"
#include "TeleopCVars.h"
#include "TeleopDepthDegrader.h"
#include "TeleopGameMode.h"
#include "TeleopLogger.h"
#include "TeleopMath.h"
#include "TeleopOperatorPawn.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"

namespace
{
	USceneCaptureComponent2D* MakeRobotCamera(AActor* Owner, const TCHAR* Name, USceneComponent* Parent)
	{
		USceneCaptureComponent2D* Cam = Owner->CreateDefaultSubobject<USceneCaptureComponent2D>(Name);
		Cam->SetupAttachment(Parent);
		Cam->bCaptureEveryFrame = false;
		Cam->bCaptureOnMovement = false;
		Cam->bAlwaysPersistRenderingState = true;
		// RGB = scene color (linear HDR, before post processing), A = scene depth (view-space z, cm).
		Cam->CaptureSource = SCS_SceneColorSceneDepth;
		// Raw camera look (HANDOFF §7.5): no motion blur / temporal AA.
		Cam->ShowFlags.SetMotionBlur(false);
		Cam->ShowFlags.SetTemporalAA(false);
		return Cam;
	}

	/** First-order tracking with rate and acceleration limits, one axis (degrees). */
	void TrackAngle(double& Angle, double& Rate, double Target, double Tau, double MaxRate, double MaxAccel, double Dt)
	{
		const double Error = FMath::FindDeltaAngleDegrees(Angle, Target);
		const double DesiredRate = FMath::Clamp(Error / FMath::Max(Tau, 1e-3), -MaxRate, MaxRate);
		Rate += FMath::Clamp(DesiredRate - Rate, -MaxAccel * Dt, MaxAccel * Dt);
		Angle = FRotator::NormalizeAxis(Angle + Rate * Dt);
	}
}

ARobotHead::ARobotHead()
{
	PrimaryActorTick.bCanEverTick = true;
	// After physics, so captures see this frame's object poses (grabbed / thrown objects).
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	Base = CreateDefaultSubobject<USceneComponent>(TEXT("Base"));
	RootComponent = Base;

	Waist = CreateDefaultSubobject<USceneComponent>(TEXT("Waist"));
	Waist->SetupAttachment(Base);

	NeckPivot = CreateDefaultSubobject<USceneComponent>(TEXT("NeckPivot"));
	NeckPivot->SetupAttachment(Waist);

	CameraRig = CreateDefaultSubobject<USceneComponent>(TEXT("CameraRig"));
	CameraRig->SetupAttachment(NeckPivot);

	CameraLeft = MakeRobotCamera(this, TEXT("CameraLeft"), CameraRig);
	CameraRight = MakeRobotCamera(this, TEXT("CameraRight"), CameraRig);
}

void ARobotHead::BeginPlay()
{
	Super::BeginPlay();

	CameraLeft->FOVAngle = HorizontalFovDeg;
	CameraRight->FOVAngle = HorizontalFovDeg;

	Buffer = NewObject<UFrameRingBuffer>(this, TEXT("FrameRingBuffer"));
	Buffer->Initialize(NumSlots, CaptureWidth, CaptureHeight);

	Degrader = NewObject<UTeleopDepthDegrader>(this, TEXT("DepthDegrader"));
	Degrader->Initialize(this);

	// Captures need a target at registration time; the real target is switched per frame.
	CameraLeft->TextureTarget = Buffer->GetTexture(0, 0);
	CameraRight->TextureTarget = Buffer->GetTexture(0, 1);

	UE_LOG(LogTeleop, Log, TEXT("RobotHead: %d slots x 2 eyes, %dx%d RGBA16f, HFOV %.1f"), NumSlots, CaptureWidth, CaptureHeight, HorizontalFovDeg);
}

void ARobotHead::Init(ATeleopGameMode* InGameMode, ATeleopOperatorPawn* InPawn)
{
	GameMode = InGameMode;
	Pawn = InPawn;
	if (InPawn)
	{
		AddTickPrerequisiteActor(InPawn);
		// The operator pawn holds the HUD and, in GT mode, sits right here; the robot must not see it.
		CameraLeft->HiddenActors.AddUnique(InPawn);
		CameraRight->HiddenActors.AddUnique(InPawn);
	}
}

void ARobotHead::HideFromRobotCameras(AActor* Actor)
{
	CameraLeft->HiddenActors.AddUnique(Actor);
	CameraRight->HiddenActors.AddUnique(Actor);
}

void ARobotHead::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const double Now = FPlatformTime::Seconds();
	if (TeleopCVars::DebugFreeze.GetValueOnGameThread() != 0)
	{
		return; // evaluation freeze: the robot and its cameras stand still (captures resync when it ends)
	}
	UpdateNeck(Now, DeltaSeconds);
	UpdateCameraRig();

	const float LogEvery = TeleopCVars::DebugLogHandsSec.GetValueOnGameThread();
	if (LogEvery > 0.f && Now - LastRobotLogTime >= LogEvery)
	{
		LastRobotLogTime = Now;
		const FVector Cam = Base->GetComponentTransform().InverseTransformPosition(CameraRig->GetComponentLocation());
		UE_LOG(LogTeleop, Log, TEXT("[robot] waist p%.1f y%.1f r%.1f neck p%.1f y%.1f r%.1f camera %s (station frame)"),
			WaistRotation.Pitch, WaistRotation.Yaw, WaistRotation.Roll, NeckRotation.Pitch, NeckRotation.Yaw, NeckRotation.Roll,
			*Cam.ToCompactString());
	}

	const double Period = 1.0 / FMath::Clamp(TeleopCVars::CamHz.GetValueOnGameThread(), 1.f, 240.f);
	if (NextCaptureTime <= 0.0 || NextCaptureTime < Now - Period)
	{
		NextCaptureTime = Now; // first frame, or fell more than a period behind: resync
	}
	if (Now >= NextCaptureTime)
	{
		CaptureFrame(Now);
		NextCaptureTime += Period;
	}

	if (Now - RateWindowStart >= 1.0)
	{
		MeasuredCamHz = RateWindowCount / float(Now - RateWindowStart);
		RateWindowStart = Now;
		RateWindowCount = 0;
	}
}

void ARobotHead::UpdateNeck(double Now, float DeltaSeconds)
{
	ATeleopGameMode* GM = GameMode.Get();
	ATeleopOperatorPawn* P = Pawn.Get();
	if (!GM || !P)
	{
		return;
	}

	const FVector Mount = TeleopCVars::CamMountOffset.GetValueOnGameThread() != 0 ? MountOffset : FVector::ZeroVector;
	// Neutral pivot: below/behind the calibrated eye so that, looking straight ahead, the cameras are at the eye.
	const FVector NominalPivot = FVector(0.f, 0.f, GM->GetCalibratedEyeHeight()) - Mount;

	FTransform LocalHead;
	const double CmdLatency = GM->GetLatencyModel().GetCommandLatencySeconds();
	if (!P->GetLocalHistory(ETeleopDevice::Head).Sample(Now - CmdLatency, LocalHead))
	{
		return;
	}
	// Target head pose, relative to the robot base.
	const FTransform TargetWorld = LocalHead * GM->GetLocalToRemote();
	const FTransform TargetRel = TargetWorld.GetRelativeTransform(Base->GetComponentTransform());
	const FRotator TargetRot = TargetRel.Rotator();

	if (!bNeckInitialized)
	{
		WaistRotation = FRotator::ZeroRotator;
		WaistRate = FVector::ZeroVector;
		NeckRotation = FRotator::ZeroRotator;
		NeckRate = FVector::ZeroVector;
		PivotOffset = FVector::ZeroVector;
		bNeckInitialized = true;
	}

	const ETeleopNeckMode Mode = static_cast<ETeleopNeckMode>(FMath::Clamp(TeleopCVars::NeckMode.GetValueOnGameThread(), 0, 4));
	const double Dt = FMath::Clamp<double>(DeltaSeconds, 1e-4, 0.1);

	// Waist: leans so the neck pivot goes where the operator's neck is (eye target minus the camera mount). Upright
	// in the other modes, which then behave exactly as without a waist.
	const FVector WaistPivot = NominalPivot - FVector(0.0, 0.0, WaistToNeckCm);
	FRotator WaistTarget = FRotator::ZeroRotator;
	if (Mode == ETeleopNeckMode::Waist3Neck2)
	{
		const double YawShare = FMath::Clamp(TeleopCVars::WaistYawShare.GetValueOnGameThread(), 0.f, 1.f);
		const double WaistYaw = FMath::Clamp(YawShare * TargetRot.Yaw, -WaistYawLimitDeg, WaistYawLimitDeg);
		const FVector NeckTarget = TargetRel.GetLocation() - TargetRel.GetRotation().RotateVector(Mount);
		WaistTarget = TeleopMath::WaistLean(NeckTarget - WaistPivot, WaistYaw);
		WaistTarget.Pitch = FMath::Clamp(WaistTarget.Pitch, -WaistPitchLimitDeg, WaistPitchLimitDeg);
		WaistTarget.Roll = FMath::Clamp(WaistTarget.Roll, -WaistRollLimitDeg, WaistRollLimitDeg);
	}
	{
		double Pitch = WaistRotation.Pitch, Yaw = WaistRotation.Yaw, Roll = WaistRotation.Roll;
		TrackAngle(Yaw, WaistRate.Y, WaistTarget.Yaw, WaistTauSeconds, WaistMaxRateDegPerSec, WaistMaxAccelDegPerSec2, Dt);
		TrackAngle(Pitch, WaistRate.X, WaistTarget.Pitch, WaistTauSeconds, WaistMaxRateDegPerSec, WaistMaxAccelDegPerSec2, Dt);
		TrackAngle(Roll, WaistRate.Z, WaistTarget.Roll, WaistTauSeconds, WaistMaxRateDegPerSec, WaistMaxAccelDegPerSec2, Dt);
		WaistRotation = FRotator(Pitch, Yaw, Roll);
	}

	// Neck, relative to the torso: points the cameras where the head looks, whatever the waist is doing.
	const FRotator NeckTargetRot = (WaistRotation.Quaternion().Inverse() * TargetRel.GetRotation()).Rotator();
	double Pitch = NeckRotation.Pitch, Yaw = NeckRotation.Yaw, Roll = NeckRotation.Roll;
	double PitchRate = NeckRate.X, YawRate = NeckRate.Y, RollRate = NeckRate.Z;

	const bool bRotates = Mode != ETeleopNeckMode::Fixed;
	const bool bRolls = Mode == ETeleopNeckMode::Rot3DoF || Mode == ETeleopNeckMode::Full6DoF;
	TrackAngle(Yaw, YawRate, bRotates ? NeckTargetRot.Yaw : 0.0, TauSeconds, MaxRateDegPerSec, MaxAccelDegPerSec2, Dt);
	TrackAngle(Pitch, PitchRate, bRotates ? NeckTargetRot.Pitch : 0.0, TauSeconds, MaxRateDegPerSec, MaxAccelDegPerSec2, Dt);
	TrackAngle(Roll, RollRate, bRolls ? NeckTargetRot.Roll : 0.0, TauSeconds, MaxRateDegPerSec, MaxAccelDegPerSec2, Dt);

	NeckRotation = FRotator(Pitch, Yaw, Roll);
	NeckRate = FVector(PitchRate, YawRate, RollRate);

	FVector OffsetTarget = FVector::ZeroVector;
	if (Mode == ETeleopNeckMode::Full6DoF)
	{
		// Put the camera rig where the operator's eyes are, within the workspace (torso frame).
		const FVector EyeInTorso = WaistRotation.UnrotateVector(TargetRel.GetLocation() - WaistPivot);
		OffsetTarget = (EyeInTorso - FVector(0.0, 0.0, WaistToNeckCm) - NeckRotation.RotateVector(Mount))
			.BoundToBox(-WorkspaceHalfExtent, WorkspaceHalfExtent);
	}
	FVector Velocity = (OffsetTarget - PivotOffset) / FMath::Max<double>(TauSeconds, 1e-3);
	Velocity = Velocity.GetClampedToMaxSize(MaxSpeedCmPerSec);
	PivotOffset += Velocity * Dt;

	Waist->SetRelativeLocationAndRotation(WaistPivot, WaistRotation);
	NeckPivot->SetRelativeLocationAndRotation(FVector(0.0, 0.0, WaistToNeckCm) + PivotOffset, NeckRotation);
}

void ARobotHead::UpdateCameraRig()
{
	const FVector Mount = TeleopCVars::CamMountOffset.GetValueOnGameThread() != 0 ? MountOffset : FVector::ZeroVector;
	const float HalfBaselineCm = FMath::Clamp(TeleopCVars::BaselineMm.GetValueOnGameThread(), 0.f, 200.f) * 0.05f;
	CameraRig->SetRelativeLocation(Mount);
	CameraLeft->SetRelativeLocation(FVector(0.f, -HalfBaselineCm, 0.f)); // UE: +Y is right
	CameraRight->SetRelativeLocation(FVector(0.f, HalfBaselineCm, 0.f));
}

void ARobotHead::CaptureFrame(double Now)
{
	ATeleopGameMode* GM = GameMode.Get();
	if (!Buffer || !GM)
	{
		return;
	}

	const int32 Slot = Buffer->AcquireWriteSlot(ProtectedSlot);
	if (Slot == INDEX_NONE)
	{
		return;
	}

	// Write straight into the ring-buffer slot (no copy).
	CameraLeft->TextureTarget = Buffer->GetTexture(Slot, 0);
	CameraRight->TextureTarget = Buffer->GetTexture(Slot, 1);
	CameraLeft->CaptureScene();
	CameraRight->CaptureScene();

	const FTeleopLatencyModel::FFrameSample Latency = GM->GetLatencyModel().SampleFrame();

	FTeleopFrameMeta Meta;
	Meta.FrameId = NextFrameId++;
	Meta.CaptureTime = Now;
	Meta.ArrivalTime = Now + Latency.LatencySeconds;
	Meta.bDropped = Latency.bDropped;
	Meta.bBadState = Latency.bBadState;
	Meta.EyeWorld[0] = CameraLeft->GetComponentTransform();
	Meta.EyeWorld[1] = CameraRight->GetComponentTransform();
	Meta.HorizontalFovDeg = HorizontalFovDeg;
	Meta.Width = CaptureWidth;
	Meta.Height = CaptureHeight;
	UpdateDepth(Slot, Meta, Now);
	Buffer->Commit(Slot, Meta);

	if (FTeleopLogger* Logger = GM->GetLogger())
	{
		Logger->LogCapture(Meta, TeleopCVars::LatencyPreset.GetValueOnGameThread());
	}

	++RateWindowCount;
}

void ARobotHead::UpdateDepth(int32 Slot, FTeleopFrameMeta& Meta, double Now)
{
	if (!Degrader)
	{
		return;
	}
	const FTeleopDepthPreset& P = UTeleopDepthDegrader::GetActivePreset();
	const int32 K = FMath::Max(P.K, 1);
	const int32 DW = FMath::Max(CaptureWidth / K, 1);
	const int32 DH = FMath::Max(CaptureHeight / K, 1);
	const bool bResized = DW != Buffer->GetDepthWidth() || DH != Buffer->GetDepthHeight();
	Buffer->EnsureDepthTextures(DW, DH);
	if (bResized)
	{
		LastDepthSlot = INDEX_NONE;
	}

	// Depth can run slower than color (HANDOFF §5.6): then this color frame gets the last (older) depth.
	const bool bDepthDue = P.DepthHz <= 0.f || LastDepthSlot == INDEX_NONE
		|| Now - LastDepthTime >= 1.0 / P.DepthHz - 0.002;
	const float BaselineCm = TeleopCVars::BaselineMm.GetValueOnGameThread() * 0.1f;

	if (bDepthDue)
	{
		for (int32 Eye = 0; Eye < 2; ++Eye)
		{
			Degrader->Degrade(Buffer->GetTexture(Slot, Eye), Buffer->GetDepthTexture(Slot, Eye), Eye, Meta.FrameId, HorizontalFovDeg, BaselineCm);
		}
		LastDepthSlot = Slot;
		LastDepthTime = Now;
		Meta.DepthCaptureTime = Now;
	}
	else
	{
		for (int32 Eye = 0; Eye < 2; ++Eye)
		{
			Degrader->Copy(Buffer->GetDepthTexture(LastDepthSlot, Eye), Buffer->GetDepthTexture(Slot, Eye));
		}
		Meta.DepthCaptureTime = LastDepthTime;
		LastDepthSlot = Slot; // the copy now holds the latest depth (the old slot may be reused)
	}
	Meta.bDepthValid = true;
}
