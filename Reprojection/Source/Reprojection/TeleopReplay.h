#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Tasks/Task.h"
#include "TeleopTypes.h"

class ATeleopGameMode;
class UWorld;

/** One sample of an operator trajectory, in the tracking frame (cm). Head = HMD centre pose. */
struct FTeleopTrajectorySample
{
	double Time = 0.0;
	FTransform Head;
	FTransform Hand[2]; // [0] = left
	FTeleopFingerState Fingers[2];
};

/**
 * Operator trajectory as CSV, one row per sample, columns (header names, any order):
 *   t (s); hx hy hz hpitch hyaw hroll; lx ly lz lpitch lyaw lroll; rx ry rz rpitch ryaw rroll (cm, deg);
 *   lgrasp lcurl lpoint lthumb rgrasp rcurl rpoint rthumb (0..1). Lines starting with # are comments.
 * Tools/replay_trajectory.py writes one; teleop.Replay.Record records one live.
 */
class REPROJECTION_API FTeleopTrajectory
{
public:
	bool LoadCsv(const FString& Path, FString& OutError);
	bool SaveCsv(const FString& Path) const;

	void Add(const FTeleopTrajectorySample& Sample) { Samples.Add(Sample); }
	void Reset() { Samples.Reset(); }
	int32 Num() const { return Samples.Num(); }
	double Duration() const { return Samples.Num() ? Samples.Last().Time : 0.0; }

	/** Interpolated (positions and fingers linear, rotations slerp), clamped to the first / last sample. */
	FTeleopTrajectorySample Sample(double Time) const;

private:
	TArray<FTeleopTrajectorySample> Samples;
};

/**
 * Deterministic replay of an operator trajectory, to compare the methods on exactly the same motion (user request
 * 2026-11-05: GIFs of every method with the same head and hand trajectories, temporal metrics). Desktop only (the
 * desktop view renders from the trajectory's head; in VR the headset decides the view). Standalone run, see
 * Tools/replay_run.ps1:
 *   -UseFixedTimeStep -FPS=<hz>        required: TeleopClock counts frames, so every run sees the same times
 *   -TeleopReplay=<trajectory.csv>     play it: head, hands, fingers; a grip crossing 0.5 grabs / releases
 *   -TeleopSet="cvar=value;..."        console variables applied before the session starts (method, latency, ...)
 *   -TeleopReplayOut=<dir>             write eye_%05d.png (game viewport, every frame from -TeleopReplayFrom=<s>),
 *                                      obs_%05d.png (observer view, every -TeleopReplayObsEvery=<n> frames; 0 = none),
 *                                      frames.csv (per frame: time, shown frame, its age, eye - camera offset, R)
 * The process exits when the trajectory ends. teleop.Replay.Record <file.csv> / teleop.Replay.Stop record one live.
 */
class REPROJECTION_API FTeleopReplay
{
public:
	~FTeleopReplay();

	/** At InitGame, before any actor begins play: parse the command line, start the fixed-step clock. */
	void Init(ATeleopGameMode* InGameMode);
	void Shutdown();

	/** Current trajectory sample; false unless a trajectory is playing. */
	bool GetSample(FTeleopTrajectorySample& Out) const;
	bool IsPlaying() const { return bPlaying; }

	/** Recording (any mode, VR included): the pawn adds one sample per tick while active. */
	void StartRecording(const FString& Path);
	void StopRecording();
	bool IsRecording() const { return !RecordPath.IsEmpty(); }
	void AddRecordedSample(const FTeleopTrajectorySample& Sample);

private:
	void OnPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void OnScreenshot(int32 Width, int32 Height, const TArray<FColor>& Bitmap);
	void CaptureObserver(int32 FrameIndex);
	void WriteFrameRow(int32 FrameIndex, double T);
	void LogScene() const;
	void Finish();
	void QueueWrite(FString Path, int32 Width, int32 Height, TArray<FColor> Pixels);

	TWeakObjectPtr<ATeleopGameMode> GameMode;
	FTeleopTrajectory Trajectory;
	bool bPlaying = false;
	bool bFinished = false;
	bool bSceneLogged = false;
	double StartTime = 0.0;

	FString OutDir;
	double OutFrom = 0.0;
	int32 ObsEvery = 0;
	int32 PendingEyeFrame = INDEX_NONE;
	int32 FramesWritten = 0;
	TUniquePtr<FArchive> FramesCsv;
	TArray<UE::Tasks::FTask> Writes;
	FDelegateHandle PostTickHandle;
	FDelegateHandle ScreenshotHandle;

	FTeleopTrajectory Recording;
	FString RecordPath;
	double RecordStart = 0.0;
};
