#include "pch.h"
#include "Texture.h"
#include "Core/Object/Class.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderManager.h"
#include "Renderer/RHIGlobals.h"

namespace Lumina
{
    void CTexture::Serialize(FArchive& Ar)
    {
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
        if (TextureResource && TextureResource->RHIImage && TextureResource->RHIImage->GetTextureCacheIndex() != -1)
        {
            GRenderManager->GetTextureManager().RemoveTexture(TextureResource->RHIImage);
        }

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

        GRenderManager->GetTextureManager().AddTexture(TextureResource->RHIImage);
    }

    void CTexture::OnDestroy()
    {
        if (TextureResource && TextureResource->RHIImage && TextureResource->RHIImage->GetTextureCacheIndex() != -1)
        {
            GRenderManager->GetTextureManager().RemoveTexture(TextureResource->RHIImage);
        }
    }
}
