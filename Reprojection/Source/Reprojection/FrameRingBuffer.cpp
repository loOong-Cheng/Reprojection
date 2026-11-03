#include "FrameRingBuffer.h"

#include "Engine/TextureRenderTarget2D.h"

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
			UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(this, Name);
			RT->RenderTargetFormat = RTF_RGBA16f;
			RT->ClearColor = FLinearColor::Black;
			RT->bAutoGenerateMips = false;
			RT->AddressX = TA_Clamp;
			RT->AddressY = TA_Clamp;
			RT->InitAutoFormat(InWidth, InHeight);
			RT->UpdateResourceImmediate(true);
			Textures.Add(RT);
		}
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

int32 UFrameRingBuffer::FindNewestArrived(double Now) const
{
	int32 Best = INDEX_NONE;
	for (int32 Slot = 0; Slot < Metas.Num(); ++Slot)
	{
		const FTeleopFrameMeta& M = Metas[Slot];
		if (!M.IsValid() || M.bDropped || M.ArrivalTime > Now)
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
