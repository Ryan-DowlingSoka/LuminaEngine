#include "pch.h"
#include "Texture.h"
#include "Core/Object/Class.h"
#include "Memory/MemoryTracking.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderManager.h"
#include "Renderer/RHIGlobals.h"

namespace Lumina
{
    void CTexture::Serialize(FArchive& Ar)
    {
        LUMINA_MEMORY_SCOPE("Textures");

        Super::Serialize(Ar);

        if (!TextureResource)
        {
            TextureResource = MakeUnique<FTextureResource>();
        }

        Ar << *TextureResource.get();
    }

    void CTexture::PreLoad()
    {
        if (TextureResource == nullptr)
        {
            TextureResource = MakeUnique<FTextureResource>();
        }
    }

    void CTexture::PostLoad()
    {
        LUMINA_MEMORY_SCOPE("Textures");

        TextureResource->RHIImage = GRenderContext->CreateImage(TextureResource->ImageDescription);

        FRHICommandListRef TransferCommandList = GRenderContext->CreateCommandList(FCommandListInfo::Compute());
        TransferCommandList->Open();

        for (uint8 i = 0; i < TextureResource->Mips.size(); ++i)
        {
            FTextureResource::FMip& Mip = TextureResource->Mips[i];
            const uint32 RowPitch = Mip.RowPitch;
            TransferCommandList->WriteImage(TextureResource->RHIImage, 0, i, Mip.Pixels.data(), RowPitch, 1);
        }

        TransferCommandList->Close();
        GRenderContext->ExecuteCommandList(TransferCommandList, ECommandQueue::Compute);

#if !USING(WITH_EDITOR)
        // WriteImage has already copied each mip into the upload staging buffer, so the CPU pixel
        // copy is dead after this point in a cooked build: nothing re-cooks, re-saves or regenerates
        // thumbnails from it, and a device loss panics rather than recreating. (The editor keeps it --
        // import re-cook, thumbnails and asset save all read Mips.Pixels.)
        for (FTextureResource::FMip& Mip : TextureResource->Mips)
        {
            Mip.Pixels.clear();
            Mip.Pixels.shrink_to_fit();
        }
#endif
    }

    void CTexture::OnDestroy()
    {
    }
}
