#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "TeleopTypes.h"
#include "TeleopOperatorPawn.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UMaterialInterface;
class UMotionControllerComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * The operator (HANDOFF §1): seated, HMD + two controllers, no locomotion. Sits in the dark operator room
 * and never sees the remote world directly, except in GT mode, where the whole pawn is moved onto the
 * robot station so the HMD renders the remote world at the current (mapped) eye pose.
 *
 * Every tick it records the HMD and controller poses in its own tracking frame (relative to VROrigin);
 * the robot head and proxy hands read these streams delayed by the command latency.
 *
 * Dev controls (until the HANDOFF §8 layout): right A / left X = cycle method, grips = grab (proxy hands),
 * left menu or R = recenter. Desktop (no HMD): teleop.Debug.HeadYawAmpDeg gives a synthetic head sweep.
 */
UCLASS()
class REPROJECTION_API ATeleopOperatorPawn : public APawn
{
	GENERATED_BODY()

public:
	ATeleopOperatorPawn();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** Pose stream of a device in the pawn's tracking frame (VROrigin space). */
	const FTeleopPoseHistory& GetLocalHistory(ETeleopDevice Device) const { return Histories[static_cast<int32>(Device)]; }

	/** Latest pose of a device in the tracking frame. False until the device has a valid pose. */
	bool GetLatestLocalPose(ETeleopDevice Device, FTransform& OutPose) const;

	/** GT: put the tracking origin at RemoteOrigin (the local->remote mapping); otherwise back in the room. */
	void SetRemoteView(bool bRemote, const FTransform& RemoteOrigin);

	/** Adds/removes a post-process material on the HMD camera. */
	void SetCameraBlendable(UMaterialInterface* Material, bool bEnabled);

	void SetHudText(const FString& Text);

	UCameraComponent* GetCamera() const { return Camera; }

	/** Custom-stencil value of things in the room the presentation post process must not cover (HUD). */
	static constexpr int32 HudStencilValue = 1;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleop")
	TObjectPtr<USceneComponent> VROrigin;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleop")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleop")
	TObjectPtr<UMotionControllerComponent> LeftController;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleop")
	TObjectPtr<UMotionControllerComponent> RightController;

	/** World-locked status text below the view (HANDOFF §8). Rendered with custom stencil = HudStencilValue. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleop")
	TObjectPtr<UTextRenderComponent> HudText;

	/** Panel 1.5 m in front of the seat for the vertex-stage eye test (teleop.EyeTest 2). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleop|Debug")
	TObjectPtr<UStaticMeshComponent> EyeTestPanel;

	/** Eye height above the floor when no HMD is active (desktop PIE), cm. */
	UPROPERTY(EditAnywhere, Category = "Teleop")
	float DesktopEyeHeight = 120.f;

	/** Controller rest poses (tracking frame) when controllers are not tracked (desktop). */
	UPROPERTY(EditAnywhere, Category = "Teleop")
	FVector DesktopLeftHand = FVector(35.f, -20.f, 85.f);

	UPROPERTY(EditAnywhere, Category = "Teleop")
	FVector DesktopRightHand = FVector(35.f, 20.f, 85.f);

	UPROPERTY(EditAnywhere, Category = "Teleop|Debug")
	TSoftObjectPtr<UMaterialInterface> EyeTestPostProcessMaterial;

	UPROPERTY(EditAnywhere, Category = "Teleop|Debug")
	TSoftObjectPtr<UMaterialInterface> EyeTestVertexMaterial;

private:
	void RecordPoses(double Now);
	void ApplyEyeTestMode(int32 Mode);

	void OnCycleMethod();
	void OnRecenter();
	void OnGrabLeftPressed() { ForwardGrab(true, true); }
	void OnGrabLeftReleased() { ForwardGrab(true, false); }
	void OnGrabRightPressed() { ForwardGrab(false, true); }
	void OnGrabRightReleased() { ForwardGrab(false, false); }
	void ForwardGrab(bool bLeft, bool bPressed);

	FTeleopPoseHistory Histories[static_cast<int32>(ETeleopDevice::Num)];
	bool bHasPose[static_cast<int32>(ETeleopDevice::Num)] = { false, false, false };

	FTransform RoomTransform;
	bool bRemoteView = false;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LoadedEyeTestPostProcess;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> Actions;

	int32 AppliedEyeTestMode = INDEX_NONE;
};
