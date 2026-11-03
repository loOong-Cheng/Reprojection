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
