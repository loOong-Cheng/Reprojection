#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TeleopBlindTest.h"
#include "TeleopTypes.h"
#include "TeleopPresenter.generated.h"

class ARobotHead;
class ATeleopGameMode;
class ATeleopOperatorPawn;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class UTextureRenderTarget2D;
struct FTeleopFrameMeta;

/** Frame-age statistics over the last stats window (HANDOFF §8 HUD). */
USTRUCT(BlueprintType)
struct FTeleopDisplayStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Teleop") float AgeP50Ms = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Teleop") float AgeP95Ms = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Teleop") float AgeMinMs = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Teleop") float AgeMaxMs = 0.f;
	/** New robot frames shown per second. */
	UPROPERTY(BlueprintReadOnly, Category = "Teleop") float ShownFps = 0.f;
	/** End-to-end latency of the frames that arrived in the window (receiver side). */
	UPROPERTY(BlueprintReadOnly, Category = "Teleop") float LatencyP50Ms = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Teleop") float LatencyP95Ms = 0.f;
	/** Fraction of captured frames lost in the window. */
	UPROPERTY(BlueprintReadOnly, Category = "Teleop") float LossRate = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Teleop") int32 Samples = 0;
};

/**
 * Presentation (HANDOFF §2, §6): picks the frame to show (newest with A_i <= now, or with the jitter buffer
 * max(A_i, C_i + D_target); never older than the frame shown), and renders it with the active method:
 *   M0 head-locked, M1/M2 sphere (post process, per pixel ray-sphere), M3 depth mesh (grid displaced in the
 *   vertex shader), M4 = sphere background + mesh (custom stencil 2), GT = operator moved into the remote world.
 * Compensation uses only what a real system has: the frame, its capture pose and the degraded depth (§6.1).
 * Also: M2 adaptive radius (async GPU readback of the shown frame's central depth), HUD, CSV logging,
 * blind test (§8) and GT screenshot pairs (teleop.ScreenshotPair).
 */
UCLASS()
class REPROJECTION_API ATeleopPresenter : public AActor
{
	GENERATED_BODY()

public:
	ATeleopPresenter();

	void Init(ATeleopGameMode* InGameMode, ATeleopOperatorPawn* InPawn, ARobotHead* InRobotHead);

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintPure, Category = "Teleop")
	FTeleopDisplayStats GetLastStats() const { return LastStats; }

	/** Age of the frame shown this tick (now - C_shown), ms; negative if none. */
	UFUNCTION(BlueprintPure, Category = "Teleop")
	float GetCurrentFrameAgeMs() const { return CurrentAgeMs; }

	UFUNCTION(BlueprintPure, Category = "Teleop")
	ETeleopMethod GetActiveMethod() const { return ActiveMethod; }

	/** Capture pose of the frame shown right now (left / right robot camera, world). False before the first frame. */
	bool GetShownFramePose(FTransform& OutLeft, FTransform& OutRight) const;

	/** Current M2 adaptive radius, cm. */
	UFUNCTION(BlueprintPure, Category = "Teleop")
	float GetAdaptiveRadiusCm() const { return AdaptiveRadiusCm; }

	// --- Operator controls (HANDOFF §8) ---
	void CycleMethod();
	void CycleLatencyPreset();
	void AdjustBaseLatency(int32 Direction);
	void ToggleJitterBuffer();
	void CycleDepthPreset();
	void CycleNeckMode();
	void AdjustSphereRadius(int32 Direction);
	void AdjustBaseline(int32 Direction);
	void ToggleBlindTest();
	void AdjustRating(int32 Direction);
	bool IsBlindTestActive() const { return BlindTest.IsActive(); }

	/** Screenshot of the current method, then of GT (same pose), into the log folder. */
	void RequestScreenshotPair();

	FTeleopBlindTest& GetBlindTest() { return BlindTest; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, Category = "Teleop")
	TSoftObjectPtr<UMaterialInterface> PresentMaterial;

	UPROPERTY(EditAnywhere, Category = "Teleop")
	TSoftObjectPtr<UMaterialInterface> DepthMeshMaterial;

	UPROPERTY(EditAnywhere, Category = "Teleop")
	float StatsWindowSeconds = 1.f;

	/** Max M3 grid resolution (vertices); the grid follows the depth resolution up to this. */
	UPROPERTY(EditAnywhere, Category = "Teleop")
	FIntPoint MaxGridSize = FIntPoint(640, 480);

	/** Custom-stencil value of the M3 mesh (M4 shows the sphere everywhere else). */
	static constexpr int32 DepthMeshStencilValue = 2;

private:
	ETeleopMethod ResolveRequestedMethod() const;
	void ApplyMethod(ETeleopMethod Method);
	void SelectFrame(double Now);
	void UpdateMaterialParams();
	void SetFrameParams(UMaterialInstanceDynamic* MID, const FTeleopFrameMeta& Meta, int32 Slot);
	void EnsureDepthMesh(int32 DepthWidth, int32 DepthHeight);
	void UpdateAdaptiveRadius(double Now, float DeltaSeconds);
	void UpdateScreenshotPair();
	void UpdateStats(double Now);
	void LogDisplay(double Now);
	FString MethodLabel() const;
	bool UsesAdaptiveRadius(ETeleopMethod Method) const;

	TWeakObjectPtr<ATeleopGameMode> GameMode;
	TWeakObjectPtr<ATeleopOperatorPawn> Pawn;
	TWeakObjectPtr<ARobotHead> RobotHead;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PresentMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MeshMID;
	UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> DepthMesh;
	FIntPoint DepthMeshGrid = FIntPoint::ZeroValue;

	ETeleopMethod ActiveMethod = ETeleopMethod::M0_Naive;
	bool bMethodApplied = false;

	int32 ShownSlot = INDEX_NONE;
	int64 ShownFrameId = -1;
	double ShownCaptureTime = 0.0;
	float CurrentAgeMs = -1.f;

	// Receiver-side latency of arrived frames (for the jitter buffer target and the HUD).
	TArray<TPair<double, float>> ArrivalLatencies; // (arrival time, latency ms), last ~2 s
	TSet<int64> CountedArrivals;
	float JitterBufferTargetMs = 0.f;

	// M2 adaptive radius.
	struct FRadiusReadback;
	TSharedPtr<FRadiusReadback, ESPMode::ThreadSafe> Readback;
	float AdaptiveRadiusCm = 100.f;
	float AdaptiveTargetCm = 100.f;
	int64 ReadbackFrameId = -1;
	/** (time, per-frame median depth cm) of the last teleop.M2.WindowSec; the target is their median. */
	TArray<TPair<double, float>> RadiusSamples;

	// Stats window.
	TArray<float> WindowAgesMs;
	int32 WindowNewFrames = 0;
	double WindowStart = 0.0;
	TArray<float> WindowGameMs, WindowRenderMs, WindowRhiMs, WindowGpuMs;
	int32 WindowTicks = 0;
	FTeleopDisplayStats LastStats;
	double SessionStart = 0.0;
	bool bProfiledGPU = false;

	FTeleopBlindTest BlindTest;

	// Screenshot pair state machine.
	int32 PairPhase = 0;
	int32 PairWait = 0;
	int32 PairIndex = 0;
	bool bPairForceGT = false;
};
