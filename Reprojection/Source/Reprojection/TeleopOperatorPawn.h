#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "TeleopOperatorPawn.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UMaterialInterface;
class UMotionControllerComponent;
class UStaticMeshComponent;

/**
 * The operator (HANDOFF §1): seated, HMD + two controllers, no locomotion (no teleport, no turning).
 * Lives in the dark operator room and never sees the remote (Template) world directly; its HMD and
 * controller poses are the inputs to the robot-head and proxy-hand mapping.
 *
 * S1 debug: teleop.EyeTest (or right controller A / keyboard T) cycles the per-eye check (HANDOFF §7.1):
 *   1 = full-screen post process, left red / right green  -> eye index valid in the pixel shader
 *   2 = panel colored in the vertex shader, same colors    -> eye index valid in the vertex shader (WPO)
 */
UCLASS()
class REPROJECTION_API ATeleopOperatorPawn : public APawn
{
	GENERATED_BODY()

public:
	ATeleopOperatorPawn();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UCameraComponent* GetCamera() const { return Camera; }
	UMotionControllerComponent* GetLeftController() const { return LeftController; }
	UMotionControllerComponent* GetRightController() const { return RightController; }

protected:
	virtual void BeginPlay() override;

	/** Tracking-space origin, on the room floor at the seat. Never moves. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleop")
	TObjectPtr<USceneComponent> VROrigin;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleop")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleop")
	TObjectPtr<UMotionControllerComponent> LeftController;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleop")
	TObjectPtr<UMotionControllerComponent> RightController;

	/** Panel 1.5 m in front of the seat for the vertex-stage eye test (teleop.EyeTest 2). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleop|Debug")
	TObjectPtr<UStaticMeshComponent> EyeTestPanel;

	/** Eye height above the floor when no HMD is active (desktop PIE), cm. */
	UPROPERTY(EditAnywhere, Category = "Teleop")
	float DesktopEyeHeight = 120.f;

	/** Post-process material: left eye red, right eye green (pixel stage). */
	UPROPERTY(EditAnywhere, Category = "Teleop|Debug")
	TSoftObjectPtr<UMaterialInterface> EyeTestPostProcessMaterial;

	/** Unlit surface material whose color is computed in the vertex shader (vertex stage). */
	UPROPERTY(EditAnywhere, Category = "Teleop|Debug")
	TSoftObjectPtr<UMaterialInterface> EyeTestVertexMaterial;

private:
	void ApplyEyeTestMode(int32 Mode);
	void CycleEyeTestMode();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LoadedEyeTestPostProcess;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> DebugMappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CycleEyeTestAction;

	int32 AppliedEyeTestMode = INDEX_NONE;
};
