#include "pch.h"
#include "TextureRenderTarget.h"
#include "Core/Object/Class.h"
#include "Memory/MemoryTracking.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RHIGlobals.h"

namespace Lumina
{
    void CTextureRenderTarget::Serialize(FArchive& Ar)
    {
        // Skip CTexture's pixel-mip blob -- a render target has no cooked source data. Only
        // the reflected properties (Width/Height/Format/ClearColor) persist; CObject::Serialize
        // walks the full property chain. The GPU image is rebuilt in PostLoad.
        CObject::Serialize(Ar);
    }

    void CTextureRenderTarget::PostLoad()
    {
        BuildResource();
    }

    EFormat CTextureRenderTarget::GetRHIFormat() const
    {
        switch (Format)
        {
        case ERenderTargetFormat::RGBA16F: return EFormat::RGBA16_FLOAT;
        case ERenderTargetFormat::RGBA8:
        default:                           return EFormat::RGBA8_UNORM;
        }
    }

    void CTextureRenderTarget::BuildResource()
    {
        LUMINA_MEMORY_SCOPE("Textures");

        if (!TextureResource)
        {
            TextureResource = MakeUnique<FTextureResource>();
        }

        // No CPU mip data: the target is GPU-only.
        TextureResource->Mips.clear();

        FRHIImageDesc& Desc = TextureResource->ImageDescription;
        Desc = FRHIImageDesc{};
        Desc.Extent             = FUIntVector2(Width  > 0 ? Width  : 1u, Height > 0 ? Height : 1u);
        Desc.Format             = GetRHIFormat();
        Desc.Dimension          = EImageDimension::Texture2D;
        Desc.NumMips            = 1;
        Desc.InitialState       = EResourceStates::ShaderResource;
        Desc.bKeepInitialState  = true;
        Desc.DebugName          = GetName().ToString();
        // ShaderResource so materials sample it; Storage so the paint compute writes its UAV.
        Desc.Flags.SetFlag(EImageCreateFlags::ShaderResource);
        Desc.Flags.SetFlag(EImageCreateFlags::Storage);

        // Releases any previous image (FRHIImageRef is ref-counted) and registers the new one
        // into the bindless table via FTextureManager (FVulkanImage ctor).
        TextureResource->RHIImage = GRenderContext->CreateImage(Desc);

        // Clear to ClearColor so a sampler never reads uninitialized memory before the first paint.
        FRHICommandListRef CommandList = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
        CommandList->Open();
        CommandList->ClearImageFloat(TextureResource->RHIImage, AllSubresources,
            FColor(ClearColor.r, ClearColor.g, ClearColor.b, ClearColor.a));
        CommandList->Close();
        GRenderContext->ExecuteCommandList(CommandList, ECommandQueue::Graphics);
    }
}
