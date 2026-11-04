#include "RemoteHand.h"

#include "Reprojection.h"
#include "TeleopCVars.h"
#include "TeleopGameMode.h"
#include "TeleopOperatorPawn.h"

#include "Animation/AnimInstance.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "MotionControllerComponent.h"

namespace
{
	/** Sets a Blueprint float ("real" = double) or bool variable by name; false if it does not exist. */
	bool SetBlueprintNumber(UObject* Target, FName Name, double Value)
	{
		FProperty* Prop = Target ? Target->GetClass()->FindPropertyByName(Name) : nullptr;
		if (FDoubleProperty* D = CastField<FDoubleProperty>(Prop))
		{
			D->SetPropertyValue_InContainer(Target, Value);
			return true;
		}
		if (FFloatProperty* F = CastField<FFloatProperty>(Prop))
		{
			F->SetPropertyValue_InContainer(Target, float(Value));
			return true;
		}
		if (FBoolProperty* B = CastField<FBoolProperty>(Prop))
		{
			B->SetPropertyValue_InContainer(Target, Value != 0.0);
			return true;
		}
		return false;
	}

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

	HandMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HandMesh"));
	HandMesh->SetupAttachment(Root);
	HandMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HandMesh->SetCastShadow(true);
	// Only the robot's scene captures render it, which may not count as "rendered" for the anim update.
	HandMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	GrabComponentClass = TSoftClassPtr<USceneComponent>(FSoftObjectPath(TEXT("/Game/XRFramework/Blueprints/BP_GrabComponent.BP_GrabComponent_C")));
	LeftHandMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/XRMannequins/Meshes/SKM_MannyXR_left.SKM_MannyXR_left")));
	RightHandMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/XRMannequins/Meshes/SKM_MannyXR_right.SKM_MannyXR_right")));
	HandAnimClass = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(TEXT("/Game/XRMannequins/Meshes/ABP_MannequinsXR.ABP_MannequinsXR_C")));
	HandMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/XRMannequins/Materials/Instances/Manny/MI_Manny_02.MI_Manny_02")));
}

bool ARemoteHand::ConfigureTemplateHand(USkeletalMeshComponent* Mesh, bool bLeft, UMaterialInterface* Material)
{
	const ARemoteHand* Defaults = GetDefault<ARemoteHand>();
	const TSoftObjectPtr<USkeletalMesh>& MeshPath = bLeft ? Defaults->LeftHandMesh : Defaults->RightHandMesh;
	USkeletalMesh* SkelMesh = MeshPath.LoadSynchronous();
	UClass* AnimClass = Defaults->HandAnimClass.LoadSynchronous();
	if (!Mesh || !SkelMesh || !AnimClass)
	{
		UE_LOG(LogTeleop, Warning, TEXT("RemoteHand: Template hand assets not found (%s, %s)"), *MeshPath.ToString(), *Defaults->HandAnimClass.ToString());
		return false;
	}
	// Same placement on the grip as the Template's BP_XRPawn HandLeft / HandRight.
	Mesh->SetRelativeLocationAndRotation(
		FVector(-2.98126, bLeft ? -3.5 : 3.5, 4.561753),
		bLeft ? FRotator(-25.0, -180.0, 90.0) : FRotator(25.0, 0.0, 90.0));
	Mesh->SetSkeletalMesh(SkelMesh);
	Mesh->SetAnimInstanceClass(AnimClass);
	if (UMaterialInterface* M = Material ? Material : Defaults->HandMaterial.LoadSynchronous())
	{
		Mesh->SetMaterial(0, M);
	}
	return true;
}

bool ARemoteHand::ApplyFingers(UAnimInstance* Anim, const FTeleopFingerState& F, bool bLeft)
{
	// ABP_MannequinsXR reads bMirror from a BP_MannequinsXR component; ours are plain ones, so set it directly.
	bool bOk = SetBlueprintNumber(Anim, TEXT("bMirror"), bLeft ? 1.0 : 0.0);
	bOk &= SetBlueprintNumber(Anim, TEXT("PoseAlphaGrasp"), F.Grasp);
	bOk &= SetBlueprintNumber(Anim, TEXT("PoseAlphaIndexCurl"), F.IndexCurl);
	bOk &= SetBlueprintNumber(Anim, TEXT("PoseAlphaPoint"), F.Point);
	bOk &= SetBlueprintNumber(Anim, TEXT("PoseAlphaThumbUp"), F.ThumbUp);
	return bOk;
}

USceneComponent* ARemoteHand::GetGripPoint() const
{
	return GripPoint;
}

void ARemoteHand::SetupHandMesh()
{
	ConfigureTemplateHand(HandMesh, bLeft);
}

FTeleopFingerState ARemoteHand::UpdateFingers(double SampleTime)
{
	FTeleopFingerState F;
	UAnimInstance* Anim = HandMesh->GetAnimInstance();
	const ATeleopOperatorPawn* P = Pawn.Get();
	if (!Anim || !P)
	{
		return F;
	}
	P->GetFingerHistory(bLeft).Sample(SampleTime, F);
	F.Grasp = FMath::Max(F.Grasp, HeldComponent ? 1.f : 0.f);

	if (!ApplyFingers(Anim, F, bLeft) && !bWarnedAnimVars)
	{
		bWarnedAnimVars = true;
		UE_LOG(LogTeleop, Warning, TEXT("RemoteHand: %s lacks bMirror / PoseAlpha* variables; fingers stay idle"), *GetNameSafe(Anim->GetClass()));
	}
	return F;
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

	SetupHandMesh();

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
	const bool bShowHand = TeleopCVars::DebugHideRobotHands.GetValueOnGameThread() == 0;
	if (HandMesh->IsVisible() != bShowHand)
	{
		HandMesh->SetVisibility(bShowHand);
		HandMesh->SetComponentTickEnabled(bShowHand);
	}
	const FTeleopFingerState Fingers = UpdateFingers(Now - CmdLatency);

	const float LogEvery = TeleopCVars::DebugLogHandsSec.GetValueOnGameThread();
	if (LogEvery > 0.f && Now - LastHandLogTime >= LogEvery)
	{
		LastHandLogTime = Now;
		const FVector Hand = GripPoint->GetComponentLocation();
		const USceneComponent* Near = FindNearestGrabComponent(100.f);
		const FVector Delta = Near ? Near->GetComponentLocation() - Hand : FVector::ZeroVector;
		UE_LOG(LogTeleop, Log, TEXT("[hand] %s at %s valid=%d held=%s nearest=%s delta=%s dist=%.1f fingers grasp=%.2f curl=%.2f point=%.0f thumb=%.0f"),
			bLeft ? TEXT("L") : TEXT("R"), *Hand.ToCompactString(), bValid ? 1 : 0, *GetNameSafe(GetHeldActor()),
			Near ? *GetNameSafe(Near->GetOwner()) : TEXT("none"), *Delta.ToCompactString(), Near ? Delta.Size() : -1.0,
			Fingers.Grasp, Fingers.IndexCurl, Fingers.Point, Fingers.ThumbUp);
	}
}

void ARemoteHand::QueueGrab(bool bPressed, double IssueTime)
{
	const double Delay = GameMode.IsValid() ? GameMode->GetLatencyModel().GetCommandLatencySeconds() : 0.0;
	PendingCommands.Add({ IssueTime + Delay, bPressed });
}

USceneComponent* ARemoteHand::FindNearestGrabComponent(float Radius) const
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
		FCollisionShape::MakeSphere(Radius), Params);

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
	USceneComponent* Target = FindNearestGrabComponent(GrabRadius);
	if (!Target)
	{
		// Feedback for reaching: where the nearest grabbable is, relative to the hand.
		const FVector Hand = GripPoint->GetComponentLocation();
		if (const USceneComponent* Near = FindNearestGrabComponent(100.f))
		{
			const FVector Delta = Near->GetComponentLocation() - Hand;
			UE_LOG(LogTeleop, Log, TEXT("%s hand grab missed: hand at %s, nearest %s %.1f cm away (delta %s)"),
				bLeft ? TEXT("Left") : TEXT("Right"), *Hand.ToCompactString(), *GetNameSafe(Near->GetOwner()), Delta.Size(), *Delta.ToCompactString());
		}
		else
		{
			UE_LOG(LogTeleop, Log, TEXT("%s hand grab missed: hand at %s, nothing grabbable within 1 m"),
				bLeft ? TEXT("Left") : TEXT("Right"), *Hand.ToCompactString());
		}
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
