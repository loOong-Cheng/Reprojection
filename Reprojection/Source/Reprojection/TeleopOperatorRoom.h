#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TeleopOperatorRoom.generated.h"

class UMaterialInterface;
class UStaticMeshComponent;

/**
 * Closed, dark, unlit box the operator sits in (HANDOFF §1, S1), far away from the Template scene.
 * Fully closed so nothing of the remote world (sky, fog, geometry) is ever seen directly.
 *
 * The actor origin is the seat on the floor; the operator faces +X. ATeleopGameMode spawns the
 * operator pawn here, and spawns a room at its default location if the level has none.
 */
UCLASS()
class REPROJECTION_API ATeleopOperatorRoom : public AActor
{
	GENERATED_BODY()

public:
	ATeleopOperatorRoom();

	virtual void OnConstruction(const FTransform& Transform) override;

	/** Inner size of the room (X depth, Y width, Z height), cm. The seat is at the floor center. */
	UPROPERTY(EditAnywhere, Category = "Teleop")
	FVector InnerSize = FVector(400.f, 400.f, 300.f);

	UPROPERTY(EditAnywhere, Category = "Teleop", meta = (ClampMin = "1.0"))
	float WallThickness = 20.f;

	UPROPERTY(EditAnywhere, Category = "Teleop")
	FLinearColor WallColor = FLinearColor(0.015f, 0.015f, 0.02f);

	/** Unlit material with a vector parameter "Color". */
	UPROPERTY(EditAnywhere, Category = "Teleop")
	TSoftObjectPtr<UMaterialInterface> WallMaterial;

private:
	void LayoutWalls();

	UPROPERTY(VisibleAnywhere, Category = "Teleop")
	TObjectPtr<USceneComponent> Seat;

	/** Floor, ceiling, front (+X), back (-X), right (+Y), left (-Y). */
	UPROPERTY(VisibleAnywhere, Category = "Teleop")
	TArray<TObjectPtr<UStaticMeshComponent>> Panels;
};
