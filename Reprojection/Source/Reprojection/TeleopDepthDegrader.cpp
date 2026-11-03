#include "TeleopDepthDegrader.h"

#include "Reprojection.h"
#include "TeleopCVars.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	// HANDOFF §5 presets. DepthGT keeps the exact depth (no quantization, no range limit).
	const FTeleopDepthPreset GDepthPresets[UTeleopDepthDegrader::NumPresets] = {
		//  Name               K  sigma  pHole edge  Hz     quant  zmin   zmax     fatten
		{ TEXT("DepthGT"),      1, 0.0f, 0.00f, 0,  0.f, false,  0.f, 1.0e7f, 0 },
		{ TEXT("ZEDlike"),      2, 0.2f, 0.02f, 1,  0.f, true,  30.f, 500.f,  0 },
		{ TEXT("MobileStereo"), 8, 0.4f, 0.08f, 2, 30.f, true,  30.f, 500.f,  1 },
		{ TEXT("Sparse"),      16, 0.5f, 0.15f, 3, 15.f, true,  30.f, 500.f,  1 },
	};

	constexpr float HoleCellPx = 6.f;     // size of the low-frequency hole pattern, depth pixels
	constexpr float EdgeHoleDispPx = 3.f; // disparity jump between adjacent depth samples that counts as a discontinuity

	UMaterialInstanceDynamic* MakeMID(const TCHAR* Path, UObject* Outer)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, Path);
		if (!Base)
		{
			UE_LOG(LogTeleop, Error, TEXT("Depth degrader: material not found: %s"), Path);
			return nullptr;
		}
		return UMaterialInstanceDynamic::Create(Base, Outer);
	}
}

const FTeleopDepthPreset& UTeleopDepthDegrader::GetPreset(int32 Index)
{
	return GDepthPresets[FMath::Clamp(Index, 0, NumPresets - 1)];
}

const FTeleopDepthPreset& UTeleopDepthDegrader::GetActivePreset()
{
	return GetPreset(TeleopCVars::DepthPreset.GetValueOnGameThread());
}

void UTeleopDepthDegrader::Initialize(UObject* InWorldContext)
{
	WorldContext = InWorldContext;
	for (int32 Eye = 0; Eye < 2; ++Eye)
	{
		MinMaxMID[Eye] = MakeMID(TEXT("/Game/Teleop/Materials/M_TeleopDepthMinMax.M_TeleopDepthMinMax"), this);
		DegradeMID[Eye] = MakeMID(TEXT("/Game/Teleop/Materials/M_TeleopDepthDegrade.M_TeleopDepthDegrade"), this);
	}
	CopyMID = MakeMID(TEXT("/Game/Teleop/Materials/M_TeleopCopyR.M_TeleopCopyR"), this);
}

UTextureRenderTarget2D* UTeleopDepthDegrader::GetScratch(int32 Eye, int32 Width, int32 Height)
{
	TObjectPtr<UTextureRenderTarget2D>& RT = Scratch[Eye];
	if (!RT || RT->SizeX != Width || RT->SizeY != Height)
	{
		RT = NewObject<UTextureRenderTarget2D>(this);
		RT->RenderTargetFormat = RTF_RG32f;
		RT->Filter = TF_Nearest;
		RT->AddressX = TA_Clamp;
		RT->AddressY = TA_Clamp;
		RT->bAutoGenerateMips = false;
		RT->InitAutoFormat(Width, Height);
		RT->UpdateResourceImmediate(true);
	}
	return RT;
}

void UTeleopDepthDegrader::Degrade(UTextureRenderTarget2D* Source, UTextureRenderTarget2D* Dest, int32 Eye, int64 FrameId,
	float HorizontalFovDeg, float BaselineCm)
{
	UObject* Context = WorldContext.Get();
	if (!Context || !Source || !Dest || !MinMaxMID[Eye] || !DegradeMID[Eye])
	{
		return;
	}
	const FTeleopDepthPreset& P = GetActivePreset();
	const int32 W = Dest->SizeX;
	const int32 H = Dest->SizeY;

	// Pass 1: block min/max of the true depth.
	UTextureRenderTarget2D* MinMax = GetScratch(Eye, W, H);
	UMaterialInstanceDynamic* MM = MinMaxMID[Eye];
	MM->SetTextureParameterValue(TEXT("Src"), Source);
	MM->SetVectorParameterValue(TEXT("SrcTexel"), FLinearColor(1.f / Source->SizeX, 1.f / Source->SizeY, 0.f, 0.f));
	MM->SetVectorParameterValue(TEXT("OutTexel"), FLinearColor(1.f / W, 1.f / H, 0.f, 0.f));
	MM->SetScalarParameterValue(TEXT("K"), float(P.K));
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(Context, MinMax, MM);

	// Pass 2: degradation. Disparity (and its noise / quantization) is in full-resolution pixels: stereo matching
	// runs on the camera images; K only sets how densely depth is sampled.
	const float FocalPx = (Source->SizeX * 0.5f) / FMath::Tan(FMath::DegreesToRadians(HorizontalFovDeg * 0.5f));
	UMaterialInstanceDynamic* D = DegradeMID[Eye];
	D->SetTextureParameterValue(TEXT("MinMax"), MinMax);
	D->SetVectorParameterValue(TEXT("Texel"), FLinearColor(1.f / W, 1.f / H, 0.f, 0.f));
	D->SetScalarParameterValue(TEXT("FocalPx"), FocalPx);
	D->SetScalarParameterValue(TEXT("BaselineCm"), FMath::Max(BaselineCm, 0.1f));
	D->SetScalarParameterValue(TEXT("SigmaDisp"), P.SigmaDispPx);
	D->SetScalarParameterValue(TEXT("Quantize"), P.bQuantize ? 1.f : 0.f);
	D->SetScalarParameterValue(TEXT("ZMin"), P.ZMinCm);
	D->SetScalarParameterValue(TEXT("ZMax"), P.ZMaxCm);
	D->SetScalarParameterValue(TEXT("PHole"), P.PHole);
	D->SetScalarParameterValue(TEXT("HoleCellPx"), HoleCellPx);
	D->SetScalarParameterValue(TEXT("EdgeHolePx"), float(P.EdgeHolePx));
	D->SetScalarParameterValue(TEXT("EdgeDispPx"), EdgeHoleDispPx);
	D->SetScalarParameterValue(TEXT("FattenPx"), float(P.FattenPx));
	// New noise every frame (temporal flicker), different per eye.
	D->SetScalarParameterValue(TEXT("Seed"), float((FrameId * 2 + Eye) % 9973) + 0.5f);
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(Context, Dest, D);
}

void UTeleopDepthDegrader::Copy(UTextureRenderTarget2D* Source, UTextureRenderTarget2D* Dest)
{
	UObject* Context = WorldContext.Get();
	if (!Context || !Source || !Dest || !CopyMID)
	{
		return;
	}
	CopyMID->SetTextureParameterValue(TEXT("Src"), Source);
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(Context, Dest, CopyMID);
}
