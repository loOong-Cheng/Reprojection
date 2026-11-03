#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TeleopTypes.h"
#include "TeleopPresenter.generated.h"

class ARobotHead;
class ATeleopGameMode;
class ATeleopOperatorPawn;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/** Frame-age statistics over the last stats window (HANDOFF §8 HUD). */
USTRUCT(BlueprintType)
struct FTeleopDisplayStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Teleop") float AgeP50Ms = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Teleop") float AgeP95Ms = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Teleop") float AgeMinMs = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Teleop") float AgeMaxMs = 0.f;
	/** New robot frames shown per second. */
	UPROPERTY(BlueprintReadOnly, Category = "Teleop") float ShownFps = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Teleop") int32 Samples = 0;
};

/**
 * Picks the frame to show and drives the presentation (HANDOFF §2, §6):
 * newest frame with A_i <= now that was not dropped; an older frame never replaces a newer one.
 * M0 is a post-process material on the operator camera; GT moves the operator into the remote world.
 */
UCLASS()
class REPROJECTION_API ATeleopPresenter : public AActor
{
	GENERATED_BODY()

public:
	ATeleopPresenter();

	void Init(ATeleopGameMode* InGameMode, ATeleopOperatorPawn* InPawn, ARobotHead* InRobotHead);

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "Teleop")
	FTeleopDisplayStats GetLastStats() const { return LastStats; }

	/** Age of the frame shown this tick (now - C_shown), ms; negative if none. */
	UFUNCTION(BlueprintPure, Category = "Teleop")
	float GetCurrentFrameAgeMs() const { return CurrentAgeMs; }

	UFUNCTION(BlueprintPure, Category = "Teleop")
	ETeleopMethod GetActiveMethod() const { return ActiveMethod; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, Category = "Teleop")
	TSoftObjectPtr<UMaterialInterface> PresentMaterial;

	UPROPERTY(EditAnywhere, Category = "Teleop")
	float StatsWindowSeconds = 1.f;

private:
	void ApplyMethod(ETeleopMethod Method);
	void UpdateStats(double Now);

	TWeakObjectPtr<ATeleopGameMode> GameMode;
	TWeakObjectPtr<ATeleopOperatorPawn> Pawn;
	TWeakObjectPtr<ARobotHead> RobotHead;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PresentMID;

	ETeleopMethod ActiveMethod = ETeleopMethod::M0_Naive;
	bool bMethodApplied = false;

	int32 ShownSlot = INDEX_NONE;
	int64 ShownFrameId = -1;
	double ShownCaptureTime = 0.0;
	float CurrentAgeMs = -1.f;

	TArray<float> WindowAgesMs;
	int32 WindowNewFrames = 0;
	double WindowStart = 0.0;
	FTeleopDisplayStats LastStats;
};
