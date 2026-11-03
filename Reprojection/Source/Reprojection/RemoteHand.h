#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TeleopTypes.h"
#include "RemoteHand.generated.h"

class ATeleopGameMode;
class ATeleopOperatorPawn;
class UMotionControllerComponent;
class UStaticMeshComponent;

/**
 * Proxy hand in the remote world (HANDOFF §1 change 2, S1b). Follows T_map * controller pose (t - L_cmd).
 * Grab/release commands are delayed by the same command latency and then run the Template's own grab logic:
 * find the nearest BP_GrabComponent within GrabRadius and call its TryGrab(MotionController) / TryRelease().
 * The operator only sees the result through the delayed robot camera, so interaction latency comes for free.
 *
 * GripPoint is a UMotionControllerComponent because TryGrab expects one; it is deactivated (never tracks a
 * device) and simply follows this actor.
 */
UCLASS()
class REPROJECTION_API ARemoteHand : public AActor
{
	GENERATED_BODY()

public:
	ARemoteHand();

	void Init(bool bInLeft, ATeleopGameMode* InGameMode, ATeleopOperatorPawn* InPawn);

	virtual void Tick(float DeltaSeconds) override;

	/** Operator pressed/released the grab button at IssueTime; executed at IssueTime + L_cmd. */
	void QueueGrab(bool bPressed, double IssueTime);

	// --- Test hooks (used from Python in PIE) ---

	/** Drive the hand from a world pose instead of the operator's controller. */
	UFUNCTION(BlueprintCallable, Category = "Teleop|Debug")
	void DebugSetPoseOverride(const FTransform& WorldPose);

	UFUNCTION(BlueprintCallable, Category = "Teleop|Debug")
	void DebugClearPoseOverride();

	/** Grab / release immediately (no command latency). Returns true if something was grabbed / released. */
	UFUNCTION(BlueprintCallable, Category = "Teleop|Debug")
	bool DebugGrab();

	UFUNCTION(BlueprintCallable, Category = "Teleop|Debug")
	bool DebugRelease();

	UFUNCTION(BlueprintPure, Category = "Teleop")
	AActor* GetHeldActor() const;

	UFUNCTION(BlueprintPure, Category = "Teleop")
	bool IsLeft() const { return bLeft; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Teleop")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Teleop")
	TObjectPtr<UMotionControllerComponent> GripPoint;

	/** Placeholder robot gripper, visible to the robot cameras. */
	UPROPERTY(VisibleAnywhere, Category = "Teleop")
	TObjectPtr<UStaticMeshComponent> Visual;

	/** Same as the Template pawn's GrabRadiusFromGripPosition. */
	UPROPERTY(EditAnywhere, Category = "Teleop")
	float GrabRadius = 6.f;

	/** Window for the release velocity estimate (throwing), seconds. */
	UPROPERTY(EditAnywhere, Category = "Teleop")
	float ThrowVelocityWindow = 0.05f;

	UPROPERTY(EditAnywhere, Category = "Teleop")
	TSoftClassPtr<USceneComponent> GrabComponentClass;

private:
	bool DoGrab();
	bool DoRelease();
	USceneComponent* FindNearestGrabComponent() const;

	bool bLeft = false;
	TWeakObjectPtr<ATeleopGameMode> GameMode;
	TWeakObjectPtr<ATeleopOperatorPawn> Pawn;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> HeldComponent;

	UPROPERTY(Transient)
	TObjectPtr<UClass> LoadedGrabClass;

	struct FPendingCommand
	{
		double ExecuteTime;
		bool bPressed;
	};
	TArray<FPendingCommand> PendingCommands;

	/** Own world pose stream, for the throw velocity. */
	FTeleopPoseHistory WorldHistory;

	bool bHasPoseOverride = false;
	FTransform PoseOverride;
};
