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

	/** C_i, TeleopClock::Now() when CaptureScene() was issued. */
	UPROPERTY(BlueprintReadOnly, Category = "Teleop")
	double CaptureTime = 0.0;

	/** A_i = C_i + L_i. */
	UPROPERTY(BlueprintReadOnly, Category = "Teleop")
	double ArrivalTime = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Teleop")
	bool bDropped = false;

	UPROPERTY(BlueprintReadOnly, Category = "Teleop")
	bool bBadState = false;

	/** World transforms of the left [0] and right [1] robot cameras at capture (camera looks along +X). */
	UPROPERTY()
	FTransform EyeWorld[2];

	UPROPERTY(BlueprintReadOnly, Category = "Teleop")
	float HorizontalFovDeg = 110.f;

	UPROPERTY(BlueprintReadOnly, Category = "Teleop")
	int32 Width = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Teleop")
	int32 Height = 0;

	/** The slot's degraded depth is valid (false right after a depth preset change). */
	UPROPERTY(BlueprintReadOnly, Category = "Teleop")
	bool bDepthValid = false;

	/** Capture time of the color frame the degraded depth was computed from (older if depth runs slower). */
	UPROPERTY(BlueprintReadOnly, Category = "Teleop")
	double DepthCaptureTime = 0.0;

	bool IsValid() const { return FrameId >= 0; }
};

/**
 * Pool of render targets for the robot camera frames (HANDOFF §2, §7.4): NumSlots x 2 eyes.
 *  - color: RGBA16f, RGB = scene color (linear HDR), A = scene depth (view-space z, cm) -- ground truth
 *  - depth: R32f at color resolution / K, the degraded depth (HANDOFF §5), 0 = invalid
 * Captures write straight into the next free slot (no copies); slots are reused oldest-first.
 */
UCLASS()
class REPROJECTION_API UFrameRingBuffer : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(int32 InNumSlots, int32 InWidth, int32 InHeight);

	/** (Re)allocate the degraded-depth targets at Width x Height; invalidates stored depth if the size changes. */
	void EnsureDepthTextures(int32 InDepthWidth, int32 InDepthHeight);

	/** Oldest slot that is not ProtectedSlot (the one currently displayed). */
	int32 AcquireWriteSlot(int32 ProtectedSlot) const;

	void Commit(int32 Slot, const FTeleopFrameMeta& Meta);

	/** Slot of the newest frame (largest CaptureTime) that has arrived by Now and was not dropped. If
	 *  JitterBufferDelay > 0, a frame is only eligible at max(A_i, C_i + JitterBufferDelay). INDEX_NONE if none. */
	int32 FindNewestArrived(double Now, double JitterBufferDelay = 0.0) const;

	UTextureRenderTarget2D* GetTexture(int32 Slot, int32 Eye) const;
	UTextureRenderTarget2D* GetDepthTexture(int32 Slot, int32 Eye) const;
	const FTeleopFrameMeta& GetMeta(int32 Slot) const { return Metas[Slot]; }
	FTeleopFrameMeta& GetMetaMutable(int32 Slot) { return Metas[Slot]; }
	int32 NumSlots() const { return Metas.Num(); }
	int32 GetDepthWidth() const { return DepthWidth; }
	int32 GetDepthHeight() const { return DepthHeight; }

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> Textures; // index = Slot * 2 + Eye

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> DepthTextures; // index = Slot * 2 + Eye

	TArray<FTeleopFrameMeta> Metas;
	int32 DepthWidth = 0;
	int32 DepthHeight = 0;
};
