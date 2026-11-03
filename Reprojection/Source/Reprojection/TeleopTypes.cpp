#include "TeleopTypes.h"

void FTeleopPoseHistory::Add(double Time, const FTransform& Pose, double MaxAgeSeconds)
{
	if (Samples.Num() > 0 && Time <= Samples.Last().Time)
	{
		Samples.Last().Pose = Pose; // same timestamp: keep the newest pose
		return;
	}
	Samples.Add({ Time, Pose });

	int32 FirstKept = 0;
	while (FirstKept < Samples.Num() - 2 && Samples[FirstKept + 1].Time < Time - MaxAgeSeconds)
	{
		++FirstKept;
	}
	if (FirstKept > 0)
	{
		Samples.RemoveAt(0, FirstKept, EAllowShrinking::No);
	}
}

bool FTeleopPoseHistory::Sample(double Time, FTransform& OutPose) const
{
	if (Samples.Num() == 0)
	{
		return false;
	}
	if (Time <= Samples[0].Time)
	{
		OutPose = Samples[0].Pose;
		return true;
	}
	if (Time >= Samples.Last().Time)
	{
		OutPose = Samples.Last().Pose;
		return true;
	}

	// Binary search for the last sample with Sample.Time <= Time.
	int32 Lo = 0;
	int32 Hi = Samples.Num() - 1;
	while (Hi - Lo > 1)
	{
		const int32 Mid = (Lo + Hi) / 2;
		(Samples[Mid].Time <= Time ? Lo : Hi) = Mid;
	}
	const FSample& A = Samples[Lo];
	const FSample& B = Samples[Hi];
	const double Alpha = (Time - A.Time) / FMath::Max(B.Time - A.Time, 1e-9);

	OutPose.SetLocation(FMath::Lerp(A.Pose.GetLocation(), B.Pose.GetLocation(), Alpha));
	OutPose.SetRotation(FQuat::Slerp(A.Pose.GetRotation(), B.Pose.GetRotation(), Alpha).GetNormalized());
	OutPose.SetScale3D(A.Pose.GetScale3D());
	return true;
}

bool FTeleopPoseHistory::Velocity(double Time, double Window, FVector& OutLinear, FVector& OutAngularDeg) const
{
	FTransform P0, P1;
	if (Samples.Num() < 2 || Window <= 0.0 || !Sample(Time - Window, P0) || !Sample(Time, P1))
	{
		OutLinear = FVector::ZeroVector;
		OutAngularDeg = FVector::ZeroVector;
		return false;
	}
	OutLinear = (P1.GetLocation() - P0.GetLocation()) / Window;

	FVector Axis;
	double Angle;
	(P1.GetRotation() * P0.GetRotation().Inverse()).ToAxisAndAngle(Axis, Angle);
	if (Angle > PI)
	{
		Angle -= 2.0 * PI; // shortest way around
	}
	OutAngularDeg = Axis * FMath::RadiansToDegrees(Angle) / Window;
	return true;
}
