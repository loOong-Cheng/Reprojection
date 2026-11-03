#include "RemoteHand.h"

#include "Reprojection.h"
#include "TeleopGameMode.h"
#include "TeleopOperatorPawn.h"

#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MotionControllerComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/**
	 * Calls a Blueprint function on Target by name. Object parameters (inputs) get ObjectArg; returns the
	 * first bool output (BP functions return through out-params) in bOutResult.
	 */
	bool CallBlueprintFunction(UObject* Target, FName FunctionName, UObject* ObjectArg, bool& bOutResult)
	{
		bOutResult = false;
		UFunction* Function = Target ? Target->FindFunction(FunctionName) : nullptr;
		if (!Function)
		{
			UE_LOG(LogTeleop, Warning, TEXT("%s has no function %s"), *GetNameSafe(Target), *FunctionName.ToString());
			return false;
		}

		uint8* Params = static_cast<uint8*>(FMemory_Alloca(FMath::Max<int32>(Function->ParmsSize, 1)));
		FMemory::Memzero(Params, Function->ParmsSize);
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Params);
			if (!It->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm))
			{
				if (FObjectPropertyBase* ObjectProp = CastField<FObjectPropertyBase>(*It))
				{
					ObjectProp->SetObjectPropertyValue_InContainer(Params, ObjectArg);
				}
			}
		}

		Target->ProcessEvent(Function, Params);

		bool bFoundResult = false;
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (!bFoundResult && It->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm))
			{
				if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(*It))
				{
					bOutResult = BoolProp->GetPropertyValue_InContainer(Params);
					bFoundResult = true;
				}
			}
			It->DestroyValue_InContainer(Params);
		}
		return true;
	}
}

ARemoteHand::ARemoteHand()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	GripPoint = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("GripPoint"));
	GripPoint->SetupAttachment(Root);
	GripPoint->SetAutoActivate(false); // never polls a device; follows this actor
	GripPoint->bDisableLowLatencyUpdate = true;

	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Root);
	Visual->SetRelativeScale3D(FVector(0.07f));
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCastShadow(true);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Visual->SetStaticMesh(SphereMesh.Object);
	}

	GrabComponentClass = TSoftClassPtr<USceneComponent>(FSoftObjectPath(TEXT("/Game/XRFramework/Blueprints/BP_GrabComponent.BP_GrabComponent_C")));
}

void ARemoteHand::BeginPlay()
{
	Super::BeginPlay();
	LoadedGrabClass = GrabComponentClass.LoadSynchronous();
	if (!LoadedGrabClass)
	{
		UE_LOG(LogTeleop, Warning, TEXT("RemoteHand: grab component class not found: %s"), *GrabComponentClass.ToString());
	}
}

void ARemoteHand::Init(bool bInLeft, ATeleopGameMode* InGameMode, ATeleopOperatorPawn* InPawn)
{
	bLeft = bInLeft;
	GameMode = InGameMode;
	Pawn = InPawn;

	// The Template's TryGrab picks the haptic hand from the motion source.
	GripPoint->SetTrackingMotionSource(bLeft ? TEXT("LeftGrip") : TEXT("RightGrip"));

	static const FSoftObjectPath HandMaterialPath(TEXT("/Game/Teleop/Materials/M_TeleopLitColor.M_TeleopLitColor"));
	if (UMaterialInterface* Base = Cast<UMaterialInterface>(HandMaterialPath.TryLoad()))
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
		MID->SetVectorParameterValue(TEXT("Color"), bLeft ? FLinearColor(0.1f, 0.35f, 1.f) : FLinearColor(1.f, 0.45f, 0.1f));
		Visual->SetMaterial(0, MID);
	}

	if (InPawn)
	{
		AddTickPrerequisiteActor(InPawn);
	}
}

void ARemoteHand::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const double Now = FPlatformTime::Seconds();
	ATeleopGameMode* GM = GameMode.Get();
	ATeleopOperatorPawn* P = Pawn.Get();
	if (!GM || !P)
	{
		return;
	}
	const double CmdLatency = GM->GetLatencyModel().GetCommandLatencySeconds();

	FTransform World;
	bool bValid = false;
	if (bHasPoseOverride)
	{
		World = PoseOverride;
		bValid = true;
	}
	else
	{
		FTransform Local;
		if (P->GetLocalHistory(bLeft ? ETeleopDevice::LeftHand : ETeleopDevice::RightHand).Sample(Now - CmdLatency, Local))
		{
			World = Local * GM->GetLocalToRemote();
			bValid = true;
		}
	}
	if (bValid)
	{
		SetActorTransform(World, false, nullptr, ETeleportType::TeleportPhysics);
		WorldHistory.Add(Now, World);
	}

	// Delayed commands, in order.
	while (PendingCommands.Num() > 0 && PendingCommands[0].ExecuteTime <= Now)
	{
		const bool bPressed = PendingCommands[0].bPressed;
		PendingCommands.RemoveAt(0);
		bPressed ? DoGrab() : DoRelease();
	}
}

void ARemoteHand::QueueGrab(bool bPressed, double IssueTime)
{
	const double Delay = GameMode.IsValid() ? GameMode->GetLatencyModel().GetCommandLatencySeconds() : 0.0;
	PendingCommands.Add({ IssueTime + Delay, bPressed });
}

USceneComponent* ARemoteHand::FindNearestGrabComponent() const
{
	UWorld* World = GetWorld();
	if (!World || !LoadedGrabClass)
	{
		return nullptr;
	}

	const FVector Center = GripPoint->GetComponentLocation();
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RemoteHandGrab), false, this);
	World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, FCollisionObjectQueryParams::AllObjects,
		FCollisionShape::MakeSphere(GrabRadius), Params);

	USceneComponent* Best = nullptr;
	double BestDistSq = TNumericLimits<double>::Max();
	TSet<AActor*> Visited;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		if (!Actor || Visited.Contains(Actor))
		{
			continue;
		}
		Visited.Add(Actor);

		TInlineComponentArray<USceneComponent*> Components(Actor);
		for (USceneComponent* Component : Components)
		{
			if (Component && Component->IsA(LoadedGrabClass))
			{
				const double DistSq = FVector::DistSquared(Component->GetComponentLocation(), Center);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					Best = Component;
				}
			}
		}
	}
	return Best;
}

bool ARemoteHand::DoGrab()
{
	if (HeldComponent)
	{
		return false;
	}
	USceneComponent* Target = FindNearestGrabComponent();
	if (!Target)
	{
		return false;
	}

	bool bHeld = false;
	CallBlueprintFunction(Target, TEXT("TryGrab"), GripPoint, bHeld);
	if (bHeld)
	{
		HeldComponent = Target;
		UE_LOG(LogTeleop, Log, TEXT("%s hand grabbed %s"), bLeft ? TEXT("Left") : TEXT("Right"), *GetNameSafe(Target->GetOwner()));
	}
	return bHeld;
}

bool ARemoteHand::DoRelease()
{
	if (!HeldComponent)
	{
		return false;
	}

	// The Template attaches the grab component's parent (usually the physics root) to the GripPoint.
	UPrimitiveComponent* HeldPrimitive = Cast<UPrimitiveComponent>(HeldComponent->GetAttachParent());

	bool bReleased = false;
	CallBlueprintFunction(HeldComponent, TEXT("TryRelease"), nullptr, bReleased);
	if (!bReleased)
	{
		return false;
	}

	// TryRelease re-enables physics but keeps the object at rest; give it the hand's velocity so it can be thrown.
	if (HeldPrimitive && HeldPrimitive->IsSimulatingPhysics())
	{
		FVector Linear, AngularDeg;
		if (WorldHistory.Velocity(FPlatformTime::Seconds(), ThrowVelocityWindow, Linear, AngularDeg))
		{
			HeldPrimitive->SetPhysicsLinearVelocity(Linear);
			HeldPrimitive->SetPhysicsAngularVelocityInDegrees(AngularDeg);
		}
	}

	UE_LOG(LogTeleop, Log, TEXT("%s hand released %s"), bLeft ? TEXT("Left") : TEXT("Right"), *GetNameSafe(HeldComponent->GetOwner()));
	HeldComponent = nullptr;
	return true;
}

AActor* ARemoteHand::GetHeldActor() const
{
	return HeldComponent ? HeldComponent->GetOwner() : nullptr;
}

void ARemoteHand::DebugSetPoseOverride(const FTransform& WorldPose)
{
	bHasPoseOverride = true;
	PoseOverride = WorldPose;
}

void ARemoteHand::DebugClearPoseOverride()
{
	bHasPoseOverride = false;
}

bool ARemoteHand::DebugGrab()
{
	return DoGrab();
}

bool ARemoteHand::DebugRelease()
{
	return DoRelease();
}
