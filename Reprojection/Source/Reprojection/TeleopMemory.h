#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "TeleopMemory.generated.h"

class UFrameRingBuffer;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class USceneComponent;
class UTextureRenderTarget2D;

/**
 * M4 hole fill 2: memory of earlier robot frames (user design, 2026-11-04).
 *
 * Keeps up to teleop.Memory.Keyframes keyframes from the frames shown to the operator: colour and degraded depth at
 * depth resolution plus the left robot camera's capture pose. A frame becomes a keyframe when the camera has moved or
 * turned enough since every stored keyframe (teleop.Memory.MinMoveCm / MinTurnDeg); the oldest is replaced. A
 * keyframe keeps depth only on the background side of depth edges (teleop.Memory.BandPx), since holes only open there.
 *
 * Each keyframe is drawn as a depth mesh in the operator room, behind the current M3 mesh (pushed back along the eye
 * ray) and with the mesh's custom stencil, so the presentation post process lets it through. Points the current frame's
 * camera sees past are dropped (something moved). Uses only what a real system has (HANDOFF §6.1).
 */
UCLASS()
class REPROJECTION_API UTeleopMemory : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UObject* InWorldContext, USceneComponent* InAttachTo, int32 InStencilValue);

	struct FDrawParams
	{
		FTransform Map;       // operator tracking frame -> remote world
		FVector RoomSeat;     // room origin of the tracking frame
		float DispScale = 0.f;
		float EdgeDispPx = 3.f;
	};

	/** Takes the shown frame as a keyframe if it is new enough, then updates the keyframe meshes (shown only if bDraw). */
	void Update(double Now, UFrameRingBuffer* Buffer, int32 ShownSlot, bool bDraw, const FDrawParams& Params);

	void Clear();
	int32 NumKeyframes() const;

	/** Per keyframe: age, camera position and the share of texels the edge band keeps. Reads the GPU back (slow). */
	FString Describe(double Now);

private:
	struct FKeyframe
	{
		bool bValid = false;
		FTransform Pose;
		FVector2f Tan = FVector2f(1.f, 1.f);
		double Time = 0.0;
		int64 FrameId = -1;
		float BandPx = 0.f;
	};

	void EnsureTargets(int32 Width, int32 Height, int32 Count);
	void EnsureMeshes(int32 GridX, int32 GridY);
	bool IsNovel(const FTransform& Pose) const;
	void Insert(UFrameRingBuffer* Buffer, int32 Slot, double Now, const FDrawParams& Params);
	void HideAll();

	TWeakObjectPtr<UObject> WorldContext;
	TWeakObjectPtr<USceneComponent> AttachTo;
	int32 StencilValue = 2;

	UPROPERTY(Transient) TArray<TObjectPtr<UTextureRenderTarget2D>> Colors;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextureRenderTarget2D>> Depths;
	UPROPERTY(Transient) TArray<TObjectPtr<UProceduralMeshComponent>> Meshes;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> MeshMIDs;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ColorMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> EdgeMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MaxFilterMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DepthMID;
	/** Edge-band scratch: [0] background-side edge disparity, [1] its horizontal max. */
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> Scratch[2];
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> MeshMaterial;

	TArray<FKeyframe> Keys;
	FIntPoint TargetSize = FIntPoint::ZeroValue;
	FIntPoint MeshGrid = FIntPoint::ZeroValue;
	int64 LastConsideredFrameId = -1;
	double LastInsertTime = -1.0;
	bool bShown = false;
};
