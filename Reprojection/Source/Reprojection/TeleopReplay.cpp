#include "TeleopReplay.h"

#include "Reprojection.h"
#include "RemoteHand.h"
#include "TeleopClock.h"
#include "TeleopGameMode.h"
#include "TeleopMemory.h"
#include "TeleopObserver.h"
#include "TeleopOperatorPawn.h"
#include "TeleopPresenter.h"

#include "Algo/BinarySearch.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"
#include "UnrealClient.h"

namespace
{
	// CSV columns: per pose x, y, z (cm), pitch, yaw, roll (deg); per hand grasp, curl, point, thumb.
	const TCHAR* const PosePrefix[3] = { TEXT("h"), TEXT("l"), TEXT("r") };
	const TCHAR* const PoseField[6] = { TEXT("x"), TEXT("y"), TEXT("z"), TEXT("pitch"), TEXT("yaw"), TEXT("roll") };
	const TCHAR* const FingerField[4] = { TEXT("grasp"), TEXT("curl"), TEXT("point"), TEXT("thumb") };

	FTransform& PoseOf(FTeleopTrajectorySample& S, int32 i) { return i == 0 ? S.Head : S.Hand[i - 1]; }
	const FTransform& PoseOf(const FTeleopTrajectorySample& S, int32 i) { return i == 0 ? S.Head : S.Hand[i - 1]; }
	float& FingerOf(FTeleopFingerState& F, int32 i) { return i == 0 ? F.Grasp : i == 1 ? F.IndexCurl : i == 2 ? F.Point : F.ThumbUp; }
	float FingerOf(const FTeleopFingerState& F, int32 i) { return i == 0 ? F.Grasp : i == 1 ? F.IndexCurl : i == 2 ? F.Point : F.ThumbUp; }

	FTransform LerpPose(const FTransform& A, const FTransform& B, double Alpha)
	{
		return FTransform(FQuat::Slerp(A.GetRotation(), B.GetRotation(), Alpha), FMath::Lerp(A.GetLocation(), B.GetLocation(), Alpha));
	}

	FString ResolveReplayPath(const FString& Path)
	{
		return FPaths::IsRelative(Path) ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TeleopReplay"), Path) : Path;
	}

	FTeleopReplay* FindReplay(UWorld* World)
	{
		ATeleopGameMode* GM = World ? World->GetAuthGameMode<ATeleopGameMode>() : nullptr;
		return GM ? &GM->GetReplay() : nullptr;
	}

	/** "name=value;name value;..." -> console variables (before the session starts, so the whole run uses them). */
	void ApplyCVarList(const FString& List)
	{
		TArray<FString> Items;
		List.ParseIntoArray(Items, TEXT(";"));
		for (FString Item : Items)
		{
			Item.TrimStartAndEndInline();
			FString Name, Value;
			if (!Item.Split(TEXT("="), &Name, &Value) && !Item.Split(TEXT(" "), &Name, &Value))
			{
				continue;
			}
			Name.TrimStartAndEndInline();
			Value.TrimStartAndEndInline();
			if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name))
			{
				CVar->Set(*Value, ECVF_SetByConsole);
				UE_LOG(LogTeleop, Log, TEXT("[replay] %s = %s"), *Name, *CVar->GetString());
			}
			else
			{
				UE_LOG(LogTeleop, Warning, TEXT("[replay] unknown console variable %s"), *Name);
			}
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTeleopReplayRecord(TEXT("teleop.Replay.Record"),
	TEXT("teleop.Replay.Record <file.csv>: record the operator's head, hands and fingers (tracking frame) every tick until ")
	TEXT("teleop.Replay.Stop (works in VR). Relative paths go to Saved/TeleopReplay/. Replay: -TeleopReplay=<file> (FTeleopReplay)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (FTeleopReplay* R = FindReplay(World))
		{
			R->StartRecording(Args.Num() > 0 ? Args[0] : FString::Printf(TEXT("rec_%s.csv"), *FDateTime::Now().ToString()));
		}
	}));

static FAutoConsoleCommandWithWorld GTeleopReplayStop(TEXT("teleop.Replay.Stop"),
	TEXT("Stop recording (teleop.Replay.Record) and write the file."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { if (FTeleopReplay* R = FindReplay(World)) { R->StopRecording(); } }));

// ---------------------------------------------------------------------------------------------------------------
// Trajectory

bool FTeleopTrajectory::LoadCsv(const FString& Path, FString& OutError)
{
	Samples.Reset();
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
	{
		OutError = FString::Printf(TEXT("cannot read %s"), *Path);
		return false;
	}
	TMap<FString, int32> Column;
	for (const FString& RawLine : Lines)
	{
		const FString Line = RawLine.TrimStartAndEnd();
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
		{
			continue;
		}
		TArray<FString> Cells;
		Line.ParseIntoArray(Cells, TEXT(","), false);
		if (Column.Num() == 0)
		{
			for (int32 i = 0; i < Cells.Num(); ++i)
			{
				Column.Add(Cells[i].TrimStartAndEnd().ToLower(), i);
			}
			if (!Column.Contains(TEXT("t")))
			{
				OutError = TEXT("no 't' column in the header");
				return false;
			}
			continue;
		}
		auto Get = [&](const FString& Name, double Default = 0.0)
		{
			const int32* Index = Column.Find(Name);
			return Index && Cells.IsValidIndex(*Index) ? FCString::Atod(*Cells[*Index]) : Default;
		};
		FTeleopTrajectorySample S;
		S.Time = Get(TEXT("t"));
		for (int32 p = 0; p < 3; ++p)
		{
			double V[6];
			for (int32 f = 0; f < 6; ++f)
			{
				V[f] = Get(FString(PosePrefix[p]) + PoseField[f]);
			}
			PoseOf(S, p) = FTransform(FRotator(V[3], V[4], V[5]), FVector(V[0], V[1], V[2]));
		}
		for (int32 h = 0; h < 2; ++h)
		{
			for (int32 f = 0; f < 4; ++f)
			{
				FingerOf(S.Fingers[h], f) = float(Get(FString(PosePrefix[h + 1]) + FingerField[f]));
			}
		}
		if (Samples.Num() > 0 && S.Time <= Samples.Last().Time)
		{
			OutError = FString::Printf(TEXT("time not increasing at t=%.4f"), S.Time);
			return false;
		}
		Samples.Add(S);
	}
	if (Samples.Num() == 0)
	{
		OutError = TEXT("no samples");
		return false;
	}
	return true;
}

bool FTeleopTrajectory::SaveCsv(const FString& Path) const
{
	FString Out = TEXT("# teleop operator trajectory (tracking frame, cm / deg; head = HMD centre)\nt");
	for (int32 p = 0; p < 3; ++p)
	{
		for (int32 f = 0; f < 6; ++f)
		{
			Out += FString(TEXT(",")) + PosePrefix[p] + PoseField[f];
		}
	}
	for (int32 h = 0; h < 2; ++h)
	{
		for (int32 f = 0; f < 4; ++f)
		{
			Out += FString(TEXT(",")) + PosePrefix[h + 1] + FingerField[f];
		}
	}
	Out += TEXT("\n");
	for (const FTeleopTrajectorySample& S : Samples)
	{
		Out += FString::Printf(TEXT("%.4f"), S.Time);
		for (int32 p = 0; p < 3; ++p)
		{
			const FVector L = PoseOf(S, p).GetLocation();
			const FRotator R = PoseOf(S, p).Rotator();
			Out += FString::Printf(TEXT(",%.3f,%.3f,%.3f,%.3f,%.3f,%.3f"), L.X, L.Y, L.Z, R.Pitch, R.Yaw, R.Roll);
		}
		for (int32 h = 0; h < 2; ++h)
		{
			for (int32 f = 0; f < 4; ++f)
			{
				Out += FString::Printf(TEXT(",%.3f"), FingerOf(S.Fingers[h], f));
			}
		}
		Out += TEXT("\n");
	}
	return FFileHelper::SaveStringToFile(Out, *Path);
}

FTeleopTrajectorySample FTeleopTrajectory::Sample(double Time) const
{
	if (Samples.Num() == 0)
	{
		return FTeleopTrajectorySample();
	}
	const int32 Upper = Algo::UpperBoundBy(Samples, Time, &FTeleopTrajectorySample::Time);
	if (Upper <= 0)
	{
		return Samples[0];
	}
	if (Upper >= Samples.Num())
	{
		return Samples.Last();
	}
	const FTeleopTrajectorySample& A = Samples[Upper - 1];
	const FTeleopTrajectorySample& B = Samples[Upper];
	const double Alpha = (Time - A.Time) / FMath::Max(B.Time - A.Time, 1e-9);
	FTeleopTrajectorySample S;
	S.Time = Time;
	for (int32 p = 0; p < 3; ++p)
	{
		PoseOf(S, p) = LerpPose(PoseOf(A, p), PoseOf(B, p), Alpha);
	}
	for (int32 h = 0; h < 2; ++h)
	{
		for (int32 f = 0; f < 4; ++f)
		{
			FingerOf(S.Fingers[h], f) = FMath::Lerp(FingerOf(A.Fingers[h], f), FingerOf(B.Fingers[h], f), float(Alpha));
		}
	}
	return S;
}

// ---------------------------------------------------------------------------------------------------------------
// Replay

FTeleopReplay::~FTeleopReplay()
{
	Shutdown();
}

void FTeleopReplay::Init(ATeleopGameMode* InGameMode)
{
	GameMode = InGameMode;
	const TCHAR* Cmd = FCommandLine::Get();

	FString Set;
	if (FParse::Value(Cmd, TEXT("TeleopSet="), Set, false))
	{
		ApplyCVarList(Set);
	}

	FString Path;
	if (!FParse::Value(Cmd, TEXT("TeleopReplay="), Path))
	{
		return;
	}
	Path = ResolveReplayPath(Path);
	FString Error;
	if (!Trajectory.LoadCsv(Path, Error))
	{
		UE_LOG(LogTeleop, Error, TEXT("[replay] %s: %s"), *Path, *Error);
		return;
	}
	if (UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled())
	{
		UE_LOG(LogTeleop, Error, TEXT("[replay] desktop only (the headset decides the view in VR); start with -nohmd"));
		return;
	}
	if (FApp::UseFixedTimeStep())
	{
		TeleopClock::BeginFixedStep(FApp::GetFixedDeltaTime());
	}
	else
	{
		UE_LOG(LogTeleop, Warning, TEXT("[replay] no -UseFixedTimeStep -FPS=N: wall-clock time, runs will not match frame by frame"));
	}
	StartTime = TeleopClock::Now();
	bPlaying = true;
	UE_LOG(LogTeleop, Log, TEXT("[replay] playing %s: %d samples, %.2f s, step %.4f s (%s)"), *Path, Trajectory.Num(),
		Trajectory.Duration(), FApp::GetFixedDeltaTime(), TeleopClock::IsFixedStep() ? TEXT("fixed") : TEXT("wall clock"));

	if (FParse::Value(Cmd, TEXT("TeleopReplayOut="), OutDir))
	{
		FParse::Value(Cmd, TEXT("TeleopReplayFrom="), OutFrom);
		FParse::Value(Cmd, TEXT("TeleopReplayObsEvery="), ObsEvery);
		IFileManager::Get().MakeDirectory(*OutDir, true);
		FModuleManager::Get().LoadModule(TEXT("ImageWrapper")); // the PNG writes run on worker threads
		FramesCsv.Reset(IFileManager::Get().CreateFileWriter(*FPaths::Combine(OutDir, TEXT("frames.csv"))));
		if (FramesCsv)
		{
			const FTCHARToUTF8 Header(TEXT("frame,t,method,label,shown_frame,age_ms,off_x,off_y,off_z,off_cm,rot_deg,r_cm,keyframes,")
				TEXT("head_x,head_y,head_z,head_pitch,head_yaw,head_roll\n"));
			FramesCsv->Serialize(const_cast<ANSICHAR*>(Header.Get()), Header.Length());
		}
		ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddRaw(this, &FTeleopReplay::OnScreenshot);
		UE_LOG(LogTeleop, Log, TEXT("[replay] writing frames from t=%.2f s to %s (observer every %d frames)"), OutFrom, *OutDir, ObsEvery);
	}
	PostTickHandle = FWorldDelegates::OnWorldPostActorTick.AddRaw(this, &FTeleopReplay::OnPostActorTick);
}

void FTeleopReplay::Shutdown()
{
	if (PostTickHandle.IsValid())
	{
		FWorldDelegates::OnWorldPostActorTick.Remove(PostTickHandle);
		PostTickHandle.Reset();
	}
	if (ScreenshotHandle.IsValid())
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
		ScreenshotHandle.Reset();
	}
	UE::Tasks::Wait(Writes);
	Writes.Reset();
	FramesCsv.Reset();
	if (bPlaying)
	{
		TeleopClock::EndFixedStep();
		bPlaying = false;
	}
	StopRecording();
}

bool FTeleopReplay::GetSample(FTeleopTrajectorySample& Out) const
{
	if (!bPlaying)
	{
		return false;
	}
	Out = Trajectory.Sample(TeleopClock::Now() - StartTime);
	return true;
}

void FTeleopReplay::OnPostActorTick(UWorld* World, ELevelTick, float)
{
	ATeleopGameMode* GM = GameMode.Get();
	if (!bPlaying || bFinished || !GM || World != GM->GetWorld())
	{
		return;
	}
	const double T = TeleopClock::Now() - StartTime;
	if (!bSceneLogged && T >= 1.0)
	{
		bSceneLogged = true;
		LogScene();
	}
	if (T > Trajectory.Duration() + 1e-6)
	{
		Finish();
		return;
	}
	if (OutDir.IsEmpty() || T < OutFrom - 1e-6)
	{
		return;
	}
	const int32 Frame = FramesWritten++;
	if (PendingEyeFrame != INDEX_NONE)
	{
		UE_LOG(LogTeleop, Warning, TEXT("[replay] frame %d was not drawn (no screenshot)"), PendingEyeFrame);
	}
	PendingEyeFrame = Frame;
	FScreenshotRequest::RequestScreenshot(false); // processed when this frame's viewport draws (OnScreenshot)
	if (ObsEvery > 0 && Frame % ObsEvery == 0)
	{
		CaptureObserver(Frame);
	}
	WriteFrameRow(Frame, T);
}

void FTeleopReplay::OnScreenshot(int32 Width, int32 Height, const TArray<FColor>& Bitmap)
{
	if (PendingEyeFrame == INDEX_NONE)
	{
		return;
	}
	QueueWrite(FPaths::Combine(OutDir, FString::Printf(TEXT("eye_%05d.png"), PendingEyeFrame)), Width, Height, Bitmap);
	PendingEyeFrame = INDEX_NONE;
}

void FTeleopReplay::CaptureObserver(int32 FrameIndex)
{
	ATeleopGameMode* GM = GameMode.Get();
	ATeleopObserver* Observer = GM ? GM->GetObserver() : nullptr;
	UTextureRenderTarget2D* RT = Observer ? Observer->GetRenderTarget() : nullptr;
	if (!RT)
	{
		return;
	}
	// Same size as the eye view, so the two sit side by side like the spectator screen.
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
		if (Size.X > 0 && Size.Y > 0 && (RT->SizeX != Size.X || RT->SizeY != Size.Y))
		{
			RT->ResizeTarget(Size.X, Size.Y);
		}
	}
	Observer->DebugCaptureNow();
	TArray<FColor> Pixels;
	FTextureRenderTargetResource* Resource = RT->GameThread_GetRenderTargetResource();
	if (Resource && Resource->ReadPixels(Pixels)) // waits for this frame's capture
	{
		QueueWrite(FPaths::Combine(OutDir, FString::Printf(TEXT("obs_%05d.png"), FrameIndex)), RT->SizeX, RT->SizeY, MoveTemp(Pixels));
	}
}

void FTeleopReplay::WriteFrameRow(int32 FrameIndex, double T)
{
	ATeleopGameMode* GM = GameMode.Get();
	ATeleopPresenter* P = GM ? GM->GetPresenter() : nullptr;
	const ATeleopOperatorPawn* Pawn = P ? Cast<ATeleopOperatorPawn>(GM->GetWorld()->GetFirstPlayerController()->GetPawn()) : nullptr;
	if (!FramesCsv || !P || !Pawn)
	{
		return;
	}
	FVector Off = FVector::ZeroVector;
	float RotDeg = 0.f;
	P->GetEyeCameraOffset(Off, RotDeg);
	FTransform Head;
	Pawn->GetLatestLocalPose(ETeleopDevice::Head, Head);
	const FVector HL = Head.GetLocation();
	const FRotator HR = Head.Rotator();
	const FString Row = FString::Printf(TEXT("%d,%.5f,%d,\"%s\",%lld,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n"),
		FrameIndex, T, int32(P->GetActiveMethod()), *P->GetMethodLabel(), P->GetShownFrameId(), P->GetCurrentFrameAgeMs(),
		Off.X, Off.Y, Off.Z, Off.Size(), RotDeg, P->GetShownSphereRadiusCm(), P->GetMemory() ? P->GetMemory()->NumKeyframes() : 0,
		HL.X, HL.Y, HL.Z, HR.Pitch, HR.Yaw, HR.Roll);
	const FTCHARToUTF8 Utf8(*Row);
	FramesCsv->Serialize(const_cast<ANSICHAR*>(Utf8.Get()), Utf8.Length());
}

void FTeleopReplay::LogScene() const
{
	// Where things are in the operator's tracking frame, for writing trajectories (Tools/replay_trajectory.py).
	ATeleopGameMode* GM = GameMode.Get();
	UWorld* World = GM ? GM->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	const FTransform L2R = GM->GetLocalToRemote();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetClass()->GetName().Contains(TEXT("Grabbable")))
		{
			UE_LOG(LogTeleop, Log, TEXT("[replay] scene %s local %s"), *It->GetName(), *L2R.InverseTransformPosition(It->GetActorLocation()).ToCompactString());
		}
	}
	const ATeleopOperatorPawn* Pawn = Cast<ATeleopOperatorPawn>(World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr);
	for (const bool bLeft : { true, false })
	{
		const ARemoteHand* Hand = GM->GetRemoteHand(bLeft);
		FTransform Controller;
		if (Hand && Hand->GetGripPoint() && Pawn && Pawn->GetLatestLocalPose(bLeft ? ETeleopDevice::LeftHand : ETeleopDevice::RightHand, Controller))
		{
			// The robot hand is delayed; at rest it is where the controller is, so the grip offset is valid then.
			const FVector Grip = L2R.InverseTransformPosition(Hand->GetGripPoint()->GetComponentLocation());
			UE_LOG(LogTeleop, Log, TEXT("[replay] scene grip %s local %s controller %s (grip - controller in the controller frame %s)"),
				bLeft ? TEXT("L") : TEXT("R"), *Grip.ToCompactString(), *Controller.GetLocation().ToCompactString(),
				*Controller.InverseTransformPositionNoScale(Grip).ToCompactString());
		}
	}
}

void FTeleopReplay::Finish()
{
	bFinished = true;
	UE::Tasks::Wait(Writes);
	Writes.Reset();
	FramesCsv.Reset();
	UE_LOG(LogTeleop, Log, TEXT("[replay] done: %.2f s, %d frames written"), Trajectory.Duration(), FramesWritten);
	if (ScreenshotHandle.IsValid())
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
		ScreenshotHandle.Reset();
	}
	if (!GIsEditor)
	{
		FPlatformMisc::RequestExit(false, TEXT("teleop replay done"));
	}
}

void FTeleopReplay::QueueWrite(FString Path, int32 Width, int32 Height, TArray<FColor> Pixels)
{
	Writes.RemoveAll([](const UE::Tasks::FTask& Task) { return Task.IsCompleted(); });
	while (Writes.Num() >= 32)
	{
		Writes[0].Wait(); // bound the memory held by queued frames
		Writes.RemoveAt(0);
	}
	Writes.Add(UE::Tasks::Launch(UE_SOURCE_LOCATION, [Path = MoveTemp(Path), Width, Height, Pixels = MoveTemp(Pixels)]() mutable
	{
		for (FColor& C : Pixels)
		{
			C.A = 255; // scene captures leave alpha at 0
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		FFileHelper::SaveArrayToFile(Png, *Path);
	}));
}

// ---------------------------------------------------------------------------------------------------------------
// Recording

void FTeleopReplay::StartRecording(const FString& Path)
{
	Recording.Reset();
	RecordPath = ResolveReplayPath(Path);
	RecordStart = TeleopClock::Now();
	UE_LOG(LogTeleop, Log, TEXT("[replay] recording to %s (teleop.Replay.Stop writes it)"), *RecordPath);
}

void FTeleopReplay::StopRecording()
{
	if (RecordPath.IsEmpty())
	{
		return;
	}
	const bool bOk = Recording.Num() > 0 && Recording.SaveCsv(RecordPath);
	UE_LOG(LogTeleop, Log, TEXT("[replay] recording %s: %d samples, %.2f s -> %s"), bOk ? TEXT("saved") : TEXT("NOT saved"),
		Recording.Num(), Recording.Duration(), *RecordPath);
	RecordPath.Reset();
	Recording.Reset();
}

void FTeleopReplay::AddRecordedSample(const FTeleopTrajectorySample& Sample)
{
	if (RecordPath.IsEmpty())
	{
		return;
	}
	FTeleopTrajectorySample S = Sample;
	S.Time = TeleopClock::Now() - RecordStart;
	if (Recording.Num() == 0 || S.Time > Recording.Duration())
	{
		Recording.Add(S);
	}
}
