#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TeleopProps.generated.h"

class UStaticMeshComponent;

/**
 * Near-field props added to the Template scene (HANDOFF §1 change 3), placed relative to the robot station:
 * a checkerboard standing on the desk (parallax / S5 translation-error measurement), thin rods (they expose
 * reprojection and depth errors first), a sliding block and a spinning fan (scene-motion latency that no
 * method can compensate).
 */
UCLASS()
class REPROJECTION_API ATeleopProps : public AActor
{
	GENERATED_BODY()

public:
	ATeleopProps();

	virtual void Tick(float DeltaSeconds) override;

	/** Checkerboard center, relative to the station (cm). It faces the station (-X). */
	UPROPERTY(EditAnywhere, Category = "Teleop")
	FVector CheckerLocation = FVector(85.f, 0.f, 89.f);

	UPROPERTY(EditAnywhere, Category = "Teleop")
	float CheckerSizeCm = 30.f;

	UPROPERTY(EditAnywhere, Category = "Teleop")
	float SlideAmplitudeCm = 40.f;

	UPROPERTY(EditAnywhere, Category = "Teleop")
	float SlidePeriodSec = 3.f;

	UPROPERTY(EditAnywhere, Category = "Teleop")
	float FanDegPerSec = 180.f;

	/** World location of the checkerboard center (for tests). */
	UFUNCTION(BlueprintPure, Category = "Teleop")
	FVector GetCheckerWorldLocation() const;

protected:
	virtual void BeginPlay() override;

private:
	UStaticMeshComponent* MakeMesh(const TCHAR* Name, UStaticMesh* Mesh, const FVector& Location, const FVector& Scale, USceneComponent* Parent);

	UPROPERTY(VisibleAnywhere, Category = "Teleop") TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, Category = "Teleop") TObjectPtr<UStaticMeshComponent> Checker;
	UPROPERTY(VisibleAnywhere, Category = "Teleop") TArray<TObjectPtr<UStaticMeshComponent>> Rods;
	UPROPERTY(VisibleAnywhere, Category = "Teleop") TObjectPtr<UStaticMeshComponent> SlidingBlock;
	UPROPERTY(VisibleAnywhere, Category = "Teleop") TObjectPtr<USceneComponent> FanHub;
	UPROPERTY(VisibleAnywhere, Category = "Teleop") TArray<TObjectPtr<UStaticMeshComponent>> FanBlades;

	FVector SlideCenter = FVector(115.f, 0.f, 95.f);
	double StartTime = 0.0;
};
