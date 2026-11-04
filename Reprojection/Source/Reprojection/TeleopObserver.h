#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TeleopObserver.generated.h"

class ARobotHead;
class ATeleopGameMode;
class ATeleopOperatorPawn;
class ATeleopPresenter;
class ULineBatchComponent;
class UMaterialInstanceDynamic;
class USceneCaptureComponent2D;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UTextureRenderTarget2D;

/**
 * Third-person observer view of the remote world for the PC window (recording, debugging). The HMD is unchanged; in VR
 * the spectator screen shows a split screen: this view left, the operator's eye view right (teleop.Observer).
 *
 * It shows:
 *   - the real robot: a grey body on the robot's actual joints (pedestal, torso on the waist, head on the neck, two
 *     camera lenses), the robot camera frustum now (orange) and the capture pose of the frame the operator is seeing
 *     right now (yellow); the real robot hands are the ARemoteHand actors themselves;
 *   - the operator, undelayed (T_map * pose(t)): HMD, view frustum and Template hands with the current fingers, as green
 *     ghosts; white lines join the operator's eye to the robot camera and each ghost hand to its robot hand.
 * All of it renders only in scene captures and is hidden from the robot cameras, so neither the operator nor the
 * robot sees it. Presets: teleop.Observer.View, key V.
 */
UCLASS()
class REPROJECTION_API ATeleopObserver : public AActor
{
	GENERATED_BODY()

public:
	ATeleopObserver();

	void Init(ATeleopGameMode* InGameMode, ATeleopOperatorPawn* InPawn, ARobotHead* InRobotHead, ATeleopPresenter* InPresenter);

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Next camera preset (key V). */
	void CycleView();

	UFUNCTION(BlueprintPure, Category = "Teleop")
	UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }

	/** Updates the overlays and captures now, also without an HMD (tests export the render target afterwards). */
	UFUNCTION(BlueprintCallable, Category = "Teleop|Debug")
	void DebugCaptureNow();

protected:
	UPROPERTY(VisibleAnywhere, Category = "Teleop")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Teleop")
	TObjectPtr<USceneCaptureComponent2D> Capture;

	UPROPERTY(VisibleAnywhere, Category = "Teleop")
	TObjectPtr<ULineBatchComponent> Lines;

	// The real robot, attached to its joints.
	UPROPERTY(VisibleAnywhere, Category = "Teleop|Robot") TObjectPtr<UStaticMeshComponent> Pedestal;
	UPROPERTY(VisibleAnywhere, Category = "Teleop|Robot") TObjectPtr<UStaticMeshComponent> Torso;
	UPROPERTY(VisibleAnywhere, Category = "Teleop|Robot") TObjectPtr<UStaticMeshComponent> Neck;
	UPROPERTY(VisibleAnywhere, Category = "Teleop|Robot") TObjectPtr<UStaticMeshComponent> Head;
	UPROPERTY(VisibleAnywhere, Category = "Teleop|Robot") TObjectPtr<UStaticMeshComponent> LensLeft;
	UPROPERTY(VisibleAnywhere, Category = "Teleop|Robot") TObjectPtr<UStaticMeshComponent> LensRight;

	// The operator, undelayed.
	UPROPERTY(VisibleAnywhere, Category = "Teleop|Operator") TObjectPtr<UStaticMeshComponent> GhostHmd;
	UPROPERTY(VisibleAnywhere, Category = "Teleop|Operator") TObjectPtr<USceneComponent> GhostGripLeft;
	UPROPERTY(VisibleAnywhere, Category = "Teleop|Operator") TObjectPtr<USceneComponent> GhostGripRight;
	UPROPERTY(VisibleAnywhere, Category = "Teleop|Operator") TObjectPtr<USkeletalMeshComponent> GhostHandLeft;
	UPROPERTY(VisibleAnywhere, Category = "Teleop|Operator") TObjectPtr<USkeletalMeshComponent> GhostHandRight;

	// Text in front of the observer camera: view name + the operator HUD (top left), colour legend (bottom left).
	UPROPERTY(VisibleAnywhere, Category = "Teleop|Text") TObjectPtr<UTextRenderComponent> InfoText;
	UPROPERTY(VisibleAnywhere, Category = "Teleop|Text") TArray<TObjectPtr<UTextRenderComponent>> LegendText;
	/** Translucent dark backing behind the info and legend text. */
	UPROPERTY(VisibleAnywhere, Category = "Teleop|Text") TObjectPtr<UStaticMeshComponent> InfoPanel;
	UPROPERTY(VisibleAnywhere, Category = "Teleop|Text") TObjectPtr<UStaticMeshComponent> LegendPanel;

	UPROPERTY(EditAnywhere, Category = "Teleop")
	float FovDeg = 70.f;

	/** Operator view frustum on the ghost head (one Quest 3 eye, roughly). */
	UPROPERTY(EditAnywhere, Category = "Teleop")
	float OperatorFovDeg = 100.f;

	UPROPERTY(EditAnywhere, Category = "Teleop")
	float FrustumLengthCm = 35.f;

	UPROPERTY(EditAnywhere, Category = "Teleop")
	float LineThicknessCm = 0.6f;

	/** Render target size without a known PC window (desktop tests). */
	UPROPERTY(EditAnywhere, Category = "Teleop")
	FIntPoint DefaultResolution = FIntPoint(960, 1080);

	/** Render target width limit (cost); the window region is matched in aspect, not in pixels, above this. */
	UPROPERTY(EditAnywhere, Category = "Teleop")
	int32 MaxWidth = 1600;

private:
	void UpdateRobotModel();
	void UpdateGhosts();
	void DrawOverlays();
	void UpdateCamera();
	void UpdateText();
	void UpdateSpectatorScreen(int32 Mode);
	FIntPoint DesiredResolution(int32 Mode) const;
	void DrawFrustum(const FTransform& Camera, float HorizontalFovDeg, float Aspect, const FLinearColor& Color);

	TWeakObjectPtr<ATeleopGameMode> GameMode;
	TWeakObjectPtr<ATeleopOperatorPawn> Pawn;
	TWeakObjectPtr<ARobotHead> RobotHead;
	TWeakObjectPtr<ATeleopPresenter> Presenter;

	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> RenderTarget;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RobotMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> LensMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GhostMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PanelMID;

	FTransform GhostEye;
	bool bHasGhostEye = false;

	int32 AppliedMode = -1;
	float AppliedSplit = -1.f;
	double LastCaptureTime = 0.0;
};
