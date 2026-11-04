#pragma once

#include "CoreMinimal.h"

/**
 * Robot camera pinhole model (HANDOFF §7.2/§7.3). Mirrors Shaders/Teleop/Camera.ush; unit tested
 * (Automation: Teleop.Math.*). UE world, cm. Camera looks along Fwd; uv (0,0) top-left, v down;
 * depth Z is view-space z along Fwd, not radial distance.
 */
struct FTeleopPinhole
{
	FVector Pos = FVector::ZeroVector;
	FVector Fwd = FVector::ForwardVector;
	FVector Right = FVector::RightVector;
	FVector Up = FVector::UpVector;
	double TanHalfX = 1.0;
	double TanHalfY = 1.0;

	static FTeleopPinhole FromTransform(const FTransform& CameraWorld, double HorizontalFovDeg, double Aspect /* width / height */)
	{
		FTeleopPinhole C;
		C.Pos = CameraWorld.GetLocation();
		C.Fwd = CameraWorld.GetUnitAxis(EAxis::X);
		C.Right = CameraWorld.GetUnitAxis(EAxis::Y);
		C.Up = CameraWorld.GetUnitAxis(EAxis::Z);
		C.TanHalfX = FMath::Tan(FMath::DegreesToRadians(HorizontalFovDeg * 0.5));
		C.TanHalfY = C.TanHalfX / Aspect;
		return C;
	}

	/** World point -> uv; OutZ = view-space depth. */
	FVector2D Project(const FVector& P, double& OutZ) const
	{
		const FVector D = P - Pos;
		OutZ = FVector::DotProduct(D, Fwd);
		const double InvZ = 1.0 / FMath::Max(OutZ, 1e-3);
		return FVector2D(0.5 + 0.5 * FVector::DotProduct(D, Right) * InvZ / TanHalfX,
		                 0.5 - 0.5 * FVector::DotProduct(D, Up) * InvZ / TanHalfY);
	}

	/** uv + view-space depth -> world point. */
	FVector Unproject(const FVector2D& UV, double Z) const
	{
		const double X = (UV.X * 2.0 - 1.0) * TanHalfX;
		const double Y = (1.0 - UV.Y * 2.0) * TanHalfY;
		return Pos + Z * (Fwd + X * Right + Y * Up);
	}
};

namespace TeleopMath
{
	/** Smallest positive s with |O + s V - Center| = R (V normalized); -1 if none. Same as Teleop_RaySphere. */
	inline double RaySphere(const FVector& O, const FVector& V, const FVector& Center, double R)
	{
		const FVector L = O - Center;
		const double B = FVector::DotProduct(V, L);
		const double C = L.SizeSquared() - R * R;
		const double Disc = B * B - C;
		if (Disc < 0.0)
		{
			return -1.0;
		}
		const double S = FMath::Sqrt(Disc);
		const double S1 = -B - S;
		const double S2 = -B + S;
		return S1 > 0.0 ? S1 : (S2 > 0.0 ? S2 : -1.0);
	}

	/**
	 * 3-DoF waist (joints yaw, then pitch, then roll: the FRotator order) carrying the neck pivot at Length along
	 * the torso's up axis. Unit tested (Teleop.Math.Waist).
	 */
	inline FVector WaistNeckPosition(const FRotator& Waist, double Length)
	{
		return Waist.RotateVector(FVector(0.0, 0.0, Length));
	}

	/** Pitch and roll that point the torso at Target (relative to the waist pivot) for the given waist yaw. */
	inline FRotator WaistLean(const FVector& Target, double YawDeg)
	{
		const FVector D = FRotator(0.0, YawDeg, 0.0).UnrotateVector(Target).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		// FRotator(P, 0, R) turns +Z into (-cos R sin P, sin R, cos R cos P).
		const double Roll = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(D.Y, -1.0, 1.0)));
		const double Pitch = FMath::RadiansToDegrees(FMath::Atan2(-D.X, D.Z));
		return FRotator(Pitch, YawDeg, Roll);
	}
}
