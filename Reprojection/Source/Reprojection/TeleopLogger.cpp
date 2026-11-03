#include "TeleopLogger.h"

#include "FrameRingBuffer.h"
#include "Reprojection.h"
#include "TeleopCVars.h"
#include "TeleopLatencyModel.h"

#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"

FTeleopLogger::FTeleopLogger()
{
	SessionDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TeleopLogs"), FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
}

FTeleopLogger::~FTeleopLogger()
{
	Flush();
}

FArchive* FTeleopLogger::Open(TUniquePtr<FArchive>& File, const TCHAR* Name, const TCHAR* Header)
{
	if (!File)
	{
		IFileManager::Get().MakeDirectory(*SessionDir, true);
		// AllowRead: the CSVs can be inspected while the session is running.
		File.Reset(IFileManager::Get().CreateFileWriter(*FPaths::Combine(SessionDir, Name), FILEWRITE_AllowRead));
		if (File)
		{
			WriteLine(File.Get(), Header);
			UE_LOG(LogTeleop, Log, TEXT("Logging %s to %s"), Name, *SessionDir);
		}
	}
	return File.Get();
}

void FTeleopLogger::WriteLine(FArchive* File, const FString& Line)
{
	if (File)
	{
		FTCHARToUTF8 Utf8(*(Line + TEXT("\n")));
		File->Serialize(const_cast<ANSICHAR*>(Utf8.Get()), Utf8.Length());
	}
}

void FTeleopLogger::LogCapture(const FTeleopFrameMeta& Meta, int32 LatencyPresetIndex)
{
	if (TeleopCVars::Log.GetValueOnGameThread() == 0)
	{
		return;
	}
	FArchive* File = Open(CaptureFile, TEXT("capture.csv"),
		TEXT("frame_id,capture_time,latency_ms,arrival_time,dropped,bad_state,latency_preset,depth_capture_time"));
	WriteLine(File, FString::Printf(TEXT("%lld,%.6f,%.3f,%.6f,%d,%d,%s,%.6f"),
		Meta.FrameId, Meta.CaptureTime, (Meta.ArrivalTime - Meta.CaptureTime) * 1000.0, Meta.ArrivalTime,
		Meta.bDropped ? 1 : 0, Meta.bBadState ? 1 : 0, *FTeleopLatencyModel::GetPresetName(LatencyPresetIndex), Meta.DepthCaptureTime));
}

void FTeleopLogger::LogDisplay(const FTeleopDisplayRow& R)
{
	if (TeleopCVars::Log.GetValueOnGameThread() == 0)
	{
		return;
	}
	FArchive* File = Open(DisplayFile, TEXT("display.csv"),
		TEXT("time,shown_frame_id,shown_capture_time,frame_age_ms,method,latency_preset,base_offset_ms,jitter_buffer,jitter_target_ms,depth_preset,sphere_r_cm,baseline_mm,neck_mode,cmd_latency_ms,blind,blind_condition"));
	WriteLine(File, FString::Printf(TEXT("%.6f,%lld,%.6f,%.3f,%s,%s,%.1f,%d,%.2f,%s,%.1f,%.1f,%d,%.2f,%d,%s"),
		R.Time, R.ShownFrameId, R.ShownCaptureTime, R.AgeMs, *R.Method, *R.LatencyPreset, R.BaseOffsetMs,
		R.bJitterBuffer ? 1 : 0, R.JitterBufferTargetMs, *R.DepthPreset, R.SphereRadiusCm, R.BaselineMm, R.NeckMode,
		R.CmdLatencyMs, R.bBlind ? 1 : 0, *R.BlindCondition));
}

void FTeleopLogger::LogBlind(const FString& CsvLine)
{
	FArchive* File = Open(BlindFile, TEXT("blind.csv"),
		TEXT("condition_index,condition_code,method,latency_preset,base_offset_ms,jitter_buffer,depth_preset,stability_1to7,comfort_1to7,depth_credibility_1to7,view_start,view_end,rated_at"));
	WriteLine(File, CsvLine);
	if (File)
	{
		File->Flush();
	}
}

void FTeleopLogger::Flush()
{
	for (TUniquePtr<FArchive>* F : { &CaptureFile, &DisplayFile, &BlindFile })
	{
		if (*F)
		{
			(*F)->Flush();
		}
	}
}
