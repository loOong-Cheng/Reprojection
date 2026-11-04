#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TeleopTypes.h"
#include "RemoteHand.generated.h"

class ATeleopGameMode;
class ATeleopOperatorPawn;
class UAnimInstance;
class UMaterialInterface;
class UMotionControllerComponent;
class USkeletalMesh;
class USkeletalMeshComponent;

/**
 * Robot hand in the remote world (HANDOFF §1 change 2, S1b). Follows T_map * controller pose (t - L_cmd).
 * Grab/release commands are delayed by the same command latency and then run the Template's own grab logic:
 * find the nearest BP_GrabComponent within GrabRadius and call its TryGrab(MotionController) / TryRelease().
 * The operator only sees the result through the delayed robot camera, so interaction latency comes for free.
 *
 * Looks like the VR Template's hand (SKM_MannyXR + ABP_MannequinsXR, same offset from the grip as in BP_XRPawn,
 * left = mirrored). The fingers play the operator's grip / trigger / touch inputs, delayed by L_cmd; a held object
 * keeps the hand closed.
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

	/** The grip point (what TryGrab holds on to); follows the delayed controller pose. */
	USceneComponent* GetGripPoint() const;

	/**
	 * Makes Mesh a VR Template hand (mesh, animation, BP_XRPawn's offset from the grip; asset paths from this class's
	 * defaults). Material overrides the Template material if given. False if the assets are missing.
	 */
	static bool ConfigureTemplateHand(USkeletalMeshComponent* Mesh, bool bLeft, UMaterialInterface* Material = nullptr);

	/** Plays finger controls on a Template hand's anim instance. False if it lacks the ABP_MannequinsXR variables. */
	static bool ApplyFingers(UAnimInstance* Anim, const FTeleopFingerState& Fingers, bool bLeft);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Teleop")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Teleop")
	TObjectPtr<UMotionControllerComponent> GripPoint;

	/** The robot hand the robot cameras see (VR Template hand). */
	UPROPERTY(VisibleAnywhere, Category = "Teleop")
	TObjectPtr<USkeletalMeshComponent> HandMesh;

	UPROPERTY(EditAnywhere, Category = "Teleop|Hand")
	TSoftObjectPtr<USkeletalMesh> LeftHandMesh;

	UPROPERTY(EditAnywhere, Category = "Teleop|Hand")
	TSoftObjectPtr<USkeletalMesh> RightHandMesh;

	UPROPERTY(EditAnywhere, Category = "Teleop|Hand")
	TSoftClassPtr<UAnimInstance> HandAnimClass;

	UPROPERTY(EditAnywhere, Category = "Teleop|Hand")
	TSoftObjectPtr<UMaterialInterface> HandMaterial;

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
	USceneComponent* FindNearestGrabComponent(float Radius) const;
	void SetupHandMesh();
	/** Plays the operator's finger controls at SampleTime on the hand; returns what was applied. */
	FTeleopFingerState UpdateFingers(double SampleTime);

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

	double LastHandLogTime = 0.0;
	bool bWarnedAnimVars = false;
};
