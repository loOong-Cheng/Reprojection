#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FrameRingBuffer.generated.h"

class UTextureRenderTarget2D;

/** Metadata of one captured stereo frame (HANDOFF §2). */
USTRUCT(BlueprintType)
struct FTeleopFrameMeta
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Teleop")
	int64 FrameId = -1;

	/** C_i, FPlatformTime::Seconds() when CaptureScene() was issued. */
	UPROPERTY(BlueprintReadOnly, Category = "Teleop")
	double CaptureTime = 0.0;

	/** A_i = C_i + L_i. */
	UPROPERTY(BlueprintReadOnly, Category = "Teleop")
	double ArrivalTime = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Teleop")
	bool bDropped = false;

	/** World transforms of the left [0] and right [1] robot cameras at capture (camera looks along +X). */
	UPROPERTY()
	FTransform EyeWorld[2];

	UPROPERTY(BlueprintReadOnly, Category = "Teleop")
	float HorizontalFovDeg = 110.f;

	UPROPERTY(BlueprintReadOnly, Category = "Teleop")
	int32 Width = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Teleop")
	int32 Height = 0;

	bool IsValid() const { return FrameId >= 0; }
};

/**
 * Pool of render targets for the robot camera frames (HANDOFF §2, §7.4): NumSlots x 2 eyes, RGBA16f,
 * RGB = scene color (linear HDR), A = scene depth (view-space z, cm). Captures write straight into the
 * next free slot, so there are no copies; slots are reused oldest-first.
 */
UCLASS()
class REPROJECTION_API UFrameRingBuffer : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(int32 InNumSlots, int32 InWidth, int32 InHeight);

	/** Oldest slot that is not ProtectedSlot (the one currently displayed). */
	int32 AcquireWriteSlot(int32 ProtectedSlot) const;

	void Commit(int32 Slot, const FTeleopFrameMeta& Meta);

	/** Slot of the newest frame (largest CaptureTime) that has arrived by Now and was not dropped; INDEX_NONE if none. */
	int32 FindNewestArrived(double Now) const;

	UTextureRenderTarget2D* GetTexture(int32 Slot, int32 Eye) const;
	const FTeleopFrameMeta& GetMeta(int32 Slot) const { return Metas[Slot]; }
	int32 NumSlots() const { return Metas.Num(); }

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> Textures; // index = Slot * 2 + Eye

	TArray<FTeleopFrameMeta> Metas;
};
