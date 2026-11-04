#include "TeleopProps.h"

#include "TeleopCVars.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	void SetColor(UStaticMeshComponent* C, UObject* Outer, const FLinearColor& Color)
	{
		static const FSoftObjectPath LitPath(TEXT("/Game/Teleop/Materials/M_TeleopLitColor.M_TeleopLitColor"));
		if (UMaterialInterface* Base = Cast<UMaterialInterface>(LitPath.TryLoad()))
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			C->SetMaterial(0, MID);
		}
	}
}

ATeleopProps::ATeleopProps()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	// Checkerboard standing on the desk (desk top z = 74), facing the station: plane normal +Z -> pitch 90 -> -X.
	Checker = MakeMesh(TEXT("Checker"), PlaneMesh.Object, CheckerLocation, FVector(CheckerSizeCm / 100.f), Root);
	Checker->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));

	// Three 1 cm rods, 25 cm tall, standing on the desk between the pistols and the cubes.
	for (int32 i = 0; i < 3; ++i)
	{
		const FString Name = FString::Printf(TEXT("Rod%d"), i);
		Rods.Add(MakeMesh(*Name, CylinderMesh.Object, FVector(65.f, 30.f + 5.f * i, 74.f + 12.5f), FVector(0.01f, 0.01f, 0.25f), Root));
	}

	SlidingBlock = MakeMesh(TEXT("SlidingBlock"), CubeMesh.Object, SlideCenter, FVector(0.08f), Root);

	FanHub = CreateDefaultSubobject<USceneComponent>(TEXT("FanHub"));
	FanHub->SetupAttachment(Root);
	FanHub->SetRelativeLocation(FVector(180.f, -40.f, 110.f));
	for (int32 i = 0; i < 3; ++i)
	{
		const FString Name = FString::Printf(TEXT("FanBlade%d"), i);
		UStaticMeshComponent* Blade = MakeMesh(*Name, CubeMesh.Object, FVector::ZeroVector, FVector(0.01f, 0.30f, 0.05f), FanHub);
		Blade->SetRelativeRotation(FRotator(0.f, 0.f, 120.f * i));
		Blade->SetRelativeLocation(FRotator(0.f, 0.f, 120.f * i).RotateVector(FVector(0.f, 15.f, 0.f)));
		FanBlades.Add(Blade);
	}
}

UStaticMeshComponent* ATeleopProps::MakeMesh(const TCHAR* Name, UStaticMesh* Mesh, const FVector& Location, const FVector& Scale, USceneComponent* Parent)
{
	UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	C->SetupAttachment(Parent);
	C->SetStaticMesh(Mesh);
	C->SetRelativeLocation(Location);
	C->SetRelativeScale3D(Scale);
	C->SetMobility(EComponentMobility::Movable);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	return C;
}

void ATeleopProps::BeginPlay()
{
	Super::BeginPlay();
	StartTime = FPlatformTime::Seconds();

	static const FSoftObjectPath CheckerPath(TEXT("/Game/Teleop/Materials/M_TeleopChecker.M_TeleopChecker"));
	if (UMaterialInterface* CheckerMat = Cast<UMaterialInterface>(CheckerPath.TryLoad()))
	{
		Checker->SetMaterial(0, CheckerMat);
	}
	for (UStaticMeshComponent* Rod : Rods)
	{
		SetColor(Rod, this, FLinearColor(0.9f, 0.85f, 0.1f));
	}
	SetColor(SlidingBlock, this, FLinearColor(0.1f, 0.8f, 0.3f));
	for (UStaticMeshComponent* Blade : FanBlades)
	{
		SetColor(Blade, this, FLinearColor(0.8f, 0.15f, 0.15f));
	}
}

void ATeleopProps::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = FPlatformTime::Seconds();
	if (TeleopCVars::DebugFreeze.GetValueOnGameThread() != 0 && LastTickTime > 0.0)
	{
		StartTime += Now - LastTickTime; // frozen: the animation time stands still
	}
	LastTickTime = Now;
	const double T = Now - StartTime;
	SlidingBlock->SetRelativeLocation(SlideCenter + FVector(0.f, SlideAmplitudeCm * FMath::Sin(2.0 * PI * T / SlidePeriodSec), 0.f));
	FanHub->SetRelativeRotation(FRotator(0.f, 0.f, FMath::Fmod(FanDegPerSec * T, 360.0)));
}

FVector ATeleopProps::GetCheckerWorldLocation() const
{
	return Checker->GetComponentLocation();
}
