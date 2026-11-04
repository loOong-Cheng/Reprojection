#include "TeleopOperatorPawn.h"

#include "Reprojection.h"
#include "RobotHead.h"
#include "TeleopCVars.h"
#include "TeleopGameMode.h"
#include "TeleopObserver.h"
#include "TeleopPresenter.h"
#include "InputActionValue.h"

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
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MotionControllerComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr int32 NumRobotCamEdges = 9;

	/** Places a /Engine/BasicShapes/Cylinder (100 cm tall along Z, 100 cm across, pivot at the centre) from A to B. */
	void PlaceEdge(UStaticMeshComponent* Edge, const FVector& A, const FVector& B, float ThicknessCm)
	{
		const FVector D = B - A;
		const double Len = D.Size();
		Edge->SetRelativeLocationAndRotation((A + B) * 0.5, FRotationMatrix::MakeFromZ(D / FMath::Max(Len, 1e-3)).Rotator());
		Edge->SetRelativeScale3D(FVector(ThicknessCm / 100.0, ThicknessCm / 100.0, Len / 100.0));
	}

	/** Overlay in the operator view: no collision or shadow, HUD stencil (passes the presentation post process),
	 *  invisible to scene captures (robot cameras, observer). */
	void SetupOverlayPrimitive(UStaticMeshComponent* C, int32 Stencil)
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetRenderCustomDepth(true);
		C->SetCustomDepthStencilValue(Stencil);
		C->SetHiddenInSceneCapture(true);
		C->SetVisibility(false);
	}
}

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

	// Robot camera marker (teleop.Debug.ShowRobotCamera); its pose is set every tick, the edges when the FOV changes.
	RobotCamMarker = CreateDefaultSubobject<USceneComponent>(TEXT("RobotCamMarker"));
	RobotCamMarker->SetupAttachment(VROrigin);
	RobotCamMarker->SetVisibility(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	RobotCamPoint = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RobotCamPoint"));
	RobotCamPoint->SetupAttachment(RobotCamMarker);
	RobotCamPoint->SetRelativeScale3D(FVector(RobotCamPointCm / 100.f));
	SetupOverlayPrimitive(RobotCamPoint, HudStencilValue);
	if (SphereMesh.Succeeded())
	{
		RobotCamPoint->SetStaticMesh(SphereMesh.Object);
	}
	for (int32 i = 0; i < NumRobotCamEdges; ++i)
	{
		UStaticMeshComponent* Edge = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("RobotCamEdge%d"), i));
		Edge->SetupAttachment(RobotCamMarker);
		SetupOverlayPrimitive(Edge, HudStencilValue);
		if (CylinderMesh.Succeeded())
		{
			Edge->SetStaticMesh(CylinderMesh.Object);
		}
		RobotCamEdges.Add(Edge);
	}
	RobotCamMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Teleop/Materials/M_TeleopUnlitColor.M_TeleopUnlitColor")));

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

	// Unlit: the operator room has no light.
	if (UMaterialInterface* Unlit = RobotCamMaterial.LoadSynchronous())
	{
		RobotCamMID = UMaterialInstanceDynamic::Create(Unlit, this);
		RobotCamMID->SetVectorParameterValue(TEXT("Color"), RobotCamColor);
		RobotCamPoint->SetMaterial(0, RobotCamMID);
		for (UStaticMeshComponent* Edge : RobotCamEdges)
		{
			Edge->SetMaterial(0, RobotCamMID);
		}
	}
}

void ATeleopOperatorPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	RecordPoses(FPlatformTime::Seconds());

	const float LogEvery = TeleopCVars::DebugLogHandsSec.GetValueOnGameThread();
	if (LogEvery > 0.f && FPlatformTime::Seconds() - LastHeadLogTime >= LogEvery)
	{
		LastHeadLogTime = FPlatformTime::Seconds();
		FTransform Head;
		if (GetLatestLocalPose(ETeleopDevice::Head, Head))
		{
			UE_LOG(LogTeleop, Log, TEXT("[head] local %s yaw %.1f pitch %.1f tracked=%d"), *Head.GetLocation().ToCompactString(),
				Head.Rotator().Yaw, Head.Rotator().Pitch, bHeadTracked ? 1 : 0);
		}
	}

	const int32 Mode = TeleopCVars::EyeTest.GetValueOnGameThread();
	if (Mode != AppliedEyeTestMode)
	{
		ApplyEyeTestMode(Mode);
	}

	UpdateRobotCameraMarker();

	const bool bHud = TeleopCVars::DebugHideHud.GetValueOnGameThread() == 0;
	if (HudText->IsVisible() != bHud)
	{
		HudText->SetVisibility(bHud);
	}
}

void ATeleopOperatorPawn::UpdateRobotCameraMarker()
{
	const ATeleopGameMode* GM = GetWorld()->GetAuthGameMode<ATeleopGameMode>();
	const ARobotHead* RH = GM ? GM->GetRobotHead() : nullptr;
	const bool bShow = TeleopCVars::DebugShowRobotCamera.GetValueOnGameThread() != 0 && RH && RH->GetCameraRig();
	if (bShow != bMarkerShown)
	{
		RobotCamMarker->SetVisibility(bShow, /*bPropagateToChildren*/ true);
		bMarkerShown = bShow;
	}
	if (!bShow)
	{
		return;
	}

	// Frustum in the camera frame (looks along +X), same shape as the observer view's.
	const float Fov = RH->HorizontalFovDeg;
	const float Aspect = float(RH->CaptureWidth) / FMath::Max(RH->CaptureHeight, 1);
	if (Fov != MarkerFovDeg || Aspect != MarkerAspect)
	{
		MarkerFovDeg = Fov;
		MarkerAspect = Aspect;
		const double TanX = FMath::Tan(FMath::DegreesToRadians(Fov * 0.5));
		const double TanY = TanX / FMath::Max(Aspect, 0.1f);
		const double D = RobotCamFrustumCm;
		const FVector Corner[4] = { FVector(D, -TanX * D, TanY * D), FVector(D, TanX * D, TanY * D),
			FVector(D, TanX * D, -TanY * D), FVector(D, -TanX * D, -TanY * D) };
		for (int32 i = 0; i < 4; ++i)
		{
			PlaceEdge(RobotCamEdges[i], FVector::ZeroVector, Corner[i], RobotCamLineCm);
			PlaceEdge(RobotCamEdges[4 + i], Corner[i], Corner[(i + 1) % 4], RobotCamLineCm);
		}
		// "Up" tick on the top edge, so roll is visible.
		const FVector TopMid = (Corner[0] + Corner[1]) * 0.5;
		PlaceEdge(RobotCamEdges[8], TopMid, TopMid + FVector(0.0, 0.0, D * 0.15), RobotCamLineCm);
	}

	// The robot camera now (remote world) in the tracking frame: remote = local * T_map. The marker hangs on VROrigin,
	// so it is right in the room (M0-M4) and, with the pawn moved onto the station, in the remote world (GT).
	FTransform Rig = RH->GetCameraRig()->GetComponentTransform();
	Rig.SetScale3D(FVector::OneVector);
	FTransform Local = Rig.GetRelativeTransform(GM->GetLocalToRemote());
	Local.SetScale3D(FVector::OneVector);
	RobotCamMarker->SetRelativeTransform(Local);
}

void ATeleopOperatorPawn::ToggleRobotCameraMarker(const TCHAR* Source)
{
	UE_LOG(LogTeleop, Log, TEXT("[input] %s: robot camera marker"), Source);
	const int32 Next = TeleopCVars::DebugShowRobotCamera.GetValueOnGameThread() != 0 ? 0 : 1;
	TeleopCVars::DebugShowRobotCamera->Set(Next, ECVF_SetByConsole);
	UE_LOG(LogTeleop, Log, TEXT("[control] teleop.Debug.ShowRobotCamera = %d"), Next);
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
		bHeadTracked = false;
		if (GEngine->XRSystem->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, Orientation, Position))
		{
			// Until tracking starts (Link right after the session starts, headset not worn) the runtime reports an
			// all-zero pose, sometimes with IsTracking() already true. Never record that one.
			const bool bHasData = !Position.IsNearlyZero(0.01) || !Orientation.Equals(FQuat::Identity, 1e-6f);
			Head = FTransform(Orientation, Position);
			bHeadTracked = bHasData && GEngine->XRSystem->IsTracking(IXRTrackingSystem::HMDDeviceId);
			bHeadValid = bHasData && (bHeadTracked || bHasPose[0]);
		}
	}
	else
	{
		// Desktop: fixed seated eye, optionally a synthetic yaw sweep for tests.
		const float Amp = TeleopCVars::DebugHeadYawAmpDeg.GetValueOnGameThread();
		const float Hz = TeleopCVars::DebugHeadYawHz.GetValueOnGameThread();
		const float Yaw = TeleopCVars::DebugHeadYawDeg.GetValueOnGameThread() + (Amp != 0.f ? Amp * FMath::Sin(2.0 * PI * Hz * Now) : 0.f);
		const FVector Offset(TeleopCVars::DebugHeadOffsetX.GetValueOnGameThread(), TeleopCVars::DebugHeadOffsetY.GetValueOnGameThread(),
			TeleopCVars::DebugHeadOffsetZ.GetValueOnGameThread());
		Head = FTransform(FRotator(TeleopCVars::DebugHeadPitchDeg.GetValueOnGameThread(), Yaw, 0.f), FVector(0.f, 0.f, DesktopEyeHeight) + Offset);
		// The robot follows the head centre (recorded below); the view renders from the left eye (teleop.Debug.DesktopEye).
		const float HalfIpdCm = TeleopCVars::DebugDesktopEye.GetValueOnGameThread() != 0
			? FMath::Clamp(TeleopCVars::BaselineMm.GetValueOnGameThread(), 0.f, 200.f) * 0.05f : 0.f;
		Camera->SetRelativeLocationAndRotation(Head.GetLocation() + Head.GetRotation().RotateVector(FVector(0.f, -HalfIpdCm, 0.f)), Head.GetRotation());
		bHeadValid = true;
		bHeadTracked = true;
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
	FingerHistories[0].Add(Now, Fingers[0]);
	FingerHistories[1].Add(Now, Fingers[1]);
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

FString ATeleopOperatorPawn::GetHudText() const
{
	return HudText->Text.ToString();
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

	auto MakeAxis = [this](const TCHAR* Name, const FKey& Key) -> UInputAction*
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = EInputActionValueType::Axis1D;
		MappingContext->MapKey(Action, Key);
		Actions.Add(Action);
		return Action;
	};

	// The triggers only move the robot's index fingers (Template hand), so no mode switch may sit on them: HANDOFF §8's
	// duplicate A-or-X / B-or-Y buttons are split instead (X = neck mode, Y = jitter buffer).
	UInputAction* CycleMethod = MakeAction(TEXT("IA_CycleMethod"), { EKeys::OculusTouch_Right_A_Click, EKeys::M });
	UInputAction* Recenter = MakeAction(TEXT("IA_Recenter"), { EKeys::OculusTouch_Right_Thumbstick_Click, EKeys::R });
	UInputAction* GrabLeft = MakeAction(TEXT("IA_GrabLeft"), { EKeys::OculusTouch_Left_Grip_Axis });
	UInputAction* GrabRight = MakeAction(TEXT("IA_GrabRight"), { EKeys::OculusTouch_Right_Grip_Axis });
	UInputAction* LatencyPreset = MakeAction(TEXT("IA_LatencyPreset"), { EKeys::OculusTouch_Right_B_Click });
	UInputAction* JitterBuffer = MakeAction(TEXT("IA_JitterBuffer"), { EKeys::OculusTouch_Left_Y_Click, EKeys::J });
	UInputAction* NeckMode = MakeAction(TEXT("IA_NeckMode"), { EKeys::OculusTouch_Left_X_Click, EKeys::N });
	UInputAction* DepthPreset = MakeAction(TEXT("IA_DepthPreset"), { EKeys::OculusTouch_Left_Thumbstick_Click });
	UInputAction* Blind = MakeAction(TEXT("IA_Blind"), { EKeys::OculusTouch_Left_Menu_Click });
	UInputAction* ObserverView = MakeAction(TEXT("IA_ObserverView"), { EKeys::V }); // PC keyboard: observer camera
	UInputAction* RobotCamera = MakeAction(TEXT("IA_RobotCamera"), { EKeys::C });   // PC keyboard; HMD: right stick Y
	UInputAction* RightX = MakeAxis(TEXT("IA_RightStickX"), EKeys::OculusTouch_Right_Thumbstick_X);
	UInputAction* RightY = MakeAxis(TEXT("IA_RightStickY"), EKeys::OculusTouch_Right_Thumbstick_Y);
	UInputAction* LeftX = MakeAxis(TEXT("IA_LeftStickX"), EKeys::OculusTouch_Left_Thumbstick_X);
	UInputAction* LeftY = MakeAxis(TEXT("IA_LeftStickY"), EKeys::OculusTouch_Left_Thumbstick_Y);

	Input->BindAction(CycleMethod, ETriggerEvent::Started, this, &ATeleopOperatorPawn::OnCycleMethod);
	Input->BindAction(Recenter, ETriggerEvent::Started, this, &ATeleopOperatorPawn::OnRecenter);
	Input->BindAction(GrabLeft, ETriggerEvent::Started, this, &ATeleopOperatorPawn::OnGrabLeftPressed);
	Input->BindAction(GrabLeft, ETriggerEvent::Completed, this, &ATeleopOperatorPawn::OnGrabLeftReleased);
	Input->BindAction(GrabRight, ETriggerEvent::Started, this, &ATeleopOperatorPawn::OnGrabRightPressed);
	Input->BindAction(GrabRight, ETriggerEvent::Completed, this, &ATeleopOperatorPawn::OnGrabRightReleased);
	Input->BindAction(LatencyPreset, ETriggerEvent::Started, this, &ATeleopOperatorPawn::OnCycleLatencyPreset);
	Input->BindAction(JitterBuffer, ETriggerEvent::Started, this, &ATeleopOperatorPawn::OnToggleJitterBuffer);
	Input->BindAction(NeckMode, ETriggerEvent::Started, this, &ATeleopOperatorPawn::OnCycleNeckMode);
	Input->BindAction(DepthPreset, ETriggerEvent::Started, this, &ATeleopOperatorPawn::OnCycleDepthPreset);
	Input->BindAction(Blind, ETriggerEvent::Started, this, &ATeleopOperatorPawn::OnToggleBlind);
	Input->BindActionValueLambda(ObserverView, ETriggerEvent::Started, [this](const FInputActionValue&)
	{
		UE_LOG(LogTeleop, Log, TEXT("[input] V: observer view"));
		const ATeleopGameMode* GM = GetWorld()->GetAuthGameMode<ATeleopGameMode>();
		if (ATeleopObserver* O = GM ? GM->GetObserver() : nullptr) { O->CycleView(); }
	});
	Input->BindActionValueLambda(RobotCamera, ETriggerEvent::Started, [this](const FInputActionValue&)
	{
		ToggleRobotCameraMarker(TEXT("C"));
	});
	for (const ETriggerEvent Event : { ETriggerEvent::Triggered, ETriggerEvent::Completed })
	{
		Input->BindAction(RightX, Event, this, &ATeleopOperatorPawn::OnRightStickX);
		Input->BindAction(RightY, Event, this, &ATeleopOperatorPawn::OnRightStickY);
		Input->BindAction(LeftX, Event, this, &ATeleopOperatorPawn::OnLeftStickX);
		Input->BindAction(LeftY, Event, this, &ATeleopOperatorPawn::OnLeftStickY);
	}

	// Finger controls for the robot hands, same keys as the Template's IMC_Hands. Like the Template, the hand points
	// while the index finger is off the trigger and gives a thumb up while the thumb is off the buttons and stick.
	auto BindFingers = [&](int32 Hand, const TCHAR* Side, const FKey& Grip, const FKey& Trigger, const FKey& TriggerTouch,
		std::initializer_list<FKey> ThumbTouch)
	{
		UInputAction* Grasp = MakeAxis(*FString::Printf(TEXT("IA_Grasp%s"), Side), Grip);
		UInputAction* Curl = MakeAxis(*FString::Printf(TEXT("IA_IndexCurl%s"), Side), Trigger);
		UInputAction* IndexTouch = MakeAction(*FString::Printf(TEXT("IA_IndexTouch%s"), Side), { TriggerTouch });
		UInputAction* Thumb = MakeAction(*FString::Printf(TEXT("IA_ThumbTouch%s"), Side), ThumbTouch);
		Input->BindActionValueLambda(Grasp, ETriggerEvent::Triggered, [this, Hand](const FInputActionValue& V) { Fingers[Hand].Grasp = V.Get<float>(); });
		Input->BindActionValueLambda(Grasp, ETriggerEvent::Completed, [this, Hand](const FInputActionValue&) { Fingers[Hand].Grasp = 0.f; });
		Input->BindActionValueLambda(Curl, ETriggerEvent::Triggered, [this, Hand](const FInputActionValue& V) { Fingers[Hand].IndexCurl = V.Get<float>(); });
		Input->BindActionValueLambda(Curl, ETriggerEvent::Completed, [this, Hand](const FInputActionValue&) { Fingers[Hand].IndexCurl = 0.f; });
		Input->BindActionValueLambda(IndexTouch, ETriggerEvent::Started, [this, Hand](const FInputActionValue&) { Fingers[Hand].Point = 0.f; });
		Input->BindActionValueLambda(IndexTouch, ETriggerEvent::Completed, [this, Hand](const FInputActionValue&) { Fingers[Hand].Point = 1.f; });
		Input->BindActionValueLambda(Thumb, ETriggerEvent::Started, [this, Hand](const FInputActionValue&) { Fingers[Hand].ThumbUp = 0.f; });
		Input->BindActionValueLambda(Thumb, ETriggerEvent::Completed, [this, Hand](const FInputActionValue&) { Fingers[Hand].ThumbUp = 1.f; });
	};
	BindFingers(0, TEXT("L"), EKeys::OculusTouch_Left_Grip_Axis, EKeys::OculusTouch_Left_Trigger_Axis, EKeys::OculusTouch_Left_Trigger_Touch,
		{ EKeys::OculusTouch_Left_X_Touch, EKeys::OculusTouch_Left_Y_Touch, EKeys::OculusTouch_Left_Thumbstick_Touch });
	BindFingers(1, TEXT("R"), EKeys::OculusTouch_Right_Grip_Axis, EKeys::OculusTouch_Right_Trigger_Axis, EKeys::OculusTouch_Right_Trigger_Touch,
		{ EKeys::OculusTouch_Right_A_Touch, EKeys::OculusTouch_Right_B_Touch, EKeys::OculusTouch_Right_Thumbstick_Touch });

	if (const APlayerController* PC = GetController<APlayerController>())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(MappingContext, /*Priority*/ 10);
		}
	}
}

ATeleopPresenter* ATeleopOperatorPawn::GetPresenter() const
{
	const ATeleopGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ATeleopGameMode>() : nullptr;
	return GM ? GM->GetPresenter() : nullptr;
}

int32 ATeleopOperatorPawn::Flick(float Axis, bool& bArmed)
{
	if (bArmed && FMath::Abs(Axis) > 0.7f)
	{
		bArmed = false;
		return Axis > 0.f ? 1 : -1;
	}
	if (FMath::Abs(Axis) < 0.3f)
	{
		bArmed = true;
	}
	return 0;
}

void ATeleopOperatorPawn::OnCycleMethod()
{
	UE_LOG(LogTeleop, Log, TEXT("[input] A: cycle method"));
	if (ATeleopPresenter* P = GetPresenter()) { P->CycleMethod(); }
}

void ATeleopOperatorPawn::OnCycleLatencyPreset()
{
	UE_LOG(LogTeleop, Log, TEXT("[input] B: latency preset"));
	if (ATeleopPresenter* P = GetPresenter()) { P->CycleLatencyPreset(); }
}

void ATeleopOperatorPawn::OnToggleJitterBuffer()
{
	UE_LOG(LogTeleop, Log, TEXT("[input] Y: jitter buffer"));
	if (ATeleopPresenter* P = GetPresenter()) { P->ToggleJitterBuffer(); }
}

void ATeleopOperatorPawn::OnCycleNeckMode()
{
	UE_LOG(LogTeleop, Log, TEXT("[input] X: neck mode"));
	if (ATeleopPresenter* P = GetPresenter()) { P->CycleNeckMode(); }
}

void ATeleopOperatorPawn::OnCycleDepthPreset()
{
	UE_LOG(LogTeleop, Log, TEXT("[input] left stick click: depth preset"));
	if (ATeleopPresenter* P = GetPresenter()) { P->CycleDepthPreset(); }
}

void ATeleopOperatorPawn::OnToggleBlind()
{
	UE_LOG(LogTeleop, Log, TEXT("[input] left menu: blind test"));
	if (ATeleopPresenter* P = GetPresenter()) { P->ToggleBlindTest(); }
}

void ATeleopOperatorPawn::OnRightStickX(const FInputActionValue& Value)
{
	const int32 D = Flick(Value.Get<float>(), bRightXArmed);
	if (D != 0)
	{
		UE_LOG(LogTeleop, Log, TEXT("[input] right stick X flick %+d: base latency"), D);
		if (ATeleopPresenter* P = GetPresenter()) { P->AdjustBaseLatency(D); }
	}
}

void ATeleopOperatorPawn::OnRightStickY(const FInputActionValue& Value)
{
	const int32 D = Flick(Value.Get<float>(), bRightYArmed);
	if (D == 0)
	{
		return;
	}
	// The rating needs the stick only during the blind test; outside it, up or down toggles the robot camera marker.
	ATeleopPresenter* P = GetPresenter();
	if (P && P->IsBlindTestActive())
	{
		UE_LOG(LogTeleop, Log, TEXT("[input] right stick Y flick %+d: blind rating"), D);
		P->AdjustRating(D);
	}
	else
	{
		ToggleRobotCameraMarker(D > 0 ? TEXT("right stick Y flick +1") : TEXT("right stick Y flick -1"));
	}
}

void ATeleopOperatorPawn::OnLeftStickX(const FInputActionValue& Value)
{
	const int32 D = Flick(Value.Get<float>(), bLeftXArmed);
	if (D != 0)
	{
		UE_LOG(LogTeleop, Log, TEXT("[input] left stick X flick %+d: baseline"), D);
		if (ATeleopPresenter* P = GetPresenter()) { P->AdjustBaseline(D); }
	}
}

void ATeleopOperatorPawn::OnLeftStickY(const FInputActionValue& Value)
{
	const int32 D = Flick(Value.Get<float>(), bLeftYArmed);
	if (D != 0)
	{
		UE_LOG(LogTeleop, Log, TEXT("[input] left stick Y flick %+d: sphere radius"), D);
		if (ATeleopPresenter* P = GetPresenter()) { P->AdjustSphereRadius(D); }
	}
}

void ATeleopOperatorPawn::OnRecenter()
{
	UE_LOG(LogTeleop, Log, TEXT("[input] right stick click: recenter"));
	if (ATeleopGameMode* GM = GetWorld()->GetAuthGameMode<ATeleopGameMode>())
	{
		GM->Recenter();
	}
}

void ATeleopOperatorPawn::ForwardGrab(bool bLeft, bool bPressed)
{
	UE_LOG(LogTeleop, Log, TEXT("[input] %s grip %s"), bLeft ? TEXT("left") : TEXT("right"), bPressed ? TEXT("pressed") : TEXT("released"));
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
