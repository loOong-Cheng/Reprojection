#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "TeleopDepthDegrader.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTextureRenderTarget2D;

/** Depth preset (HANDOFF §5 table). */
struct FTeleopDepthPreset
{
	const TCHAR* Name;
	int32 K;             // depth resolution = color resolution / K
	float SigmaDispPx;   // disparity noise std, px
	float PHole;         // fraction of blocky random holes
	int32 EdgeHolePx;    // invalid pixels around depth discontinuities
	float DepthHz;       // 0 = every color frame
	bool bQuantize;      // disparity quantized to 1/8 px
	float ZMinCm;
	float ZMaxCm;
	int32 FattenPx;      // foreground edge fattening (optional artifact)
};

/**
 * Builds the degraded depth of a captured frame on the GPU (two material passes, DrawMaterialToRenderTarget):
 * block min/max (scratch), then noise / quantization / range / holes / edge effects into the frame's depth target.
 * Uses the true scene depth only to generate the degraded depth (HANDOFF §6.1).
 */
UCLASS()
class REPROJECTION_API UTeleopDepthDegrader : public UObject
{
	GENERATED_BODY()

public:
	static constexpr int32 NumPresets = 4;
	static const FTeleopDepthPreset& GetPreset(int32 Index);

	/** Current preset (teleop.DepthPreset). */
	static const FTeleopDepthPreset& GetActivePreset();

	void Initialize(UObject* InWorldContext);

	/** Degrade the alpha (scene depth) of Source into Dest (R32f, size = source / K). */
	void Degrade(UTextureRenderTarget2D* Source, UTextureRenderTarget2D* Dest, int32 Eye, int64 FrameId,
		float HorizontalFovDeg, float BaselineCm);

	/** Copy a degraded depth (used when the depth frame rate is lower than the color frame rate). */
	void Copy(UTextureRenderTarget2D* Source, UTextureRenderTarget2D* Dest);

private:
	UTextureRenderTarget2D* GetScratch(int32 Eye, int32 Width, int32 Height);

	TWeakObjectPtr<UObject> WorldContext;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MinMaxMID[2];
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DegradeMID[2];
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CopyMID;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> Scratch[2];
};
