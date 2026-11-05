#include "TeleopGameMode.h"

#include "Reprojection.h"
#include "RemoteHand.h"
#include "RobotHead.h"
#include "TeleopCVars.h"
#include "TeleopClock.h"
#include "TeleopObserver.h"
#include "TeleopOperatorPawn.h"
#include "TeleopOperatorRoom.h"
#include "TeleopPresenter.h"
#include "TeleopProps.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CoreDelegates.h"

static FAutoConsoleCommandWithWorld GTeleopRecenterCommand(
	TEXT("teleop.Recenter"),
	TEXT("Map the operator's current head yaw/position onto the robot station."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (ATeleopGameMode* GM = World ? World->GetAuthGameMode<ATeleopGameMode>() : nullptr)
		{
			GM->Recenter();
		}
	}));

ATeleopGameMode::ATeleopGameMode()
{
	DefaultPawnClass = ATeleopOperatorPawn::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
}

void ATeleopGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	Replay.Init(this);
}

AActor* ATeleopGameMode::FindPlayerStart_Implementation(AController* Player, const FString& IncomingName)
{
	// Always the operator room, also over PIE's "play from camera" start spot: the operator must
	// never start inside the remote world.
	if (ATeleopOperatorRoom* Room = FindOrSpawnOperatorRoom())
	{
		return Room;
	}
	return Super::FindPlayerStart_Implementation(Player, IncomingName);
}

ATeleopOperatorRoom* ATeleopGameMode::FindOrSpawnOperatorRoom()
{
	if (OperatorRoom)
	{
		return OperatorRoom;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ATeleopOperatorRoom> It(World); It; ++It)
	{
		OperatorRoom = *It;
		return OperatorRoom;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	OperatorRoom = World->SpawnActor<ATeleopOperatorRoom>(OperatorRoomLocation, FRotator::ZeroRotator, Params);
	UE_LOG(LogTeleop, Log, TEXT("Spawned operator room at %s"), *OperatorRoomLocation.ToString());
	return OperatorRoom;
}

void ATeleopGameMode::RestartPlayer(AController* NewPlayer)
{
	Super::RestartPlayer(NewPlayer);
	if (ATeleopOperatorPawn* Pawn = NewPlayer ? Cast<ATeleopOperatorPawn>(NewPlayer->GetPawn()) : nullptr)
	{
		SetupSession(Pawn);
	}
}

void ATeleopGameMode::SetupSession(ATeleopOperatorPawn* Pawn)
{
	if (OperatorPawn)
	{
		return;
	}
	OperatorPawn = Pawn;

	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FTransform Station = GetStationTransform();

	Logger = MakeUnique<FTeleopLogger>();
	Props = World->SpawnActor<ATeleopProps>(ATeleopProps::StaticClass(), Station, Params);

	LeftHand = World->SpawnActor<ARemoteHand>(ARemoteHand::StaticClass(), Station, Params);
	RightHand = World->SpawnActor<ARemoteHand>(ARemoteHand::StaticClass(), Station, Params);
	LeftHand->Init(true, this, Pawn);
	RightHand->Init(false, this, Pawn);

	RobotHead = World->SpawnActor<ARobotHead>(ARobotHead::StaticClass(), Station, Params);
	RobotHead->Init(this, Pawn);
	RobotHead->AddTickPrerequisiteActor(LeftHand); // capture after the hands (and what they hold) moved
	RobotHead->AddTickPrerequisiteActor(RightHand);

	Presenter = World->SpawnActor<ATeleopPresenter>(ATeleopPresenter::StaticClass(), Station, Params);
	Presenter->Init(this, Pawn, RobotHead);

	Observer = World->SpawnActor<ATeleopObserver>(ATeleopObserver::StaticClass(), FTransform::Identity, Params);
	Observer->Init(this, Pawn, RobotHead, Presenter);

	UE_LOG(LogTeleop, Log, TEXT("Session: robot station at %s yaw %.1f"), *RobotStationLocation.ToString(), RobotStationYaw);
}

FTransform ATeleopGameMode::GetLocalToRemote() const
{
	// Debug offset in the calibrated station frame, where the calibrated head is at (0, 0, eye height): the yaw turns
	// about the head's vertical axis.
	const FTransform DebugOffset(FRotator(0.f, TeleopCVars::DebugMapYawDeg.GetValueOnGameThread(), 0.f),
		FVector(TeleopCVars::DebugMapOffsetX.GetValueOnGameThread(), TeleopCVars::DebugMapOffsetY.GetValueOnGameThread(),
			TeleopCVars::DebugMapOffsetZ.GetValueOnGameThread()));
	return Calibration * DebugOffset * GetStationTransform();
}

void ATeleopGameMode::BeginPlay()
{
	Super::BeginPlay();
	// Headset put on (it may have been lying on the desk at start) or system recenter (Oculus button held: the
	// tracking space moved, so the old calibration is wrong): calibrate again on the next valid pose.
	PutOnHeadHandle = FCoreDelegates::VRHeadsetPutOnHead.AddUObject(this, &ATeleopGameMode::RearmAutoRecenter, TEXT("headset put on"));
	SystemRecenterHandle = FCoreDelegates::VRHeadsetRecenter.AddUObject(this, &ATeleopGameMode::RearmAutoRecenter, TEXT("system recenter"));
}

void ATeleopGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FCoreDelegates::VRHeadsetPutOnHead.Remove(PutOnHeadHandle);
	FCoreDelegates::VRHeadsetRecenter.Remove(SystemRecenterHandle);
	Replay.Shutdown();
	Super::EndPlay(EndPlayReason);
}

void ATeleopGameMode::RearmAutoRecenter(const TCHAR* Reason)
{
	UE_LOG(LogTeleop, Log, TEXT("Auto recenter re-armed (%s)"), Reason);
	AutoRecenter.Rearm();
}

void ATeleopGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	LatencyModel.TickCommandChannel(DeltaSeconds);
	if (OperatorPawn && AutoRecenter.GetState() != FTeleopAutoRecenter::EState::Done)
	{
		FTransform Head;
		const bool bHave = OperatorPawn->GetLatestLocalPose(ETeleopDevice::Head, Head);
		if (AutoRecenter.Update(TeleopClock::Now(), bHave && OperatorPawn->IsHeadTracked(), Head))
		{
			Recenter();
		}
	}
}

FString ATeleopGameMode::GetCalibrationWarning() const
{
	if (bCalibrated)
	{
		return FString();
	}
	if (AutoRecenter.GetState() == FTeleopAutoRecenter::EState::BelowMinHeight)
	{
		return FString::Printf(TEXT("NOT CALIBRATED: head below %.0f cm (tracking floor?). Right stick click recenters anyway."),
			AutoRecenter.MinEyeHeightCm);
	}
	return TEXT("NOT CALIBRATED: waiting for head tracking (put the headset on)");
}

void ATeleopGameMode::Recenter()
{
	FTransform Head;
	if (!OperatorPawn || !OperatorPawn->GetLatestLocalPose(ETeleopDevice::Head, Head) || !OperatorPawn->IsHeadTracked())
	{
		UE_LOG(LogTeleop, Warning, TEXT("Recenter ignored: head is not tracked"));
		return;
	}
	// Reference frame: head yaw only, head floor position (z = 0). Remote = local * inverse(Ref) * Station.
	const FVector HeadLocation = Head.GetLocation();
	const FTransform Ref(FRotator(0.f, Head.Rotator().Yaw, 0.f), FVector(HeadLocation.X, HeadLocation.Y, 0.f));
	Calibration = Ref.Inverse();
	CalibratedEyeHeight = HeadLocation.Z;
	bCalibrated = true;
	UE_LOG(LogTeleop, Log, TEXT("Recentered: head %s yaw %.1f -> eye height %.1f cm at station"),
		*HeadLocation.ToString(), Head.Rotator().Yaw, CalibratedEyeHeight);
	if (CalibratedEyeHeight < AutoRecenter.MinEyeHeightCm)
	{
		UE_LOG(LogTeleop, Warning, TEXT("Eye height %.1f cm is implausibly low: is the tracking origin at the floor?"), CalibratedEyeHeight);
	}
}

void ATeleopGameMode::HandleGrabInput(bool bLeft, bool bPressed)
{
	if (ARemoteHand* Hand = GetRemoteHand(bLeft))
	{
		Hand->QueueGrab(bPressed, TeleopClock::Now());
	}
}
