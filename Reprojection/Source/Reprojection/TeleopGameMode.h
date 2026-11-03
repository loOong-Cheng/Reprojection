#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TeleopGameMode.generated.h"

class ATeleopOperatorRoom;

/**
 * Spawns the operator (ATeleopOperatorPawn) in the dark operator room instead of at the Template's
 * PlayerStart. The Template map stays the remote world; if it has no operator room placed, one is
 * spawned at OperatorRoomLocation.
 */
UCLASS()
class REPROJECTION_API ATeleopGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATeleopGameMode();

	virtual AActor* FindPlayerStart_Implementation(AController* Player, const FString& IncomingName) override;

	/** Where the operator room is spawned when the level has none, cm (about 1 km from the Template scene). */
	UPROPERTY(EditDefaultsOnly, Category = "Teleop")
	FVector OperatorRoomLocation = FVector(-100000.f, 0.f, 0.f);

private:
	ATeleopOperatorRoom* FindOrSpawnOperatorRoom();

	UPROPERTY(Transient)
	TObjectPtr<ATeleopOperatorRoom> OperatorRoom;
};
