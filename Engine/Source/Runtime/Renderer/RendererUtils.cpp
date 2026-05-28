#include "pch.h"
#include "RendererUtils.h"
#include "RenderContext.h"
#include "RenderResource.h"
#include "RHIGlobals.h"
#include "Tools/Import/ImportHelpers.h"

namespace Lumina::RenderUtils
{
    bool ResizeBufferIfNeeded(FRHIBufferRef& Buffer, uint32 DesiredSize, float GrowthFactor, uint32& LowUsageFrames)
    {
        const uint32 CurrentCapacity = (uint32)Buffer->GetSize();

        auto Recreate = [&](uint32 NewCapacity)
        {
            FRHIBufferDesc Desc = Buffer->GetDescription();
            Desc.Size = NewCapacity;
            FRHIBufferRef OldBuffer = Buffer;   // keep alive across the swap
            Buffer = GRenderContext->CreateBuffer(Desc);
            LowUsageFrames = 0;
        };

        // Grow immediately, with GrowthFactor headroom so a ramping scene doesn't realloc every frame.
        if (CurrentCapacity < DesiredSize)
        {
            Recreate(std::max(DesiredSize, static_cast<uint32>(static_cast<float>(CurrentCapacity) * GrowthFactor)));
            return true;
        }

        constexpr float  kShrinkUsageThreshold = 0.5f;   // "low" = live size < 50% of capacity
        constexpr uint32 kShrinkAfterFrames    = 180;    // ~3s at 60fps of sustained low usage
        constexpr float  kShrinkHeadroom       = 1.5f;   // shrink to 1.5x the live size
        constexpr uint32 kMinCapacity          = 4096;   // never shrink to nothing

        if (DesiredSize <= static_cast<uint32>(static_cast<float>(CurrentCapacity) * kShrinkUsageThreshold))
        {
            if (++LowUsageFrames >= kShrinkAfterFrames)
            {
                const uint32 NewCapacity = std::max(kMinCapacity, static_cast<uint32>(static_cast<float>(DesiredSize) * kShrinkHeadroom));
                if (NewCapacity < CurrentCapacity)
                {
                    Recreate(NewCapacity);
                    return true;
                }
                LowUsageFrames = kShrinkAfterFrames;   // already at target; hold, don't overflow
            }
        }
        else
        {
            LowUsageFrames = 0;
        }

        return false;
    }

    FRHIImageRef CreateImageFromPixels(TSpan<uint8> PixelData, bool bFlipVertically, FUIntVector2 Size)
    {
        TOptional<Import::Textures::FTextureImportResult> Result = Import::Textures::ImportTexture(PixelData, bFlipVertically, Size);
        if (!Result.has_value())
        {
            return nullptr;
        }
        
        FRHIImageDesc ImageDescription;
        ImageDescription.Format = Result->Format;
        ImageDescription.Extent = Result->Dimensions;
        ImageDescription.Flags.SetFlag(EImageCreateFlags::ShaderResource);
        ImageDescription.NumMips = 1;
        ImageDescription.InitialState = EResourceStates::ShaderResource;
        ImageDescription.bKeepInitialState = true;
        
        FRHIImageRef ReturnImage = GRenderContext->CreateImage(ImageDescription);

        const uint32 Width = ImageDescription.Extent.x;
        const uint32 RowPitch = Width * RHI::Format::BytesPerBlock(Result->Format);

        FRHICommandListRef TransferCommandList = GRenderContext->CreateCommandList(FCommandListInfo::Transfer());
        TransferCommandList->Open();
        TransferCommandList->WriteImage(ReturnImage, 0, 0, Result->Pixels.data(), RowPitch, 0);
        TransferCommandList->Close();
        GRenderContext->ExecuteCommandList(TransferCommandList, ECommandQueue::Transfer);

        return ReturnImage;
    }
}
