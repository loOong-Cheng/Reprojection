#include "TeleopOperatorPawn.h"

#include "Reprojection.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Materials/MaterialInterface.h"
#include "MotionControllerComponent.h"
#include "UObject/ConstructorHelpers.h"

static TAutoConsoleVariable<int32> CVarTeleopEyeTest(
	TEXT("teleop.EyeTest"),
	1,
	TEXT("Per-eye check (HANDOFF S1).\n")
	TEXT(" 0 = off\n")
	TEXT(" 1 = full-screen post process: left eye red, right eye green (pixel stage)\n")
	TEXT(" 2 = panel colored in the vertex shader: left eye red, right eye green (vertex stage)"),
	ECVF_Default);

ATeleopOperatorPawn::ATeleopOperatorPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	VROrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VROrigin"));
	RootComponent = VROrigin;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(VROrigin);
	Camera->bLockToHmd = true;
	Camera->PostProcessBlendWeight = 1.f;

	LeftController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("LeftController"));
	LeftController->SetupAttachment(VROrigin);
	LeftController->SetTrackingMotionSource(TEXT("LeftGrip"));

	RightController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("RightController"));
	RightController->SetupAttachment(VROrigin);
	RightController->SetTrackingMotionSource(TEXT("RightGrip"));

	// Plane faces +Z; pitch 90 turns its normal to -X, i.e. toward the seated operator.
	EyeTestPanel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EyeTestPanel"));
	EyeTestPanel->SetupAttachment(VROrigin);
	EyeTestPanel->SetRelativeLocation(FVector(150.f, 0.f, 120.f));
	EyeTestPanel->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
	EyeTestPanel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EyeTestPanel->SetCastShadow(false);
	EyeTestPanel->SetVisibility(false);
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

	if (UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled())
	{
		// Seated operator, but floor-referenced so the dark room's floor matches the real one.
		UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(EHMDTrackingOrigin::LocalFloor);
	}
	else
	{
		Camera->SetRelativeLocation(FVector(0.f, 0.f, DesktopEyeHeight));
	}

	LoadedEyeTestPostProcess = EyeTestPostProcessMaterial.LoadSynchronous();
	if (!LoadedEyeTestPostProcess)
	{
		UE_LOG(LogTeleop, Warning, TEXT("Eye test post-process material not found: %s"), *EyeTestPostProcessMaterial.ToString());
	}

	if (UMaterialInterface* VertexMat = EyeTestVertexMaterial.LoadSynchronous())
	{
		EyeTestPanel->SetMaterial(0, VertexMat);
	}
	else
	{
		UE_LOG(LogTeleop, Warning, TEXT("Eye test vertex material not found: %s"), *EyeTestVertexMaterial.ToString());
	}
}

void ATeleopOperatorPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const int32 Mode = CVarTeleopEyeTest.GetValueOnGameThread();
	if (Mode != AppliedEyeTestMode)
	{
		ApplyEyeTestMode(Mode);
	}
}

void ATeleopOperatorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Debug input is built in code so it needs no assets; the real controls (HANDOFF §8) come later.
	if (!CycleEyeTestAction)
	{
		CycleEyeTestAction = NewObject<UInputAction>(this, TEXT("IA_CycleEyeTest"));
		CycleEyeTestAction->ValueType = EInputActionValueType::Boolean;

		DebugMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_TeleopDebug"));
		DebugMappingContext->MapKey(CycleEyeTestAction, EKeys::OculusTouch_Right_A_Click);
		DebugMappingContext->MapKey(CycleEyeTestAction, EKeys::T);
	}

	if (const APlayerController* PC = GetController<APlayerController>())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DebugMappingContext, /*Priority*/ 10);
		}
	}

	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		Input->BindAction(CycleEyeTestAction, ETriggerEvent::Started, this, &ATeleopOperatorPawn::CycleEyeTestMode);
	}
	else
	{
		UE_LOG(LogTeleop, Warning, TEXT("Input component is not an EnhancedInputComponent; debug controls disabled."));
	}
}

void ATeleopOperatorPawn::CycleEyeTestMode()
{
	const int32 Next = (CVarTeleopEyeTest.GetValueOnGameThread() + 1) % 3;
	CVarTeleopEyeTest->Set(Next, ECVF_SetByConsole);
}

void ATeleopOperatorPawn::ApplyEyeTestMode(int32 Mode)
{
	if (LoadedEyeTestPostProcess)
	{
		Camera->PostProcessSettings.RemoveBlendable(LoadedEyeTestPostProcess);
		if (Mode == 1)
		{
			Camera->PostProcessSettings.AddBlendable(LoadedEyeTestPostProcess, 1.f);
		}
	}

	EyeTestPanel->SetVisibility(Mode == 2);

	AppliedEyeTestMode = Mode;
	UE_LOG(LogTeleop, Log, TEXT("teleop.EyeTest = %d"), Mode);
}
