#include "TeleopMath.h"
#include "TeleopTypes.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Project -> unproject round trip must be exact to < 0.1 mm (HANDOFF Â§7.2); we require < 0.001 mm.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeleopPinholeRoundTripTest, "Teleop.Math.PinholeRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTeleopPinholeRoundTripTest::RunTest(const FString& Parameters)
{
	FRandomStream Rng(1234);
	double MaxErrorCm = 0.0;
	for (int32 Trial = 0; Trial < 200; ++Trial)
	{
		const FTransform Cam(FRotator(Rng.FRandRange(-60, 60), Rng.FRandRange(-180, 180), Rng.FRandRange(-30, 30)),
			FVector(Rng.FRandRange(-500, 500), Rng.FRandRange(-500, 500), Rng.FRandRange(0, 300)));
		const FTeleopPinhole Pinhole = FTeleopPinhole::FromTransform(Cam, 110.0, 1280.0 / 960.0);
		for (int32 Point = 0; Point < 50; ++Point)
		{
			// Random point in front of the camera, inside the frustum.
			const FVector2D UV(Rng.FRandRange(0.01, 0.99), Rng.FRandRange(0.01, 0.99));
			const double Z = Rng.FRandRange(20.0, 2000.0);
			const FVector P = Pinhole.Unproject(UV, Z);
			double Z2;
			const FVector2D UV2 = Pinhole.Project(P, Z2);
			const FVector P2 = Pinhole.Unproject(UV2, Z2);
			MaxErrorCm = FMath::Max(MaxErrorCm, FVector::Dist(P, P2));
			if (!TestTrue(TEXT("depth is view-space z"), FMath::IsNearlyEqual(Z, Z2, 1e-6)))
			{
				return false;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("max round-trip error %.3g mm"), MaxErrorCm * 10.0));
	return TestTrue(TEXT("round trip < 0.001 mm"), MaxErrorCm * 10.0 < 1e-3);
}

// Camera conventions: +X forward is the image center, +Y right is u > 0.5, +Z up is v < 0.5.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeleopPinholeAxesTest, "Teleop.Math.PinholeAxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTeleopPinholeAxesTest::RunTest(const FString& Parameters)
{
	const FTeleopPinhole C = FTeleopPinhole::FromTransform(FTransform::Identity, 90.0, 1.0);
	double Z;
	TestTrue(TEXT("center"), C.Project(FVector(100, 0, 0), Z).Equals(FVector2D(0.5, 0.5), 1e-9));
	TestTrue(TEXT("right -> u > 0.5"), C.Project(FVector(100, 50, 0), Z).X > 0.5);
	TestTrue(TEXT("up -> v < 0.5"), C.Project(FVector(100, 0, 50), Z).Y < 0.5);
	TestTrue(TEXT("90 deg fov edge"), C.Project(FVector(100, 100, 0), Z).Equals(FVector2D(1.0, 0.5), 1e-9));
	return true;
}

// Sphere: from inside, the hit is at distance R from the center along the ray; pure rotation is exact.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeleopRaySphereTest, "Teleop.Math.RaySphere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTeleopRaySphereTest::RunTest(const FString& Parameters)
{
	FRandomStream Rng(99);
	for (int32 i = 0; i < 1000; ++i)
	{
		const FVector Center(Rng.FRandRange(-100, 100), Rng.FRandRange(-100, 100), Rng.FRandRange(-100, 100));
		const FVector O = Center + Rng.GetUnitVector() * Rng.FRandRange(0.0, 50.0);
		const FVector V = Rng.GetUnitVector();
		const double R = Rng.FRandRange(60.0, 500.0);
		const double S = TeleopMath::RaySphere(O, V, Center, R);
		if (!TestTrue(TEXT("hit"), S > 0.0) || !TestTrue(TEXT("on sphere"), FMath::IsNearlyEqual(FVector::Dist(O + S * V, Center), R, 1e-6)))
		{
			return false;
		}
	}
	// Eye at the camera center: the sphere hit projects to the same direction as the eye ray (exact rotation compensation).
	const FTeleopPinhole C = FTeleopPinhole::FromTransform(FTransform(FRotator(10, 20, 0), FVector(50, 0, 120)), 110.0, 4.0 / 3.0);
	const FVector V = FRotator(5, 30, 0).Vector();
	const double S = TeleopMath::RaySphere(C.Pos, V, C.Pos, 100.0);
	double Z;
	const FVector2D UV = C.Project(C.Pos + S * V, Z);
	const FVector Back = (C.Unproject(UV, Z) - C.Pos).GetSafeNormal();
	return TestTrue(TEXT("rotation exact"), Back.Equals(V, 1e-9));
}

// Auto recenter: never on the all-zero pose Link reports before tracking starts (that put the robot cameras on the
// floor), nor below the minimum eye height; only after the pose stayed valid for SettleSeconds; again after Rearm().
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeleopAutoRecenterTest, "Teleop.Math.AutoRecenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTeleopAutoRecenterTest::RunTest(const FString& Parameters)
{
	using EState = FTeleopAutoRecenter::EState;
	const FTransform Zero = FTransform::Identity;
	const FTransform Seated(FRotator(0, 20, 0), FVector(5, -3, 118));
	const FTransform OnFloor(FRotator(0, 20, 0), FVector(5, -3, 8));

	FTeleopAutoRecenter A;
	double T = 0.0;
	for (int32 i = 0; i < 100; ++i, T += 0.014)
	{
		// The Link case: IsTracking() true but the pose is all zero.
		if (!TestFalse(TEXT("never on the zero pose"), A.Update(T, true, Zero)))
		{
			return false;
		}
	}
	TestTrue(TEXT("waiting for tracking"), A.GetState() == EState::WaitingForTracking);
	TestFalse(TEXT("not when untracked"), A.Update(T, false, Seated));
	TestFalse(TEXT("not below the minimum height"), A.Update(T += 0.014, true, OnFloor));
	TestTrue(TEXT("below min height"), A.GetState() == EState::BelowMinHeight);

	// Valid from T0: fires once, after SettleSeconds.
	const double T0 = T += 0.014;
	bool bFired = false;
	double FiredAt = -1.0;
	for (; T < T0 + 2.0; T += 0.014)
	{
		if (A.Update(T, true, Seated))
		{
			TestFalse(TEXT("fires once"), bFired);
			bFired = true;
			FiredAt = T;
		}
	}
	TestTrue(TEXT("fired"), bFired);
	TestTrue(TEXT("after the settle time"), FiredAt - T0 >= A.SettleSeconds && FiredAt - T0 < A.SettleSeconds + 0.03);
	TestTrue(TEXT("done"), A.GetState() == EState::Done);

	// A tracking dropout during settling restarts the settle time.
	FTeleopAutoRecenter B;
	TestFalse(TEXT("settling"), B.Update(0.0, true, Seated));
	TestFalse(TEXT("dropout"), B.Update(0.3, true, Zero));
	TestFalse(TEXT("restart"), B.Update(0.31, true, Seated));
	TestFalse(TEXT("not yet: settle restarted at 0.31"), B.Update(0.6, true, Seated));
	TestTrue(TEXT("fires 0.5 s after the restart"), B.Update(0.81, true, Seated));

	// Rearm (headset put on, system recenter): fires again.
	A.Rearm();
	TestFalse(TEXT("rearmed, settling"), A.Update(10.0, true, Seated));
	TestTrue(TEXT("rearmed, fires"), A.Update(10.6, true, Seated));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeleopWaistTest, "Teleop.Math.Waist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTeleopWaistTest::RunTest(const FString& Parameters)
{
	const double L = 55.0;

	// Signs: leaning toward +X / +Y moves the neck forward / right and keeps it above the waist.
	const FVector Fwd = TeleopMath::WaistNeckPosition(TeleopMath::WaistLean(FVector(20, 0, 50), 0.0), L);
	const FVector Right = TeleopMath::WaistNeckPosition(TeleopMath::WaistLean(FVector(0, 20, 50), 0.0), L);
	TestTrue(TEXT("lean forward"), Fwd.X > 15.0 && FMath::Abs(Fwd.Y) < 1e-6 && Fwd.Z > 0.0);
	TestTrue(TEXT("lean right"), Right.Y > 15.0 && FMath::Abs(Right.X) < 1e-6 && Right.Z > 0.0);
	TestTrue(TEXT("upright"), TeleopMath::WaistLean(FVector(0, 0, 40), 0.0).IsNearlyZero(1e-9));

	// Round trip: the solved lean points the torso at the target for any waist yaw (error < 0.1 mm at the neck).
	FRandomStream Rng(42);
	double MaxErr = 0.0;
	for (int32 i = 0; i < 2000; ++i)
	{
		const FVector Target(Rng.FRandRange(-40.0, 40.0), Rng.FRandRange(-40.0, 40.0), Rng.FRandRange(20.0, 70.0));
		const double Yaw = Rng.FRandRange(-90.0, 90.0);
		const FRotator W = TeleopMath::WaistLean(Target, Yaw);
		MaxErr = FMath::Max(MaxErr, (TeleopMath::WaistNeckPosition(W, L) - Target.GetSafeNormal() * L).Size());
		if (!TestTrue(TEXT("yaw kept"), FMath::IsNearlyEqual(W.Yaw, Yaw, 1e-9)))
		{
			return false;
		}
	}
	TestTrue(FString::Printf(TEXT("round trip error %.2e cm < 0.01 cm"), MaxErr), MaxErr < 0.01);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
