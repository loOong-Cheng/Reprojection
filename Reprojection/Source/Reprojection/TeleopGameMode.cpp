#include "TeleopGameMode.h"

#include "Reprojection.h"
#include "TeleopOperatorPawn.h"
#include "TeleopOperatorRoom.h"

#include "Engine/World.h"
#include "EngineUtils.h"

ATeleopGameMode::ATeleopGameMode()
{
	DefaultPawnClass = ATeleopOperatorPawn::StaticClass();
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
