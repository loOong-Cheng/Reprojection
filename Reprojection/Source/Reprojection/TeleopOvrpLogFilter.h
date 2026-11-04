#pragma once

#include "CoreMinimal.h"

/**
 * Filters one known-harmless OVRPlugin error out of the log.
 *
 * On every controller pose query (both hands, game and render thread) OculusXRInput first asks OVRPlugin for the
 * "detached controller" interaction profile. Runtimes without XR_META_detached_controllers (the Meta XR Simulator)
 * answer xrGetCurrentInteractionProfile with XR_ERROR_PATH_INVALID, and OVRPlugin logs it: ~290 errors per second,
 * ~7.5 MB of log per minute. The plugin then falls back to the normal controller pose, so input is unaffected.
 *
 * The Meta plugin forwards OVRPlugin messages with GLog->CategorizedLogf, which bypasses log category verbosity, so
 * "log LogOVRPlugin off" cannot silence it. Instead this replaces OVRPlugin's log callback with the same level mapping
 * plus a filter for that one message: the first occurrence is logged, repeats are counted and summarized.
 * Development builds only (the Meta plugin installs no callback in Shipping). -NoOvrpLogFilter disables it.
 */
namespace TeleopOvrpLogFilter
{
	/** Installs the callback once OVRPlugin is loaded. Cheap; call every tick until it returns true. */
	bool TryInstall();

	/** Logs how many repeats were dropped since the last call (if any). Game thread. */
	void ReportSuppressed();
}
