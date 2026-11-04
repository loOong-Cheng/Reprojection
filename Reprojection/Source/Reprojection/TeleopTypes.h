#pragma once

#include "CoreMinimal.h"
#include "TeleopTypes.generated.h"

/** Presentation / compensation methods (HANDOFF §6). Values are the teleop.Method console values. */
UENUM(BlueprintType)
enum class ETeleopMethod : uint8
{
	M0_Naive = 0,
	M1_SphereFixed = 1,
	M2_SphereAdaptive = 2,
	M3_DepthMesh = 3,
	M4_Hybrid = 4,
	GT = 5,
};

/** Robot neck modes (HANDOFF §3). Values are the teleop.NeckMode console values. */
UENUM(BlueprintType)
enum class ETeleopNeckMode : uint8
{
	Fixed = 0,
	Rot2DoF = 1,
	Rot3DoF = 2,
	Full6DoF = 3,
	/** 3-DoF waist (yaw, pitch, roll) that leans to follow head translation, carrying the 2-DoF neck (Sharpa). */
	Waist3Neck2 = 4,
};

/** Operator devices whose poses are streamed to the robot. */
enum class ETeleopDevice : uint8
{
	Head = 0,
	LeftHand = 1,
	RightHand = 2,
	Num = 3,
};

/** Time-stamped pose stream with interpolated lookup, used to delay poses by the command latency. */
struct FTeleopPoseHistory
{
	/** Times are FPlatformTime::Seconds(). Keeps the last MaxAgeSeconds. */
	void Add(double Time, const FTransform& Pose, double MaxAgeSeconds = 2.0);

	/** Pose at Time (interpolated; clamped to the oldest/newest sample). False if empty. */
	bool Sample(double Time, FTransform& OutPose) const;

	/** Finite-difference velocity over [Time - Window, Time]: linear cm/s, angular deg/s (world axes). */
	bool Velocity(double Time, double Window, FVector& OutLinear, FVector& OutAngularDeg) const;

	bool IsEmpty() const { return Samples.Num() == 0; }
	void Reset() { Samples.Reset(); }

private:
	struct FSample
	{
		double Time;
		FTransform Pose;
	};
	TArray<FSample> Samples;
};

/**
 * Finger controls of one hand, the inputs of the VR Template hand animation (ABP_MannequinsXR PoseAlpha*), 0..1.
 * The robot hand plays them delayed by the command latency, like the poses.
 */
struct FTeleopFingerState
{
	float Grasp = 0.f;     // grip axis
	float IndexCurl = 0.f; // trigger axis
	float Point = 0.f;     // 1 = index finger off the trigger
	float ThumbUp = 0.f;   // 1 = thumb off the buttons and the stick
};

/** Time-stamped finger stream, interpolated like FTeleopPoseHistory. */
struct FTeleopFingerHistory
{
	void Add(double Time, const FTeleopFingerState& State, double MaxAgeSeconds = 2.0);

	/** State at Time (interpolated; clamped to the oldest/newest sample). False if empty. */
	bool Sample(double Time, FTeleopFingerState& OutState) const;

private:
	TArray<TPair<double, FTeleopFingerState>> Samples;
};

/**
 * Decides when the automatic recenter may run. On Quest Link the first head poses after the session starts are all
 * zero (no tracking yet, e.g. headset not worn), and calibrating on one put the robot cameras on the floor. This waits
 * for a tracked, non-zero head pose at a plausible eye height that stays valid for SettleSeconds.
 */
struct FTeleopAutoRecenter
{
	enum class EState : uint8 { WaitingForTracking, BelowMinHeight, Settling, Done };

	double SettleSeconds = 0.5;
	/** With the LocalFloor tracking origin a seated eye is ~110-130 cm above the floor. */
	float MinEyeHeightCm = 50.f;

	/** Feed the latest head pose (tracking frame) once per frame; returns true once, when the recenter should run. */
	bool Update(double Now, bool bTracked, const FTransform& HeadLocal)
	{
		if (State == EState::Done)
		{
			return false;
		}
		if (!bTracked || HeadLocal.GetLocation().IsNearlyZero(0.01))
		{
			State = EState::WaitingForTracking;
			StableSince = -1.0;
			return false;
		}
		if (HeadLocal.GetLocation().Z < MinEyeHeightCm)
		{
			State = EState::BelowMinHeight;
			StableSince = -1.0;
			return false;
		}
		if (StableSince < 0.0)
		{
			StableSince = Now;
			State = EState::Settling;
		}
		if (Now - StableSince >= SettleSeconds)
		{
			State = EState::Done;
			return true;
		}
		return false;
	}

	/** Run again on the next valid pose (headset put on, system recenter). */
	void Rearm()
	{
		State = EState::WaitingForTracking;
		StableSince = -1.0;
	}

	EState GetState() const { return State; }

private:
	EState State = EState::WaitingForTracking;
	double StableSince = -1.0;
};
