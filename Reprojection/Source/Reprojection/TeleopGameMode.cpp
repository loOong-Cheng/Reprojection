#include "TeleopGameMode.h"

#include "Reprojection.h"
#include "RemoteHand.h"
#include "RobotHead.h"
#include "TeleopOperatorPawn.h"
#include "TeleopOperatorRoom.h"
#include "TeleopPresenter.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "HAL/IConsoleManager.h"

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

	UE_LOG(LogTeleop, Log, TEXT("Session: robot station at %s yaw %.1f"), *RobotStationLocation.ToString(), RobotStationYaw);
}

void ATeleopGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bCalibrated && OperatorPawn)
	{
		FTransform Head;
		if (OperatorPawn->GetLatestLocalPose(ETeleopDevice::Head, Head))
		{
			Recenter();
		}
	}
}

void ATeleopGameMode::Recenter()
{
	FTransform Head;
	if (!OperatorPawn || !OperatorPawn->GetLatestLocalPose(ETeleopDevice::Head, Head))
	{
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
}

void ATeleopGameMode::HandleGrabInput(bool bLeft, bool bPressed)
{
	if (ARemoteHand* Hand = GetRemoteHand(bLeft))
	{
		Hand->QueueGrab(bPressed, FPlatformTime::Seconds());
	}
}
