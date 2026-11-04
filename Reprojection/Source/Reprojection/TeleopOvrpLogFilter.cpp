#include "TeleopOvrpLogFilter.h"

#include "Reprojection.h"

#include "CoreGlobals.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"

#include <atomic>
#include <cstring>

#if PLATFORM_WINDOWS && !UE_BUILD_SHIPPING
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#define TELEOP_OVRP_LOG_FILTER 1
#else
#define TELEOP_OVRP_LOG_FILTER 0
#endif

namespace TeleopOvrpLogFilter
{
#if TELEOP_OVRP_LOG_FILTER
	namespace
	{
		// OVR_Plugin_Types.h: ovrpLogLevel (32-bit enum) and ovrpLogCallback2; OVR_Plugin.h: ovrp_SetLogCallback2.
		enum : int32 { OvrpLevelDebug = 0, OvrpLevelInfo = 1, OvrpLevelError = 2, OvrpLevelWarning = 3 };
		using FOvrpLogCallback2 = void(__cdecl*)(int32 Level, const char* Message, int Length);
		using FOvrpSetLogCallback2 = int32(__cdecl*)(FOvrpLogCallback2 Callback);

		std::atomic<bool> bLoggedFirst{ false };
		std::atomic<int64> SuppressedCount{ 0 };
		int64 ReportedCount = 0;

		bool IsDetachedProfileError(const char* Message, int Length)
		{
			// Bounded copy: OVRPlugin passes an explicit length; the message may not be terminated.
			char Buffer[512];
			const int N = FMath::Min(Length, int(sizeof(Buffer)) - 1);
			if (N <= 0)
			{
				return false;
			}
			FMemory::Memcpy(Buffer, Message, N);
			Buffer[N] = '\0';
			return std::strstr(Buffer, "XR_ERROR_PATH_INVALID") && std::strstr(Buffer, "xrGetCurrentInteractionProfile");
		}

		void __cdecl FilteredCallback(int32 Level, const char* Message, int Length)
		{
			if (Level == OvrpLevelError && IsDetachedProfileError(Message, Length))
			{
				if (bLoggedFirst.exchange(true))
				{
					SuppressedCount.fetch_add(1, std::memory_order_relaxed);
					return;
				}
				GLog->CategorizedLogf(TEXT("LogOVRPlugin"), ELogVerbosity::Warning,
					TEXT("%s (runtime without XR_META_detached_controllers; harmless, repeats are counted, not logged)"),
					*FString::ConstructFromPtrSize(Message, Length));
				return;
			}

			// Same mapping as the Meta plugin's own callback (OculusXRHMDModule.cpp).
			ELogVerbosity::Type Verbosity = ELogVerbosity::NoLogging;
			switch (Level)
			{
			case OvrpLevelDebug: Verbosity = ELogVerbosity::Log; break;
			case OvrpLevelInfo: Verbosity = ELogVerbosity::Display; break;
			case OvrpLevelError: Verbosity = ELogVerbosity::Error; break;
			case OvrpLevelWarning: Verbosity = ELogVerbosity::Warning; break;
			default: break;
			}
			GLog->CategorizedLogf(TEXT("LogOVRPlugin"), Verbosity, TEXT("%s"), *FString::ConstructFromPtrSize(Message, Length));
		}

		bool bInstalled = false;
	}

	bool TryInstall()
	{
		if (bInstalled)
		{
			return true;
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("NoOvrpLogFilter")))
		{
			UE_LOG(LogTeleop, Log, TEXT("OVRPlugin log filter disabled (-NoOvrpLogFilter)"));
			bInstalled = true;
			return true;
		}
		// Only if the Meta plugin already loaded it (it sets its own callback right after loading, in HMD PreInit).
		HMODULE Ovrp = ::GetModuleHandleW(L"OVRPlugin.dll");
		if (!Ovrp)
		{
			return false;
		}
		const FOvrpSetLogCallback2 SetLogCallback2 = reinterpret_cast<FOvrpSetLogCallback2>(FPlatformProcess::GetDllExport(Ovrp, TEXT("ovrp_SetLogCallback2")));
		if (!SetLogCallback2)
		{
			UE_LOG(LogTeleop, Warning, TEXT("OVRPlugin has no ovrp_SetLogCallback2; log filter not installed"));
			bInstalled = true; // do not retry
			return true;
		}
		SetLogCallback2(&FilteredCallback);
		bInstalled = true;
		UE_LOG(LogTeleop, Log, TEXT("OVRPlugin log filter installed (drops repeats of the detached-controller XR_ERROR_PATH_INVALID error)"));
		return true;
	}

	void ReportSuppressed()
	{
		const int64 Count = SuppressedCount.load(std::memory_order_relaxed);
		if (Count != ReportedCount)
		{
			UE_LOG(LogTeleop, Log, TEXT("OVRPlugin log filter: %lld repeats of the detached-controller error dropped (%lld total)"),
				Count - ReportedCount, Count);
			ReportedCount = Count;
		}
	}
#else
	bool TryInstall() { return true; }
	void ReportSuppressed() {}
#endif
}
