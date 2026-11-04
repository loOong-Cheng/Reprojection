#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FrameRingBuffer.h"
#include "TeleopTypes.h"
#include "RobotHead.generated.h"

class ATeleopGameMode;
class ATeleopOperatorPawn;
class UFrameRingBuffer;
class USceneCaptureComponent2D;
class UTeleopDepthDegrader;

/**
 * Robot head at the station in the remote world (HANDOFF §3).
 *
 * Neck: tracks the mapped operator head pose H(t - L_cmd) with a first-order low pass (Tau), angular rate and
 * acceleration limits, and, in Full6DoF, a translation speed limit and workspace box. Cameras are mounted
 * MountOffset in front of/above the neck pivot, so pure head rotation also translates them (parallax).
 *
 * Waist (Waist3Neck2, the default): Base -> Waist (yaw, pitch, roll; WaistToNeckCm below the neck) -> 2-DoF neck.
 * The waist leans (pitch/roll, slower than the neck) so the neck pivot follows where the operator's neck is; it
 * moves on a sphere around the waist, so it follows sideways/forward head motion but not height changes. Waist
 * yaw takes teleop.Waist.YawShare of the head yaw. The neck turns relative to the torso; a sideways lean rolls the
 * cameras (the neck has no roll).
 *
 * Cameras: left/right USceneCaptureComponent2D, baseline b, captured manually at f_cam into the ring buffer
 * (RGB = linear scene color, A = view-space depth in cm).
 */
UCLASS()
class REPROJECTION_API ARobotHead : public AActor
{
	GENERATED_BODY()

public:
	ARobotHead();

	void Init(ATeleopGameMode* InGameMode, ATeleopOperatorPawn* InPawn);

	virtual void Tick(float DeltaSeconds) override;

	UFrameRingBuffer* GetBuffer() const { return Buffer; }

	/** Newest committed frame id (-1 before the first capture). */
	int64 GetLastFrameId() const { return NextFrameId - 1; }

	/** Measured capture rate over the last second, Hz. */
	float GetMeasuredCamHz() const { return MeasuredCamHz; }

	/** Slot the presenter is showing; never overwritten. */
	void SetProtectedSlot(int32 Slot) { ProtectedSlot = Slot; }

	/** The robot cameras never render this actor (observer overlays). */
	void HideFromRobotCameras(AActor* Actor);

	// Joints, for the observer view's robot model.
	USceneComponent* GetBase() const { return Base; }
	USceneComponent* GetWaist() const { return Waist; }
	USceneComponent* GetNeckPivot() const { return NeckPivot; }
	USceneComponent* GetCameraRig() const { return CameraRig; }
	USceneCaptureComponent2D* GetCamera(int32 Eye) const { return Eye == 0 ? CameraLeft : CameraRight; }

	UPROPERTY(EditAnywhere, Category = "Teleop|Camera")
	int32 CaptureWidth = 1280;

	UPROPERTY(EditAnywhere, Category = "Teleop|Camera")
	int32 CaptureHeight = 960;

	UPROPERTY(EditAnywhere, Category = "Teleop|Camera")
	float HorizontalFovDeg = 110.f;

	/** Ring buffer slots; must exceed max latency x f_cam (HANDOFF §7.4: 300 ms x 60 Hz = 18 -> 24). */
	UPROPERTY(EditAnywhere, Category = "Teleop|Camera")
	int32 NumSlots = 24;

	/** Camera rig offset from the neck pivot: forward (X) and up (Z), cm (teleop.CamMountOffset toggles it). */
	UPROPERTY(EditAnywhere, Category = "Teleop|Neck")
	FVector MountOffset = FVector(10.f, 0.f, 5.f);

	UPROPERTY(EditAnywhere, Category = "Teleop|Neck")
	float TauSeconds = 0.04f;

	UPROPERTY(EditAnywhere, Category = "Teleop|Neck")
	float MaxRateDegPerSec = 300.f;

	UPROPERTY(EditAnywhere, Category = "Teleop|Neck")
	float MaxAccelDegPerSec2 = 2000.f;

	UPROPERTY(EditAnywhere, Category = "Teleop|Neck")
	float MaxSpeedCmPerSec = 50.f;

	/** Full6DoF workspace half extents around the nominal pivot, cm. */
	UPROPERTY(EditAnywhere, Category = "Teleop|Neck")
	FVector WorkspaceHalfExtent = FVector(25.f, 25.f, 20.f);

	/** Waist pivot to neck pivot along the torso, cm (Waist3Neck2). */
	UPROPERTY(EditAnywhere, Category = "Teleop|Waist")
	float WaistToNeckCm = 55.f;

	UPROPERTY(EditAnywhere, Category = "Teleop|Waist")
	float WaistTauSeconds = 0.15f;

	UPROPERTY(EditAnywhere, Category = "Teleop|Waist")
	float WaistMaxRateDegPerSec = 90.f;

	UPROPERTY(EditAnywhere, Category = "Teleop|Waist")
	float WaistMaxAccelDegPerSec2 = 600.f;

	/** Joint limits, degrees: pitch (lean forward/back), roll (sideways), yaw. */
	UPROPERTY(EditAnywhere, Category = "Teleop|Waist")
	float WaistPitchLimitDeg = 30.f;

	UPROPERTY(EditAnywhere, Category = "Teleop|Waist")
	float WaistRollLimitDeg = 20.f;

	UPROPERTY(EditAnywhere, Category = "Teleop|Waist")
	float WaistYawLimitDeg = 90.f;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Teleop")
	TObjectPtr<USceneComponent> Base;

	/** Waist joint; the neck pivot sits WaistToNeckCm above it along the torso. */
	UPROPERTY(VisibleAnywhere, Category = "Teleop")
	TObjectPtr<USceneComponent> Waist;

	UPROPERTY(VisibleAnywhere, Category = "Teleop")
	TObjectPtr<USceneComponent> NeckPivot;

	UPROPERTY(VisibleAnywhere, Category = "Teleop")
	TObjectPtr<USceneComponent> CameraRig;

	UPROPERTY(VisibleAnywhere, Category = "Teleop")
	TObjectPtr<USceneCaptureComponent2D> CameraLeft;

	UPROPERTY(VisibleAnywhere, Category = "Teleop")
	TObjectPtr<USceneCaptureComponent2D> CameraRight;

private:
	void UpdateNeck(double Now, float DeltaSeconds);
	void UpdateCameraRig();
	void CaptureFrame(double Now);

	TWeakObjectPtr<ATeleopGameMode> GameMode;
	TWeakObjectPtr<ATeleopOperatorPawn> Pawn;

	void UpdateDepth(int32 Slot, FTeleopFrameMeta& Meta, double Now);

	UPROPERTY(Transient)
	TObjectPtr<UFrameRingBuffer> Buffer;

	UPROPERTY(Transient)
	TObjectPtr<UTeleopDepthDegrader> Degrader;

	int32 LastDepthSlot = INDEX_NONE;
	double LastDepthTime = 0.0;

	// Waist state, relative to Base.
	FRotator WaistRotation = FRotator::ZeroRotator;
	FVector WaistRate = FVector::ZeroVector; // deg/s for (pitch, yaw, roll)

	// Neck state, relative to the torso (Waist).
	FRotator NeckRotation = FRotator::ZeroRotator;
	FVector NeckRate = FVector::ZeroVector; // deg/s for (pitch, yaw, roll)
	/** Full6DoF translation of the neck pivot from its nominal position (torso frame). */
	FVector PivotOffset = FVector::ZeroVector;
	bool bNeckInitialized = false;

	double NextCaptureTime = 0.0;
	int64 NextFrameId = 0;
	int32 ProtectedSlot = INDEX_NONE;

	double LastRobotLogTime = 0.0;

	double RateWindowStart = 0.0;
	int32 RateWindowCount = 0;
	float MeasuredCamHz = 0.f;
};
