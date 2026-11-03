#include "TeleopOperatorPawn.h"

#include "Reprojection.h"
#include "TeleopCVars.h"
#include "TeleopGameMode.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "IXRTrackingSystem.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Materials/MaterialInterface.h"
#include "MotionControllerComponent.h"
#include "UObject/ConstructorHelpers.h"

ATeleopOperatorPawn::ATeleopOperatorPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	VROrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VROrigin"));
	RootComponent = VROrigin;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(VROrigin);
	Camera->bLockToHmd = true;
	Camera->PostProcessBlendWeight = 1.f;
	// The robot frames are linear HDR and get tonemapped once by this camera; extra lens effects would make
	// the operator view differ from GT for reasons unrelated to compensation.
	Camera->PostProcessSettings.bOverride_BloomIntensity = true;
	Camera->PostProcessSettings.BloomIntensity = 0.f;
	Camera->PostProcessSettings.bOverride_VignetteIntensity = true;
	Camera->PostProcessSettings.VignetteIntensity = 0.f;

	LeftController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("LeftController"));
	LeftController->SetupAttachment(VROrigin);
	LeftController->SetTrackingMotionSource(TEXT("LeftGrip"));

	RightController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("RightController"));
	RightController->SetupAttachment(VROrigin);
	RightController->SetTrackingMotionSource(TEXT("RightGrip"));

	// Below the line of sight, tilted toward the seated eye (HANDOFF §8). Text faces -X (toward the seat).
	HudText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("HudText"));
	HudText->SetupAttachment(VROrigin);
	HudText->SetRelativeLocation(FVector(70.f, 0.f, 88.f));
	HudText->SetRelativeRotation(FRotator(30.f, 180.f, 0.f));
	HudText->SetHorizontalAlignment(EHTA_Center);
	HudText->SetVerticalAlignment(EVRTA_TextCenter);
	HudText->SetWorldSize(1.8f);
	HudText->SetTextRenderColor(FColor(255, 220, 120));
	HudText->SetText(FText::FromString(TEXT("Teleop")));
	HudText->SetCastShadow(false);
	HudText->SetRenderCustomDepth(true);
	HudText->SetCustomDepthStencilValue(HudStencilValue);

	// Plane faces +Z; pitch 90 turns its normal to -X, i.e. toward the seated operator.
	EyeTestPanel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EyeTestPanel"));
	EyeTestPanel->SetupAttachment(VROrigin);
	EyeTestPanel->SetRelativeLocation(FVector(150.f, 0.f, 120.f));
	EyeTestPanel->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
	EyeTestPanel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EyeTestPanel->SetCastShadow(false);
	EyeTestPanel->SetVisibility(false);
	// Overlay like the HUD, so the vertex-stage test is visible through the presentation post process.
	EyeTestPanel->SetRenderCustomDepth(true);
	EyeTestPanel->SetCustomDepthStencilValue(HudStencilValue);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		EyeTestPanel->SetStaticMesh(PlaneMesh.Object);
	}

	EyeTestPostProcessMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Teleop/Materials/M_EyeTest_PP.M_EyeTest_PP")));
	EyeTestVertexMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Teleop/Materials/M_EyeTest_Vertex.M_EyeTest_Vertex")));
}

void ATeleopOperatorPawn::BeginPlay()
{
	Super::BeginPlay();

	RoomTransform = GetActorTransform();

	// Controllers update in their own tick; read them after that.
	PrimaryActorTick.AddPrerequisite(LeftController, LeftController->PrimaryComponentTick);
	PrimaryActorTick.AddPrerequisite(RightController, RightController->PrimaryComponentTick);

	if (UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled())
	{
		// Seated, but floor-referenced, so the remote desk sits at the real desk height (1:1 mapping).
		UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(EHMDTrackingOrigin::LocalFloor);
	}
	else
	{
		Camera->SetRelativeLocation(FVector(0.f, 0.f, DesktopEyeHeight));
	}

	LoadedEyeTestPostProcess = EyeTestPostProcessMaterial.LoadSynchronous();
	if (UMaterialInterface* VertexMat = EyeTestVertexMaterial.LoadSynchronous())
	{
		EyeTestPanel->SetMaterial(0, VertexMat);
	}
}

void ATeleopOperatorPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	RecordPoses(FPlatformTime::Seconds());

	const int32 Mode = TeleopCVars::EyeTest.GetValueOnGameThread();
	if (Mode != AppliedEyeTestMode)
	{
		ApplyEyeTestMode(Mode);
	}
}

void ATeleopOperatorPawn::RecordPoses(double Now)
{
	// Head
	FTransform Head;
	bool bHeadValid = false;
	if (UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled() && GEngine && GEngine->XRSystem.IsValid())
	{
		FQuat Orientation;
		FVector Position;
		if (GEngine->XRSystem->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, Orientation, Position))
		{
			Head = FTransform(Orientation, Position);
			bHeadValid = GEngine->XRSystem->IsTracking(IXRTrackingSystem::HMDDeviceId) || bHasPose[0];
		}
	}
	else
	{
		// Desktop: fixed seated eye, optionally a synthetic yaw sweep for tests.
		const float Amp = TeleopCVars::DebugHeadYawAmpDeg.GetValueOnGameThread();
		const float Hz = TeleopCVars::DebugHeadYawHz.GetValueOnGameThread();
		const float Yaw = Amp != 0.f ? Amp * FMath::Sin(2.0 * PI * Hz * Now) : 0.f;
		Head = FTransform(FRotator(0.f, Yaw, 0.f), FVector(0.f, 0.f, DesktopEyeHeight));
		Camera->SetRelativeLocationAndRotation(Head.GetLocation(), Head.GetRotation());
		bHeadValid = true;
	}
	if (bHeadValid)
	{
		Histories[0].Add(Now, Head);
		bHasPose[0] = true;
	}

	// Hands
	auto RecordHand = [this, Now](UMotionControllerComponent* MC, const FVector& RestPos, int32 Index)
	{
		FTransform Pose = MC->IsTracked()
			? MC->GetRelativeTransform()
			: FTransform(FRotator::ZeroRotator, RestPos);
		Histories[Index].Add(Now, Pose);
		bHasPose[Index] = true;
	};
	RecordHand(LeftController, DesktopLeftHand, 1);
	RecordHand(RightController, DesktopRightHand, 2);
}

bool ATeleopOperatorPawn::GetLatestLocalPose(ETeleopDevice Device, FTransform& OutPose) const
{
	const int32 Index = static_cast<int32>(Device);
	return bHasPose[Index] && Histories[Index].Sample(TNumericLimits<double>::Max(), OutPose);
}

void ATeleopOperatorPawn::SetRemoteView(bool bRemote, const FTransform& RemoteOrigin)
{
	bRemoteView = bRemote;
	SetActorTransform(bRemote ? RemoteOrigin : RoomTransform, false, nullptr, ETeleportType::TeleportPhysics);
	HudText->SetVisibility(!bRemote);
}

void ATeleopOperatorPawn::SetCameraBlendable(UMaterialInterface* Material, bool bEnabled)
{
	if (!Material)
	{
		return;
	}
	Camera->PostProcessSettings.RemoveBlendable(Material);
	if (bEnabled)
	{
		Camera->PostProcessSettings.AddBlendable(Material, 1.f);
	}
}

void ATeleopOperatorPawn::SetHudText(const FString& Text)
{
	HudText->SetText(FText::FromString(Text));
}

void ATeleopOperatorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Built in code so it needs no assets; the full control layout (HANDOFF §8) comes in S7.
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		UE_LOG(LogTeleop, Warning, TEXT("Input component is not an EnhancedInputComponent; controls disabled."));
		return;
	}

	if (!MappingContext)
	{
		MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Teleop"));
	}

	auto MakeAction = [this](const TCHAR* Name, std::initializer_list<FKey> Keys) -> UInputAction*
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = EInputActionValueType::Boolean;
		for (const FKey& Key : Keys)
		{
			MappingContext->MapKey(Action, Key);
		}
		Actions.Add(Action);
		return Action;
	};

	UInputAction* CycleMethod = MakeAction(TEXT("IA_CycleMethod"), { EKeys::OculusTouch_Right_A_Click, EKeys::OculusTouch_Left_X_Click, EKeys::M });
	UInputAction* Recenter = MakeAction(TEXT("IA_Recenter"), { EKeys::OculusTouch_Left_Menu_Click, EKeys::R });
	UInputAction* GrabLeft = MakeAction(TEXT("IA_GrabLeft"), { EKeys::OculusTouch_Left_Grip_Axis });
	UInputAction* GrabRight = MakeAction(TEXT("IA_GrabRight"), { EKeys::OculusTouch_Right_Grip_Axis });

	Input->BindAction(CycleMethod, ETriggerEvent::Started, this, &ATeleopOperatorPawn::OnCycleMethod);
	Input->BindAction(Recenter, ETriggerEvent::Started, this, &ATeleopOperatorPawn::OnRecenter);
	Input->BindAction(GrabLeft, ETriggerEvent::Started, this, &ATeleopOperatorPawn::OnGrabLeftPressed);
	Input->BindAction(GrabLeft, ETriggerEvent::Completed, this, &ATeleopOperatorPawn::OnGrabLeftReleased);
	Input->BindAction(GrabRight, ETriggerEvent::Started, this, &ATeleopOperatorPawn::OnGrabRightPressed);
	Input->BindAction(GrabRight, ETriggerEvent::Completed, this, &ATeleopOperatorPawn::OnGrabRightReleased);

	if (const APlayerController* PC = GetController<APlayerController>())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(MappingContext, /*Priority*/ 10);
		}
	}
}

void ATeleopOperatorPawn::OnCycleMethod()
{
	// Only the implemented methods: M0 -> GT -> M0 (M1-M4 join the cycle as they land).
	const ETeleopMethod Current = static_cast<ETeleopMethod>(TeleopCVars::Method.GetValueOnGameThread());
	const ETeleopMethod Next = Current == ETeleopMethod::GT ? ETeleopMethod::M0_Naive : ETeleopMethod::GT;
	TeleopCVars::Method->Set(static_cast<int32>(Next), ECVF_SetByConsole);
}

void ATeleopOperatorPawn::OnRecenter()
{
	if (ATeleopGameMode* GM = GetWorld()->GetAuthGameMode<ATeleopGameMode>())
	{
		GM->Recenter();
	}
}

void ATeleopOperatorPawn::ForwardGrab(bool bLeft, bool bPressed)
{
	if (ATeleopGameMode* GM = GetWorld()->GetAuthGameMode<ATeleopGameMode>())
	{
		GM->HandleGrabInput(bLeft, bPressed);
	}
}

void ATeleopOperatorPawn::ApplyEyeTestMode(int32 Mode)
{
	SetCameraBlendable(LoadedEyeTestPostProcess, Mode == 1);
	EyeTestPanel->SetVisibility(Mode == 2);
	AppliedEyeTestMode = Mode;
}
