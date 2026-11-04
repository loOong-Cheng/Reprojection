#include "TeleopObserver.h"

#include "RemoteHand.h"
#include "Reprojection.h"
#include "RobotHead.h"
#include "TeleopCVars.h"
#include "TeleopGameMode.h"
#include "TeleopOperatorPawn.h"
#include "TeleopPresenter.h"

#include "Components/LineBatchComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"

namespace
{
	const FLinearColor RobotNowColor(1.f, 0.45f, 0.05f);  // orange: robot camera now
	const FLinearColor ShownColor(1.f, 0.9f, 0.1f);       // yellow: capture pose of the frame on the HMD
	const FLinearColor OperatorColor(0.15f, 1.f, 0.35f);  // green: operator, undelayed
	const FLinearColor GapColor(1.f, 1.f, 1.f);           // white: operator -> robot

	/** Orbit around a target in the station frame: (TargetX forward, 0, calibrated eye height + TargetDz). */
	struct FViewPreset
	{
		float Yaw;   // relative to the station's forward; 0 = behind the robot looking forward
		float Pitch;
		float Distance;
		float TargetX;
		float TargetDz;
		const TCHAR* Name;
	};
	// The rear 3/4 views show the robot head and cameras, the hands and the desk objects together (picked from
	// captures, desktop eye height 120 cm).
	const FViewPreset ViewPresets[] = {
		{ -45.f, -35.f, 200.f, 50.f, -45.f, TEXT("behind-right 45 deg") },
		{ 45.f, -35.f, 200.f, 50.f, -45.f, TEXT("behind-left 45 deg") },
		{ -140.f, -38.f, 200.f, 25.f, -30.f, TEXT("front-right, over the desk") },
		{ -90.f, -12.f, 220.f, 25.f, -30.f, TEXT("right side") },
		{ 0.f, -80.f, 260.f, 25.f, -30.f, TEXT("top") },
	};
	constexpr int32 NumViewPresets = UE_ARRAY_COUNT(ViewPresets);

	const FViewPreset& ActivePreset()
	{
		return ViewPresets[FMath::Clamp(TeleopCVars::ObserverView.GetValueOnGameThread(), 0, NumViewPresets - 1)];
	}

	UStaticMeshComponent* MakeOverlayMesh(AActor* Owner, const TCHAR* Name, UStaticMesh* Mesh, USceneComponent* Parent)
	{
		UStaticMeshComponent* C = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(Parent);
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);           // a hidden caster could still shadow the operator / robot views
		C->SetVisibleInSceneCaptureOnly(true);
		return C;
	}

	struct FLegendEntry
	{
		FLinearColor Color;
		const TCHAR* Text;
	};
	// Bottom to top.
	const FLegendEntry Legend[] = {
		{ GapColor, TEXT("white: operator -> robot (eye -> camera, hand -> hand)") },
		{ ShownColor, TEXT("yellow: capture pose of the frame on the HMD now") },
		{ RobotNowColor, TEXT("orange: robot camera now     grey: robot body, dark hands: robot hands") },
		{ OperatorColor, TEXT("green: operator now (head, view, hands; no delay)") },
	};

	/** The unlit text (M_TeleopTextUnlit) is multiplied by the fixed exposure; pre-divide so the colours neither clip
	 *  (pale) nor sink (dark). 0.5 EV above full compensation was picked by eye. */
	FColor TextColor(const FLinearColor& Color, float ExposureEv)
	{
		return (Color * FMath::Pow(2.f, 0.5f - ExposureEv)).ToFColor(true);
	}

	USkeletalMeshComponent* MakeGhostHand(AActor* Owner, const TCHAR* Name, USceneComponent* Parent)
	{
		USkeletalMeshComponent* C = Owner->CreateDefaultSubobject<USkeletalMeshComponent>(Name);
		C->SetupAttachment(Parent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetVisibleInSceneCaptureOnly(true);
		C->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		return C;
	}
}

ATeleopObserver::ATeleopObserver()
{
	PrimaryActorTick.bCanEverTick = true;
	// After the robot moved and captured, so the view shows this frame's robot pose.
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(Root);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->CaptureSource = SCS_FinalColorLDR;
	Capture->ShowFlags.SetMotionBlur(false);
	// Fixed exposure (teleop.Observer.Exposure): auto exposure adapts to the sky and leaves the robot in the dark, and
	// would make recordings pump when the view changes.
	FPostProcessSettings& PP = Capture->PostProcessSettings;
	PP.bOverride_AutoExposureMethod = true;
	PP.AutoExposureMethod = AEM_Manual;
	PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PP.AutoExposureApplyPhysicalCameraExposure = false;
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = 3.5f;

	Lines = CreateDefaultSubobject<ULineBatchComponent>(TEXT("Lines"));
	Lines->SetupAttachment(Root);
	Lines->SetVisibleInSceneCaptureOnly(true);
	Lines->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	UStaticMesh* Cube = CubeMesh.Object;
	UStaticMesh* Cylinder = CylinderMesh.Object;

	Pedestal = MakeOverlayMesh(this, TEXT("Pedestal"), Cylinder, Root);
	Torso = MakeOverlayMesh(this, TEXT("Torso"), Cube, Root);
	Neck = MakeOverlayMesh(this, TEXT("Neck"), Cylinder, Root);
	Head = MakeOverlayMesh(this, TEXT("Head"), Cube, Root);
	LensLeft = MakeOverlayMesh(this, TEXT("LensLeft"), Cylinder, Root);
	LensRight = MakeOverlayMesh(this, TEXT("LensRight"), Cylinder, Root);
	GhostHmd = MakeOverlayMesh(this, TEXT("GhostHmd"), Cube, Root);

	GhostGripLeft = CreateDefaultSubobject<USceneComponent>(TEXT("GhostGripLeft"));
	GhostGripLeft->SetupAttachment(Root);
	GhostGripRight = CreateDefaultSubobject<USceneComponent>(TEXT("GhostGripRight"));
	GhostGripRight->SetupAttachment(Root);
	GhostHandLeft = MakeGhostHand(this, TEXT("GhostHandLeft"), GhostGripLeft);
	GhostHandRight = MakeGhostHand(this, TEXT("GhostHandRight"), GhostGripRight);

	auto MakeText = [this](const TCHAR* Name, EVerticalTextAligment VAlign)
	{
		UTextRenderComponent* T = CreateDefaultSubobject<UTextRenderComponent>(Name);
		T->SetupAttachment(Capture);
		T->SetRelativeRotation(FRotator(0.f, 180.f, 0.f)); // face the observer camera
		T->SetHorizontalAlignment(EHTA_Left);
		T->SetVerticalAlignment(VAlign);
		T->SetCastShadow(false);
		T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		T->SetVisibleInSceneCaptureOnly(true);
		return T;
	};
	InfoText = MakeText(TEXT("InfoText"), EVRTA_TextTop);
	for (int32 i = 0; i < UE_ARRAY_COUNT(Legend); ++i)
	{
		UTextRenderComponent* T = MakeText(*FString::Printf(TEXT("Legend%d"), i), EVRTA_TextBottom);
		T->SetText(FText::FromString(Legend[i].Text));
		LegendText.Add(T);
	}
	InfoPanel = MakeOverlayMesh(this, TEXT("InfoPanel"), PlaneMesh.Object, Capture);
	LegendPanel = MakeOverlayMesh(this, TEXT("LegendPanel"), PlaneMesh.Object, Capture);
}

void ATeleopObserver::UpdateText()
{
	// Top-left / bottom-left of the view, 30 cm in front of the camera.
	const float D = 30.f;
	const float Aspect = RenderTarget ? float(RenderTarget->SizeX) / FMath::Max<int32>(RenderTarget->SizeY, 1) : 1.f;
	const float HalfW = D * FMath::Tan(FMath::DegreesToRadians(Capture->FOVAngle * 0.5f));
	const float HalfH = HalfW / FMath::Max(Aspect, 0.1f);
	const float Size = 2.f * HalfH * 0.022f;
	const float Ev = TeleopCVars::ObserverExposure.GetValueOnGameThread();

	const FViewPreset& V = ActivePreset();
	const FString Hud = Pawn.IsValid() ? Pawn->GetHudText() : FString();
	InfoText->SetText(FText::FromString(FString::Printf(TEXT("Observer view [V]: %s\n%s"), V.Name, *Hud)));
	InfoText->SetWorldSize(Size);
	InfoText->SetTextRenderColor(TextColor(FLinearColor(0.95f, 0.95f, 0.95f), Ev));
	InfoText->SetRelativeLocation(FVector(D, -HalfW * 0.96f, HalfH * 0.96f));
	float LegendWidth = 0.f;
	for (int32 i = 0; i < LegendText.Num(); ++i)
	{
		LegendText[i]->SetWorldSize(Size);
		LegendText[i]->SetTextRenderColor(TextColor(Legend[i].Color, Ev));
		LegendText[i]->SetRelativeLocation(FVector(D, -HalfW * 0.96f, -HalfH * 0.96f + i * Size * 1.25f));
		LegendWidth = FMath::Max(LegendWidth, float(LegendText[i]->GetTextLocalSize().Y));
	}

	// Backing panels just behind the text. Plane: 100 cm, normal +Z; pitch 90 turns the normal toward the camera
	// and maps its local X to up, Y to right.
	const float Pad = Size * 0.4f;
	auto PlacePanel = [&](UStaticMeshComponent* Panel, float Left, float Top, float Width, float Height)
	{
		Panel->SetRelativeTransform(FTransform(FRotator(90.f, 0.f, 0.f),
			FVector(D + 0.2f, Left - Pad + (Width + 2.f * Pad) * 0.5f, Top + Pad - (Height + 2.f * Pad) * 0.5f),
			FVector((Height + 2.f * Pad) / 100.f, (Width + 2.f * Pad) / 100.f, 1.f)));
	};
	const FVector InfoSize = InfoText->GetTextLocalSize();
	PlacePanel(InfoPanel, -HalfW * 0.96f, HalfH * 0.96f, float(InfoSize.Y), float(InfoSize.Z));
	const float LegendHeight = Size * 1.25f * (LegendText.Num() - 1) + Size;
	PlacePanel(LegendPanel, -HalfW * 0.96f, -HalfH * 0.96f + LegendHeight, LegendWidth, LegendHeight);
}

void ATeleopObserver::Init(ATeleopGameMode* InGameMode, ATeleopOperatorPawn* InPawn, ARobotHead* InRobotHead, ATeleopPresenter* InPresenter)
{
	GameMode = InGameMode;
	Pawn = InPawn;
	RobotHead = InRobotHead;
	Presenter = InPresenter;
	if (!InRobotHead)
	{
		return;
	}
	AddTickPrerequisiteActor(InRobotHead);
	if (InPresenter)
	{
		AddTickPrerequisiteActor(InPresenter);
		Capture->HiddenActors.AddUnique(InPresenter); // its M3 mesh rides on the pawn
	}
	if (InPawn)
	{
		Capture->HiddenActors.AddUnique(InPawn); // HUD; in GT the pawn sits at the station
	}
	InRobotHead->HideFromRobotCameras(this);
	Capture->FOVAngle = FovDeg;

	static const FSoftObjectPath LitPath(TEXT("/Game/Teleop/Materials/M_TeleopLitColor.M_TeleopLitColor"));
	static const FSoftObjectPath GhostPath(TEXT("/Game/Teleop/Materials/M_TeleopGhost.M_TeleopGhost"));
	if (UMaterialInterface* Lit = Cast<UMaterialInterface>(LitPath.TryLoad()))
	{
		RobotMID = UMaterialInstanceDynamic::Create(Lit, this);
		RobotMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.6f, 0.62f, 0.66f));
		LensMID = UMaterialInstanceDynamic::Create(Lit, this);
		LensMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.01f, 0.01f, 0.012f));
		for (UStaticMeshComponent* C : { Pedestal.Get(), Torso.Get(), Neck.Get(), Head.Get() })
		{
			C->SetMaterial(0, RobotMID);
		}
		LensLeft->SetMaterial(0, LensMID);
		LensRight->SetMaterial(0, LensMID);
	}
	if (UMaterialInterface* Ghost = Cast<UMaterialInterface>(GhostPath.TryLoad()))
	{
		GhostMID = UMaterialInstanceDynamic::Create(Ghost, this);
		GhostMID->SetVectorParameterValue(TEXT("Color"), OperatorColor);
		GhostMID->SetScalarParameterValue(TEXT("Opacity"), 0.35f);
		GhostHmd->SetMaterial(0, GhostMID);
		PanelMID = UMaterialInstanceDynamic::Create(Ghost, this);
		PanelMID->SetVectorParameterValue(TEXT("Color"), FLinearColor::Black);
		PanelMID->SetScalarParameterValue(TEXT("Opacity"), 1.f); // x (0.6 + Fresnel ~ 0) facing the camera
		InfoPanel->SetMaterial(0, PanelMID);
		LegendPanel->SetMaterial(0, PanelMID);
	}
	else
	{
		UE_LOG(LogTeleop, Warning, TEXT("Observer: %s not found (run teleop_materials.build_all())"), *GhostPath.ToString());
	}
	static const FSoftObjectPath TextPath(TEXT("/Game/Teleop/Materials/M_TeleopTextUnlit.M_TeleopTextUnlit"));
	if (UMaterialInterface* TextMat = Cast<UMaterialInterface>(TextPath.TryLoad()))
	{
		InfoText->SetTextMaterial(TextMat);
		for (UTextRenderComponent* T : LegendText)
		{
			T->SetTextMaterial(TextMat);
		}
	}
	ARemoteHand::ConfigureTemplateHand(GhostHandLeft, true, GhostMID);
	ARemoteHand::ConfigureTemplateHand(GhostHandRight, false, GhostMID);

	// The robot model rides on the robot's joints.
	const FAttachmentTransformRules Keep = FAttachmentTransformRules::KeepRelativeTransform;
	Pedestal->AttachToComponent(InRobotHead->GetBase(), Keep);
	Torso->AttachToComponent(InRobotHead->GetWaist(), Keep);
	Neck->AttachToComponent(InRobotHead->GetWaist(), Keep);
	Head->AttachToComponent(InRobotHead->GetNeckPivot(), Keep);
	LensLeft->AttachToComponent(InRobotHead->GetCamera(0), Keep);
	LensRight->AttachToComponent(InRobotHead->GetCamera(1), Keep);
	for (UStaticMeshComponent* Lens : { LensLeft.Get(), LensRight.Get() })
	{
		// Cylinder axis (Z) along the view axis (X): radius 1.25 cm, 2 cm long, just in front of the camera.
		Lens->SetRelativeTransform(FTransform(FRotator(90.f, 0.f, 0.f), FVector(1.f, 0.f, 0.f), FVector(0.025f, 0.025f, 0.02f)));
	}

	RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("ObserverRT"));
	RenderTarget->RenderTargetFormat = RTF_RGBA8;
	RenderTarget->ClearColor = FLinearColor::Black;
	RenderTarget->InitAutoFormat(DefaultResolution.X, DefaultResolution.Y);
	Capture->TextureTarget = RenderTarget;
}

void ATeleopObserver::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AppliedMode > 0 && UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled())
	{
		UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenTexture(nullptr);
		UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenMode(ESpectatorScreenMode::SingleEyeCroppedToFill);
	}
	Super::EndPlay(EndPlayReason);
}

void ATeleopObserver::CycleView()
{
	const int32 Next = (FMath::Clamp(TeleopCVars::ObserverView.GetValueOnGameThread(), 0, NumViewPresets - 1) + 1) % NumViewPresets;
	TeleopCVars::ObserverView->Set(Next, ECVF_SetByConsole);
	UE_LOG(LogTeleop, Log, TEXT("[control] teleop.Observer.View = %d (%s)"), Next, ViewPresets[Next].Name);
}

void ATeleopObserver::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!GameMode.IsValid() || !RobotHead.IsValid() || !RenderTarget)
	{
		return;
	}
	const int32 Mode = FMath::Clamp(TeleopCVars::Observer.GetValueOnGameThread(), 0, 2);
	const bool bHmd = UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled();
	if (bHmd)
	{
		UpdateSpectatorScreen(Mode);
	}
	// Without an HMD nothing shows the view (desktop PIE shows the operator view); tests call DebugCaptureNow.
	if (Mode == 0 || !bHmd)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	const double Period = 1.0 / FMath::Clamp(TeleopCVars::ObserverHz.GetValueOnGameThread(), 1.f, 120.f);
	if (Now - LastCaptureTime >= Period - 0.002)
	{
		LastCaptureTime = Now;
		DebugCaptureNow();
	}
}

void ATeleopObserver::DebugCaptureNow()
{
	if (!RobotHead.IsValid() || !RenderTarget)
	{
		return;
	}
	UpdateRobotModel();
	UpdateGhosts();
	DrawOverlays();
	UpdateCamera();
	UpdateText();
	Capture->PostProcessSettings.AutoExposureBias = TeleopCVars::ObserverExposure.GetValueOnGameThread();
	Capture->CaptureScene(); // sends this frame's pending render-state updates first
}

FIntPoint ATeleopObserver::DesiredResolution(int32 Mode) const
{
	FIntPoint Window = FIntPoint::ZeroValue;
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		Window = GEngine->GameViewport->Viewport->GetSizeXY();
	}
	if (Window.X <= 0 || Window.Y <= 0)
	{
		return DefaultResolution;
	}
	const float Share = Mode == 1 ? FMath::Clamp(TeleopCVars::ObserverSplit.GetValueOnGameThread(), 0.2f, 0.8f) : 1.f;
	FIntPoint Res(FMath::RoundToInt(Window.X * Share), Window.Y);
	if (Res.X > MaxWidth)
	{
		Res = FIntPoint(MaxWidth, FMath::RoundToInt(float(Res.Y) * MaxWidth / Res.X));
	}
	return FIntPoint(FMath::Max(Res.X, 64), FMath::Max(Res.Y, 64));
}

void ATeleopObserver::UpdateSpectatorScreen(int32 Mode)
{
	if (Mode > 0)
	{
		// Same aspect as the window region it fills (the texture is stretched to it; the eye is cropped to fit).
		const FIntPoint Res = DesiredResolution(Mode);
		if (RenderTarget->SizeX != Res.X || RenderTarget->SizeY != Res.Y)
		{
			RenderTarget->ResizeTarget(Res.X, Res.Y);
		}
	}
	const float Split = FMath::Clamp(TeleopCVars::ObserverSplit.GetValueOnGameThread(), 0.2f, 0.8f);
	if (Mode == AppliedMode && (Mode != 1 || Split == AppliedSplit))
	{
		return;
	}
	if (Mode == 0)
	{
		if (AppliedMode > 0)
		{
			UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenTexture(nullptr);
			UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenMode(ESpectatorScreenMode::SingleEyeCroppedToFill);
		}
	}
	else
	{
		UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenTexture(RenderTarget);
		if (Mode == 1)
		{
			UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenModeTexturePlusEyeLayout(
				FVector2D(Split, 0.f), FVector2D(1.f, 1.f),  // eye: right part
				FVector2D(0.f, 0.f), FVector2D(Split, 1.f),  // observer: left part
				/*bDrawEyeFirst*/ false, /*bClearBlack*/ true, /*bUseAlpha*/ false);
			UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenMode(ESpectatorScreenMode::TexturePlusEye);
		}
		else
		{
			UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenMode(ESpectatorScreenMode::Texture);
		}
	}
	UE_LOG(LogTeleop, Log, TEXT("Observer: spectator screen %s"),
		Mode == 0 ? TEXT("released") : Mode == 1 ? *FString::Printf(TEXT("split (observer %.0f%% left, eye right)"), Split * 100.f) : TEXT("observer only"));
	AppliedMode = Mode;
	AppliedSplit = Split;
}

void ATeleopObserver::UpdateRobotModel()
{
	ARobotHead* RH = RobotHead.Get();
	const FVector Mount = TeleopCVars::CamMountOffset.GetValueOnGameThread() != 0 ? RH->MountOffset : FVector::ZeroVector;
	const float L = RH->WaistToNeckCm;

	// Pedestal: floor (station base) up to the waist pivot.
	const FVector WaistPivot = RH->GetWaist()->GetRelativeLocation();
	const float PedestalHeight = float(FMath::Max(WaistPivot.Z, 1.0));
	Pedestal->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(WaistPivot.X, WaistPivot.Y, PedestalHeight * 0.5f),
		FVector(0.16f, 0.16f, PedestalHeight / 100.f)));

	// Torso from the waist up to just below the neck pivot; neck in between (waist frame).
	const float TorsoHeight = FMath::Max(L - 9.f, 5.f);
	Torso->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, 1.f + TorsoHeight * 0.5f), FVector(0.18f, 0.30f, TorsoHeight / 100.f)));
	Neck->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, L - 4.f), FVector(0.07f, 0.07f, 0.08f)));

	// Head around the neck pivot; its front face just in front of the cameras (neck frame).
	Head->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(Mount.X - 7.f, 0.f, Mount.Z), FVector(0.16f, 0.20f, 0.14f)));
}

void ATeleopObserver::UpdateGhosts()
{
	ATeleopGameMode* GM = GameMode.Get();
	ATeleopOperatorPawn* P = Pawn.Get();
	if (!GM || !P)
	{
		return;
	}
	const FTransform LocalToRemote = GM->GetLocalToRemote();

	FTransform HeadLocal;
	bHasGhostEye = P->GetLatestLocalPose(ETeleopDevice::Head, HeadLocal);
	if (bHasGhostEye)
	{
		GhostEye = HeadLocal * LocalToRemote;
		GhostEye.SetScale3D(FVector::OneVector);
		// HMD box slightly in front of the eyes.
		GhostHmd->SetWorldTransform(FTransform(GhostEye.GetRotation(), GhostEye.TransformPosition(FVector(5.f, 0.f, 0.f)), FVector(0.09f, 0.18f, 0.09f)));
	}
	GhostHmd->SetVisibility(bHasGhostEye);

	for (const bool bLeft : { true, false })
	{
		USceneComponent* Grip = bLeft ? GhostGripLeft : GhostGripRight;
		USkeletalMeshComponent* Hand = bLeft ? GhostHandLeft : GhostHandRight;
		FTransform Local;
		const bool bHas = P->GetLatestLocalPose(bLeft ? ETeleopDevice::LeftHand : ETeleopDevice::RightHand, Local);
		if (bHas)
		{
			FTransform World = Local * LocalToRemote;
			World.SetScale3D(FVector::OneVector);
			Grip->SetWorldTransform(World);
			FTeleopFingerState Fingers;
			P->GetFingerHistory(bLeft).Sample(TNumericLimits<double>::Max(), Fingers);
			ARemoteHand::ApplyFingers(Hand->GetAnimInstance(), Fingers, bLeft);
		}
		Hand->SetVisibility(bHas);
	}
}

void ATeleopObserver::DrawFrustum(const FTransform& Camera, float HorizontalFovDeg, float Aspect, const FLinearColor& Color)
{
	const double TanX = FMath::Tan(FMath::DegreesToRadians(HorizontalFovDeg * 0.5));
	const double TanY = TanX / FMath::Max(Aspect, 0.1f);
	const double D = FrustumLengthCm;
	const FVector O = Camera.GetLocation();
	const FVector Corner[4] = {
		Camera.TransformPosition(FVector(D, -TanX * D, TanY * D)),  // top left
		Camera.TransformPosition(FVector(D, TanX * D, TanY * D)),   // top right
		Camera.TransformPosition(FVector(D, TanX * D, -TanY * D)),  // bottom right
		Camera.TransformPosition(FVector(D, -TanX * D, -TanY * D)), // bottom left
	};
	for (int32 i = 0; i < 4; ++i)
	{
		Lines->DrawLine(O, Corner[i], Color, SDPG_World, LineThicknessCm);
		Lines->DrawLine(Corner[i], Corner[(i + 1) % 4], Color, SDPG_World, LineThicknessCm);
	}
	// "Up" tick on the top edge, so roll is visible.
	const FVector TopMid = (Corner[0] + Corner[1]) * 0.5;
	Lines->DrawLine(TopMid, TopMid + Camera.GetUnitAxis(EAxis::Z) * (D * 0.15), Color, SDPG_World, LineThicknessCm);
}

void ATeleopObserver::DrawOverlays()
{
	ARobotHead* RH = RobotHead.Get();
	ATeleopGameMode* GM = GameMode.Get();
	Lines->Flush();

	const float Aspect = float(RH->CaptureWidth) / FMath::Max(RH->CaptureHeight, 1);
	FTransform Rig = RH->GetCameraRig()->GetComponentTransform();
	Rig.SetScale3D(FVector::OneVector);
	DrawFrustum(Rig, RH->HorizontalFovDeg, Aspect, RobotNowColor);

	FTransform ShownLeft, ShownRight;
	if (Presenter.IsValid() && Presenter->GetShownFramePose(ShownLeft, ShownRight))
	{
		FTransform Shown(ShownLeft.GetRotation(), (ShownLeft.GetLocation() + ShownRight.GetLocation()) * 0.5);
		DrawFrustum(Shown, RH->HorizontalFovDeg, Aspect, ShownColor);
	}

	if (bHasGhostEye)
	{
		DrawFrustum(GhostEye, OperatorFovDeg, 1.f, OperatorColor);
		Lines->DrawLine(GhostEye.GetLocation(), Rig.GetLocation(), GapColor, SDPG_World, LineThicknessCm * 0.6f);
	}
	for (const bool bLeft : { true, false })
	{
		const ARemoteHand* Hand = GM ? GM->GetRemoteHand(bLeft) : nullptr;
		const USceneComponent* Ghost = bLeft ? GhostGripLeft : GhostGripRight;
		if (Hand && Hand->GetGripPoint() && (bLeft ? GhostHandLeft : GhostHandRight)->IsVisible())
		{
			Lines->DrawLine(Ghost->GetComponentLocation(), Hand->GetGripPoint()->GetComponentLocation(), GapColor, SDPG_World, LineThicknessCm * 0.6f);
		}
	}
}

void ATeleopObserver::UpdateCamera()
{
	const ATeleopGameMode* GM = GameMode.Get();
	const FViewPreset& V = ActivePreset();
	const FTransform Station = GM->GetStationTransform();
	// Look at the robot's workspace: between its head and the desk.
	const FVector Target = Station.TransformPosition(FVector(V.TargetX, 0.f, GM->GetCalibratedEyeHeight() + V.TargetDz));
	const FQuat Dir = Station.GetRotation() * FRotator(V.Pitch, V.Yaw, 0.f).Quaternion();
	Capture->SetWorldLocationAndRotation(Target - Dir.GetForwardVector() * V.Distance, Dir);
}
