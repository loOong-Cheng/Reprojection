#pragma once

#include "CoreMinimal.h"

struct FTeleopFrameMeta;

/** One row per presenter tick (what the operator saw). */
struct FTeleopDisplayRow
{
	double Time = 0.0;
	int64 ShownFrameId = -1;
	double ShownCaptureTime = 0.0;
	float AgeMs = -1.f;
	FString Method;
	FString LatencyPreset;
	float BaseOffsetMs = 0.f;
	bool bJitterBuffer = false;
	float JitterBufferTargetMs = 0.f;
	FString DepthPreset;
	float SphereRadiusCm = 0.f;
	float BaselineMm = 0.f;
	int32 NeckMode = 0;
	float CmdLatencyMs = 0.f;
	bool bBlind = false;
	FString BlindCondition;
};

/**
 * Per-session CSV logs (HANDOFF §7.8): Saved/TeleopLogs/<yyyymmdd-hhmmss>/
 *   capture.csv  one row per captured robot frame: id, C_i, L_i, A_i, dropped, bad state
 *   display.csv  one row per displayed operator frame: shown frame, frame age, method and presets
 *   blind.csv    blind-test ratings (written by FTeleopBlindTest)
 * Timestamps are TeleopClock::Now() (wall clock, or frame time in a fixed-step replay). Enabled by teleop.Log (default 1).
 */
class FTeleopLogger
{
public:
	FTeleopLogger();
	~FTeleopLogger();

	const FString& GetSessionDir() const { return SessionDir; }

	void LogCapture(const FTeleopFrameMeta& Meta, int32 LatencyPresetIndex);
	void LogDisplay(const FTeleopDisplayRow& Row);
	void LogBlind(const FString& CsvLine);

	/** Flush all files (called about once per second). */
	void Flush();

private:
	FArchive* Open(TUniquePtr<FArchive>& File, const TCHAR* Name, const TCHAR* Header);
	static void WriteLine(FArchive* File, const FString& Line);

	FString SessionDir;
	TUniquePtr<FArchive> CaptureFile;
	TUniquePtr<FArchive> DisplayFile;
	TUniquePtr<FArchive> BlindFile;
};
