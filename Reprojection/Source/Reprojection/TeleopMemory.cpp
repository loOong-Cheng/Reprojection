#include "TeleopMemory.h"

#include "FrameRingBuffer.h"
#include "Reprojection.h"
#include "TeleopCVars.h"

#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "TextureResource.h"

namespace
{
	FLinearColor Vec(const FVector& V) { return FLinearColor(float(V.X), float(V.Y), float(V.Z), 0.f); }

	constexpr double MinInsertIntervalSec = 0.1;

	UTextureRenderTarget2D* MakeTarget(UObject* Outer, ETextureRenderTargetFormat Format, int32 W, int32 H, TextureFilter Filter)
	{
		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(Outer);
		RT->RenderTargetFormat = Format;
		RT->ClearColor = FLinearColor::Black;
		RT->bAutoGenerateMips = false;
		RT->Filter = Filter;
		RT->AddressX = TA_Clamp;
		RT->AddressY = TA_Clamp;
		RT->InitAutoFormat(W, H);
		RT->UpdateResourceImmediate(true);
		return RT;
	}

	UMaterialInstanceDynamic* MakeMID(const TCHAR* Path, UObject* Outer)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, Path);
		if (!Base)
		{
			UE_LOG(LogTeleop, Error, TEXT("Memory: material not found: %s"), Path);
			return nullptr;
		}
		return UMaterialInstanceDynamic::Create(Base, Outer);
	}

	void SetCamera(UMaterialInstanceDynamic* MID, const TCHAR* Prefix, const FTransform& T)
	{
		MID->SetVectorParameterValue(*FString::Printf(TEXT("%sPos"), Prefix), Vec(T.GetLocation()));
		MID->SetVectorParameterValue(*FString::Printf(TEXT("%sFwd"), Prefix), Vec(T.GetUnitAxis(EAxis::X)));
		MID->SetVectorParameterValue(*FString::Printf(TEXT("%sRight"), Prefix), Vec(T.GetUnitAxis(EAxis::Y)));
		MID->SetVectorParameterValue(*FString::Printf(TEXT("%sUp"), Prefix), Vec(T.GetUnitAxis(EAxis::Z)));
	}
}

void UTeleopMemory::Initialize(UObject* InWorldContext, USceneComponent* InAttachTo, int32 InStencilValue)
{
	WorldContext = InWorldContext;
	AttachTo = InAttachTo;
	StencilValue = InStencilValue;
	ColorMID = MakeMID(TEXT("/Game/Teleop/Materials/M_TeleopKeyframeColor.M_TeleopKeyframeColor"), this);
	DepthMID = MakeMID(TEXT("/Game/Teleop/Materials/M_TeleopKeyframeDepth.M_TeleopKeyframeDepth"), this);
	MeshMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Teleop/Materials/M_TeleopMemoryMesh.M_TeleopMemoryMesh"));
	if (!MeshMaterial)
	{
		UE_LOG(LogTeleop, Error, TEXT("Memory: material M_TeleopMemoryMesh not found (run teleop_materials.build_all())"));
	}
}

int32 UTeleopMemory::NumKeyframes() const
{
	int32 N = 0;
	for (const FKeyframe& K : Keys)
	{
		N += K.bValid ? 1 : 0;
	}
	return N;
}

void UTeleopMemory::Clear()
{
	for (FKeyframe& K : Keys)
	{
		K = FKeyframe();
	}
	LastInsertTime = -1.0;
	HideAll();
	UE_LOG(LogTeleop, Log, TEXT("Memory: cleared"));
}

void UTeleopMemory::HideAll()
{
	for (UProceduralMeshComponent* M : Meshes)
	{
		if (M)
		{
			M->SetVisibility(false);
		}
	}
	bShown = false;
}

void UTeleopMemory::EnsureTargets(int32 Width, int32 Height, int32 Count)
{
	if (TargetSize == FIntPoint(Width, Height) && Colors.Num() == Count)
	{
		return;
	}
	TargetSize = FIntPoint(Width, Height);
	Colors.Reset();
	Depths.Reset();
	for (int32 i = 0; i < Count; ++i)
	{
		Colors.Add(MakeTarget(this, RTF_RGBA16f, Width, Height, TF_Bilinear));
		Depths.Add(MakeTarget(this, RTF_R32f, Width, Height, TF_Nearest)); // never blend fore- and background depth
	}
	Keys.Init(FKeyframe(), Count);
	LastInsertTime = -1.0;
	MeshGrid = FIntPoint::ZeroValue; // mesh count may change too
	UE_LOG(LogTeleop, Log, TEXT("Memory: %d keyframes of %dx%d (colour RGBA16f + depth R32f, %.1f MB)"), Count, Width, Height,
		Count * Width * Height * 12.0 / (1024.0 * 1024.0));
}

void UTeleopMemory::EnsureMeshes(int32 GridX, int32 GridY)
{
	const FIntPoint Grid(GridX, GridY);
	USceneComponent* Parent = AttachTo.Get();
	if ((Grid == MeshGrid && Meshes.Num() == Keys.Num()) || !Parent || !MeshMaterial)
	{
		return;
	}
	MeshGrid = Grid;

	// Same layout as the M3 grid: vertices at the cell centres in uv, rest positions on a plane in front (WPO moves them).
	TArray<FVector> Vertices;
	TArray<FVector2D> UV0;
	TArray<FVector> Normals;
	TArray<int32> Triangles;
	Vertices.Reserve(Grid.X * Grid.Y);
	UV0.Reserve(Grid.X * Grid.Y);
	for (int32 j = 0; j < Grid.Y; ++j)
	{
		for (int32 i = 0; i < Grid.X; ++i)
		{
			const FVector2D UV((i + 0.5) / Grid.X, (j + 0.5) / Grid.Y);
			UV0.Add(UV);
			Vertices.Add(FVector(100.0, (UV.X - 0.5) * 200.0, (0.5 - UV.Y) * 150.0 + 120.0));
			Normals.Add(FVector(-1, 0, 0));
		}
	}
	Triangles.Reserve((Grid.X - 1) * (Grid.Y - 1) * 6);
	for (int32 j = 0; j + 1 < Grid.Y; ++j)
	{
		for (int32 i = 0; i + 1 < Grid.X; ++i)
		{
			const int32 A = j * Grid.X + i;
			const int32 B = A + 1;
			const int32 C = A + Grid.X;
			const int32 D = C + 1;
			Triangles.Append({ A, C, B, B, C, D });
		}
	}

	for (UProceduralMeshComponent* M : Meshes)
	{
		if (M)
		{
			M->DestroyComponent();
		}
	}
	Meshes.Reset();
	MeshMIDs.Reset();
	AActor* OwnerActor = GetTypedOuter<AActor>(); // the presenter, like the M3 mesh
	UObject* Outer = OwnerActor ? static_cast<UObject*>(OwnerActor) : static_cast<UObject*>(Parent->GetOwner());
	for (int32 k = 0; k < Keys.Num(); ++k)
	{
		UProceduralMeshComponent* M = NewObject<UProceduralMeshComponent>(Outer,
			MakeUniqueObjectName(Outer, UProceduralMeshComponent::StaticClass(), TEXT("MemoryMesh")));
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetCastShadow(false);
		M->SetRenderCustomDepth(true);
		M->SetCustomDepthStencilValue(StencilValue);
		M->SetHiddenInSceneCapture(true);
		M->SetBoundsScale(200.f); // WPO moves vertices far from the rest grid (HANDOFF §7.6)
		M->SetVisibility(false);
		M->SetupAttachment(Parent);
		M->RegisterComponent();
		M->AttachToComponent(Parent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		M->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UV0, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(MeshMaterial, this);
		M->SetMaterial(0, MID);
		Meshes.Add(M);
		MeshMIDs.Add(MID);
	}
	bShown = false;
	UE_LOG(LogTeleop, Log, TEXT("Memory: %d keyframe meshes of %dx%d vertices"), Keys.Num(), Grid.X, Grid.Y);
}

bool UTeleopMemory::IsNovel(const FTransform& Pose) const
{
	const double MinMove = FMath::Max(TeleopCVars::MemoryMinMoveCm.GetValueOnGameThread(), 0.f);
	const double MinTurn = FMath::Max(TeleopCVars::MemoryMinTurnDeg.GetValueOnGameThread(), 0.f);
	for (const FKeyframe& K : Keys)
	{
		if (!K.bValid)
		{
			continue;
		}
		const double Move = FVector::Dist(Pose.GetLocation(), K.Pose.GetLocation());
		const double Turn = FMath::RadiansToDegrees(Pose.GetRotation().AngularDistance(K.Pose.GetRotation()));
		if (Move < MinMove && Turn < MinTurn)
		{
			return false; // a stored keyframe already saw the scene from about here
		}
	}
	return true;
}

void UTeleopMemory::Insert(UFrameRingBuffer* Buffer, int32 Slot, double Now, const FDrawParams& Params)
{
	UObject* Context = WorldContext.Get();
	if (!Context || !ColorMID || !DepthMID)
	{
		return;
	}
	// Free slot, else the oldest keyframe.
	int32 Index = 0;
	for (int32 i = 0; i < Keys.Num(); ++i)
	{
		if (!Keys[i].bValid)
		{
			Index = i;
			break;
		}
		if (Keys[i].Time < Keys[Index].Time)
		{
			Index = i;
		}
	}

	const FTeleopFrameMeta& Meta = Buffer->GetMeta(Slot);
	ColorMID->SetTextureParameterValue(TEXT("Src"), Buffer->GetTexture(Slot, 0));
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(Context, Colors[Index], ColorMID);

	// Band radius in depth texels: the camera-pixel width over the depth subsampling K.
	const float BandPx = FMath::Max(TeleopCVars::MemoryBandPx.GetValueOnGameThread(), 0.f);
	const float K = float(Meta.Width) / float(FMath::Max(TargetSize.X, 1));
	DepthMID->SetTextureParameterValue(TEXT("Depth"), Buffer->GetDepthTexture(Slot, 0));
	DepthMID->SetVectorParameterValue(TEXT("Texel"), FLinearColor(1.f / TargetSize.X, 1.f / TargetSize.Y, 0.f, 0.f));
	DepthMID->SetScalarParameterValue(TEXT("BandTexels"), BandPx / FMath::Max(K, 1.f));
	DepthMID->SetScalarParameterValue(TEXT("DispScale"), Params.DispScale);
	DepthMID->SetScalarParameterValue(TEXT("EdgeDispPx"), Params.EdgeDispPx);
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(Context, Depths[Index], DepthMID);

	FKeyframe& Key = Keys[Index];
	Key.bValid = true;
	Key.Pose = Meta.EyeWorld[0];
	Key.Pose.SetScale3D(FVector::OneVector);
	const float TanX = FMath::Tan(FMath::DegreesToRadians(Meta.HorizontalFovDeg * 0.5f));
	Key.Tan = FVector2f(TanX, TanX * Meta.Height / float(FMath::Max(Meta.Width, 1)));
	Key.Time = Now;
	Key.FrameId = Meta.FrameId;
	Key.BandPx = BandPx;
	LastInsertTime = Now;
}

void UTeleopMemory::Update(double Now, UFrameRingBuffer* Buffer, int32 ShownSlot, bool bDraw, const FDrawParams& Params)
{
	if (!Buffer || ShownSlot == INDEX_NONE || Buffer->GetDepthWidth() <= 0)
	{
		HideAll();
		return;
	}
	const int32 Count = FMath::Clamp(TeleopCVars::MemoryKeyframes.GetValueOnGameThread(), 1, 32);
	EnsureTargets(Buffer->GetDepthWidth(), Buffer->GetDepthHeight(), Count);

	const FTeleopFrameMeta& Meta = Buffer->GetMeta(ShownSlot);
	if (Meta.bDepthValid && Meta.FrameId != LastConsideredFrameId && Now - LastInsertTime >= MinInsertIntervalSec)
	{
		LastConsideredFrameId = Meta.FrameId;
		FTransform Pose = Meta.EyeWorld[0];
		if (IsNovel(Pose))
		{
			Insert(Buffer, ShownSlot, Now, Params);
		}
	}

	const int32 Stride = FMath::Clamp(TeleopCVars::MemoryGridStride.GetValueOnGameThread(), 1, 8);
	EnsureMeshes(FMath::Max(TargetSize.X / Stride, 2), FMath::Max(TargetSize.Y / Stride, 2));
	if (!bDraw || !Meta.bDepthValid)
	{
		if (bShown)
		{
			HideAll();
		}
		return;
	}

	// Age rank (0 = newest) sets the push-back, so newer memory wins over older.
	TArray<int32> Order;
	for (int32 i = 0; i < Keys.Num(); ++i)
	{
		if (Keys[i].bValid)
		{
			Order.Add(i);
		}
	}
	Order.Sort([this](int32 A, int32 B) { return Keys[A].Time > Keys[B].Time; });

	const float TanX = FMath::Tan(FMath::DegreesToRadians(Meta.HorizontalFovDeg * 0.5f));
	const FLinearColor CurTan(TanX, TanX * Meta.Height / float(FMath::Max(Meta.Width, 1)), 0.f, 0.f);
	const float Push = FMath::Max(TeleopCVars::MemoryPush.GetValueOnGameThread(), 0.f);
	for (int32 i = 0; i < Meshes.Num() && i < Keys.Num(); ++i)
	{
		UProceduralMeshComponent* M = Meshes[i];
		UMaterialInstanceDynamic* MID = MeshMIDs[i];
		const int32 Rank = Order.IndexOfByKey(i);
		if (!M || !MID || Rank == INDEX_NONE)
		{
			if (M)
			{
				M->SetVisibility(false);
			}
			continue;
		}
		const FKeyframe& Key = Keys[i];
		MID->SetTextureParameterValue(TEXT("KeyColor"), Colors[i]);
		MID->SetTextureParameterValue(TEXT("KeyDepth"), Depths[i]);
		SetCamera(MID, TEXT("Key"), Key.Pose);
		MID->SetVectorParameterValue(TEXT("KeyTan"), FLinearColor(Key.Tan.X, Key.Tan.Y, 0.f, 0.f));
		MID->SetTextureParameterValue(TEXT("CurDepth"), Buffer->GetDepthTexture(ShownSlot, 0));
		SetCamera(MID, TEXT("Cur"), Meta.EyeWorld[0]);
		MID->SetVectorParameterValue(TEXT("CurTan"), CurTan);
		MID->SetVectorParameterValue(TEXT("GridStep"), FLinearColor(1.f / MeshGrid.X, 1.f / MeshGrid.Y, 0.f, 0.f));
		MID->SetScalarParameterValue(TEXT("EdgeDispPx"), Params.EdgeDispPx);
		MID->SetScalarParameterValue(TEXT("DispScale"), Params.DispScale);
		MID->SetScalarParameterValue(TEXT("Push"), Push * (1.f + Rank));
		MID->SetVectorParameterValue(TEXT("RoomSeat"), Vec(Params.RoomSeat));
		MID->SetVectorParameterValue(TEXT("MapX"), Vec(Params.Map.TransformVectorNoScale(FVector::XAxisVector)));
		MID->SetVectorParameterValue(TEXT("MapY"), Vec(Params.Map.TransformVectorNoScale(FVector::YAxisVector)));
		MID->SetVectorParameterValue(TEXT("MapZ"), Vec(Params.Map.TransformVectorNoScale(FVector::ZAxisVector)));
		MID->SetVectorParameterValue(TEXT("MapT"), Vec(Params.Map.GetLocation()));
		M->SetVisibility(true);
	}
	bShown = true;
}

FString UTeleopMemory::Describe(double Now)
{
	FString Out = FString::Printf(TEXT("Memory: %d / %d keyframes, %dx%d"), NumKeyframes(), Keys.Num(), TargetSize.X, TargetSize.Y);
	double Kept = 0.0;
	int32 N = 0;
	for (int32 i = 0; i < Keys.Num(); ++i)
	{
		const FKeyframe& K = Keys[i];
		if (!K.bValid || !Depths.IsValidIndex(i) || !Depths[i])
		{
			continue;
		}
		TArray<FLinearColor> Pixels;
		FTextureRenderTargetResource* Res = Depths[i]->GameThread_GetRenderTargetResource();
		int32 Valid = 0;
		if (Res && Res->ReadLinearColorPixels(Pixels))
		{
			for (const FLinearColor& P : Pixels)
			{
				Valid += P.R > 0.f ? 1 : 0;
			}
		}
		const double Share = Pixels.Num() > 0 ? 100.0 * Valid / Pixels.Num() : 0.0;
		Kept += Share;
		++N;
		Out += FString::Printf(TEXT("\n  [%d] age %.1f s, camera %s, band %.0f px: %.1f%% of texels kept"), i, Now - K.Time,
			*K.Pose.GetLocation().ToCompactString(), K.BandPx, Share);
	}
	if (N > 0)
	{
		Out += FString::Printf(TEXT("\n  mean %.1f%% of texels kept"), Kept / N);
	}
	return Out;
}
