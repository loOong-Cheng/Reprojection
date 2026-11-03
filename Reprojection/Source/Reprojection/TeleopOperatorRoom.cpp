#include "TeleopOperatorRoom.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ATeleopOperatorRoom::ATeleopOperatorRoom()
{
	PrimaryActorTick.bCanEverTick = false;

	Seat = CreateDefaultSubobject<USceneComponent>(TEXT("Seat"));
	RootComponent = Seat;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));

	static const TCHAR* PanelNames[] = { TEXT("Floor"), TEXT("Ceiling"), TEXT("WallFront"), TEXT("WallBack"), TEXT("WallRight"), TEXT("WallLeft") };
	for (const TCHAR* PanelName : PanelNames)
	{
		UStaticMeshComponent* Panel = CreateDefaultSubobject<UStaticMeshComponent>(PanelName);
		Panel->SetupAttachment(Seat);
		if (CubeMesh.Succeeded())
		{
			Panel->SetStaticMesh(CubeMesh.Object);
		}
		// Movable + unlit: needs no lightmaps, so the Template's baked lighting stays valid.
		Panel->SetMobility(EComponentMobility::Movable);
		Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Panel->SetCastShadow(false);
		Panel->bAffectDistanceFieldLighting = false;
		Panels.Add(Panel);
	}

	WallMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Teleop/Materials/M_TeleopUnlitColor.M_TeleopUnlitColor")));

	LayoutWalls();
}

void ATeleopOperatorRoom::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	LayoutWalls();

	if (UMaterialInterface* Base = WallMaterial.LoadSynchronous())
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
		MID->SetVectorParameterValue(TEXT("Color"), WallColor);
		for (UStaticMeshComponent* Panel : Panels)
		{
			Panel->SetMaterial(0, MID);
		}
	}
}

void ATeleopOperatorRoom::LayoutWalls()
{
	if (Panels.Num() != 6)
	{
		return;
	}

	// The engine cube is 100 cm, pivot at its center.
	const float T = WallThickness;
	const float HX = InnerSize.X * 0.5f;
	const float HY = InnerSize.Y * 0.5f;
	const float H = InnerSize.Z;
	const float OuterX = InnerSize.X + 2.f * T;
	const float OuterY = InnerSize.Y + 2.f * T;
	const float OuterZ = H + 2.f * T;

	auto Place = [](UStaticMeshComponent* Panel, const FVector& Center, const FVector& SizeCm)
	{
		Panel->SetRelativeLocation(Center);
		Panel->SetRelativeScale3D(SizeCm / 100.f);
	};

	Place(Panels[0], FVector(0.f, 0.f, -T * 0.5f),        FVector(OuterX, OuterY, T)); // floor, top face at Z = 0
	Place(Panels[1], FVector(0.f, 0.f, H + T * 0.5f),     FVector(OuterX, OuterY, T)); // ceiling
	Place(Panels[2], FVector(HX + T * 0.5f, 0.f, H * 0.5f),  FVector(T, OuterY, OuterZ)); // front
	Place(Panels[3], FVector(-HX - T * 0.5f, 0.f, H * 0.5f), FVector(T, OuterY, OuterZ)); // back
	Place(Panels[4], FVector(0.f, HY + T * 0.5f, H * 0.5f),  FVector(OuterX, T, OuterZ)); // right
	Place(Panels[5], FVector(0.f, -HY - T * 0.5f, H * 0.5f), FVector(OuterX, T, OuterZ)); // left
}
