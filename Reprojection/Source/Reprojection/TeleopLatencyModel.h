#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

/** One latency preset (HANDOFF §4 table). Times in ms, probabilities per frame. */
struct FTeleopLatencyPreset
{
	const TCHAR* Name;
	float BaseGoodMs;
	float BaseBadMs;
	float JitterStdMs;   // stationary std of the AR(1) jitter
	float PGoodToBad;
	float PBadToGood;
	float LossGood;
	float LossBad;
};

/**
 * End-to-end (glass-to-glass) latency per camera frame and frame loss (HANDOFF §4):
 *   L_i = L_exp + L_enc,i + L_net,i + L_dec + L_disp
 * L_net: two-state Gilbert-Elliott chain (good / bad base latency, per-state loss) plus AR(1) correlated jitter
 * j_i = rho j_{i-1} + eps_i, eps_i ~ LogNormal - mean (slow drift with occasional spikes).
 * The command channel (operator -> robot) uses teleop.CmdLatencyMs as base plus a smaller correlated jitter.
 * Preset 5 (Constant) = teleop.LatencyMs exactly, no loss (for tests).
 */
class FTeleopLatencyModel
{
public:
	struct FFrameSample
	{
		double LatencySeconds = 0.0;
		bool bDropped = false;
		bool bBadState = false;
	};

	static constexpr int32 NumNetworkPresets = 5;
	static constexpr int32 ConstantPreset = 5;
	static const FTeleopLatencyPreset& GetPreset(int32 Index);
	static FString GetPresetName(int32 Index);

	// Fixed pipeline components (HANDOFF §4), ms.
	static constexpr float ExposureMs = 8.f;
	static constexpr float EncodeBaseMs = 5.f;
	static constexpr float EncodeJitterMs = 3.f; // U(0, 3)
	static constexpr float DecodeMs = 3.f;
	static constexpr float DisplayMs = 11.f;

	static constexpr double Rho = 0.9;           // jitter correlation per frame
	static constexpr double LogNormalSigma = 0.8; // jitter shape (heavier tail = rarer, bigger spikes)

	explicit FTeleopLatencyModel(int32 Seed = 0x7E1E0);

	/** Uses the current teleop.LatencyPreset / teleop.LatencyBaseOffsetMs. Call once per captured camera frame. */
	FFrameSample SampleFrame();

	/** Same, for an explicit preset and offset (used by teleop.LatencyTest). */
	FFrameSample SampleFrame(int32 PresetIndex, float BaseOffsetMs);

	/** Advance the command-channel jitter; call once per game tick. */
	void TickCommandChannel(float DeltaSeconds);

	/** Current command latency (operator -> robot), seconds. */
	double GetCommandLatencySeconds() const;

	bool IsBadState() const { return bBad; }

private:
	double SampleJitterInnovation(double StdMs);

	FRandomStream Rng;
	bool bBad = false;
	double JitterMs = 0.0;
	int32 LastPreset = INDEX_NONE;

	double CmdJitterMs = 0.0;
};
