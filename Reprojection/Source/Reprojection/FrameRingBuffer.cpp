#include "FrameRingBuffer.h"

#include "Engine/TextureRenderTarget2D.h"

namespace
{
	UTextureRenderTarget2D* MakeTarget(UObject* Outer, const FName Name, ETextureRenderTargetFormat Format, int32 W, int32 H, TextureFilter Filter)
	{
		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(Outer, Name);
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
}

void UFrameRingBuffer::Initialize(int32 InNumSlots, int32 InWidth, int32 InHeight)
{
	Textures.Reset();
	Metas.Reset();
	Metas.SetNum(InNumSlots);
	for (int32 Slot = 0; Slot < InNumSlots; ++Slot)
	{
		for (int32 Eye = 0; Eye < 2; ++Eye)
		{
			const FName Name = *FString::Printf(TEXT("RT_Frame%02d_%s"), Slot, Eye == 0 ? TEXT("L") : TEXT("R"));
			Textures.Add(MakeTarget(this, Name, RTF_RGBA16f, InWidth, InHeight, TF_Bilinear));
		}
	}
}

void UFrameRingBuffer::EnsureDepthTextures(int32 InDepthWidth, int32 InDepthHeight)
{
	if (InDepthWidth == DepthWidth && InDepthHeight == DepthHeight && DepthTextures.Num() == Metas.Num() * 2)
	{
		return;
	}
	DepthWidth = InDepthWidth;
	DepthHeight = InDepthHeight;
	DepthTextures.Reset();
	for (int32 Slot = 0; Slot < Metas.Num(); ++Slot)
	{
		for (int32 Eye = 0; Eye < 2; ++Eye)
		{
			// Nearest filtering: never blend foreground and background depth.
			DepthTextures.Add(MakeTarget(this, NAME_None, RTF_R32f, DepthWidth, DepthHeight, TF_Nearest));
		}
		Metas[Slot].bDepthValid = false;
	}
}

int32 UFrameRingBuffer::AcquireWriteSlot(int32 ProtectedSlot) const
{
	int32 Best = INDEX_NONE;
	for (int32 Slot = 0; Slot < Metas.Num(); ++Slot)
	{
		if (Slot == ProtectedSlot)
		{
			continue;
		}
		if (!Metas[Slot].IsValid())
		{
			return Slot; // never used
		}
		if (Best == INDEX_NONE || Metas[Slot].FrameId < Metas[Best].FrameId)
		{
			Best = Slot;
		}
	}
	return Best;
}

void UFrameRingBuffer::Commit(int32 Slot, const FTeleopFrameMeta& Meta)
{
	if (Metas.IsValidIndex(Slot))
	{
		Metas[Slot] = Meta;
	}
}

int32 UFrameRingBuffer::FindNewestArrived(double Now, double JitterBufferDelay) const
{
	int32 Best = INDEX_NONE;
	for (int32 Slot = 0; Slot < Metas.Num(); ++Slot)
	{
		const FTeleopFrameMeta& M = Metas[Slot];
		if (!M.IsValid() || M.bDropped)
		{
			continue;
		}
		const double Eligible = FMath::Max(M.ArrivalTime, M.CaptureTime + JitterBufferDelay);
		if (Eligible > Now)
		{
			continue;
		}
		if (Best == INDEX_NONE || M.CaptureTime > Metas[Best].CaptureTime)
		{
			Best = Slot;
		}
	}
	return Best;
}

UTextureRenderTarget2D* UFrameRingBuffer::GetTexture(int32 Slot, int32 Eye) const
{
	const int32 Index = Slot * 2 + Eye;
	return Textures.IsValidIndex(Index) ? Textures[Index].Get() : nullptr;
}

UTextureRenderTarget2D* UFrameRingBuffer::GetDepthTexture(int32 Slot, int32 Eye) const
{
	const int32 Index = Slot * 2 + Eye;
	return DepthTextures.IsValidIndex(Index) ? DepthTextures[Index].Get() : nullptr;
}
