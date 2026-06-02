#include "pch.h"
#include "TextureRenderTargetFactory.h"
#include "Assets/AssetTypes/Textures/TextureRenderTarget.h"
#include "Core/Math/Math.h"
#include "Core/Object/Package/Package.h"
#include "Core/Object/Package/Thumbnail/PackageThumbnail.h"
#include "Renderer/CommandList.h"
#include "Renderer/Format.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderResource.h"
#include "Renderer/RHIGlobals.h"
#include "Thumbnails/ThumbnailUtils.h"

namespace Lumina
{
    namespace
    {
        // IEEE 754 binary16 -> binary32 (render targets may be RGBA16_FLOAT).
        FORCEINLINE float HalfToFloat(uint16 H)
        {
            const uint32 Sign = (H & 0x8000u) << 16;
            uint32 Exp        = (H & 0x7C00u) >> 10;
            uint32 Mant       = (H & 0x03FFu);

            uint32 Bits;
            if (Exp == 0)
            {
                if (Mant == 0)
                {
                    Bits = Sign;
                }
                else
                {
                    Exp = 1;
                    while ((Mant & 0x0400u) == 0)
                    {
                        Mant <<= 1;
                        --Exp;
                    }
                    Mant &= 0x03FFu;
                    Bits = Sign | ((Exp + (127 - 15)) << 23) | (Mant << 13);
                }
            }
            else if (Exp == 0x1F)
            {
                Bits = Sign | 0x7F800000u | (Mant << 13);
            }
            else
            {
                Bits = Sign | ((Exp + (127 - 15)) << 23) | (Mant << 13);
            }

            float Out;
            Memory::Memcpy(&Out, &Bits, sizeof(Out));
            return Out;
        }

        // Reads the live GPU image back and writes a 256x256 RGBA8 thumbnail. Contents
        // aren't serialized, so the capture is whatever the target holds (clear color if fresh).
        void GenerateRenderTargetThumbnail(CTextureRenderTarget* RenderTarget)
        {
            FRHIImage* Image = RenderTarget ? RenderTarget->GetRHIRef() : nullptr;
            CPackage* Package = RenderTarget ? RenderTarget->GetPackage() : nullptr;
            if (Image == nullptr || Package == nullptr)
            {
                return;
            }

            const FRHIImageDesc& Desc = Image->GetDescription();
            const uint32 SourceWidth  = Desc.Extent.x;
            const uint32 SourceHeight = Desc.Extent.y;
            if (SourceWidth == 0 || SourceHeight == 0)
            {
                return;
            }

            const bool bIsHalf  = (Desc.Format == EFormat::RGBA16_FLOAT);
            const bool bIsRGBA8 = (Desc.Format == EFormat::RGBA8_UNORM) || (Desc.Format == EFormat::SRGBA8_UNORM);
            if (!bIsHalf && !bIsRGBA8)
            {
                return;
            }

            FRHIStagingImageRef Staging = GRenderContext->CreateStagingImage(Desc, ERHIAccess::HostRead);
            if (!Staging)
            {
                return;
            }

            FRHICommandListRef CommandList = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
            CommandList->Open();
            CommandList->CopyImage(Image, FTextureSlice(), Staging, FTextureSlice());
            CommandList->Close();
            GRenderContext->ExecuteCommandList(CommandList);

            // Wait for the copy (and the build-time clear) to land in host-visible memory.
            GRenderContext->WaitIdle();

            size_t RowPitch = 0;
            const uint8* SourceData = static_cast<const uint8*>(
                GRenderContext->MapStagingTexture(Staging, FTextureSlice(), ERHIAccess::HostRead, &RowPitch));
            if (SourceData == nullptr)
            {
                return;
            }

            // The shared helper expects RGBA8. RGBA8 maps pass through with their row pitch; RGBA16F render
            // targets are clamped to [0,1] and packed to RGBA8 first (display-range RTs, no tonemap needed).
            const uint8* RGBA8Source = SourceData;
            size_t       SourcePitch = RowPitch;
            TVector<uint8> Converted;
            if (bIsHalf)
            {
                Converted.resize(static_cast<size_t>(SourceWidth) * SourceHeight * 4);
                for (uint32 Y = 0; Y < SourceHeight; ++Y)
                {
                    const uint16* Row = reinterpret_cast<const uint16*>(SourceData + static_cast<size_t>(Y) * RowPitch);
                    uint8* Dst = Converted.data() + static_cast<size_t>(Y) * SourceWidth * 4;
                    for (uint32 X = 0; X < SourceWidth; ++X)
                    {
                        for (uint32 Channel = 0; Channel < 4; ++Channel)
                        {
                            const float V = Math::Clamp(HalfToFloat(Row[X * 4 + Channel]), 0.0f, 1.0f);
                            Dst[X * 4 + Channel] = static_cast<uint8>(V * 255.0f + 0.5f);
                        }
                    }
                }
                RGBA8Source = Converted.data();
                SourcePitch = static_cast<size_t>(SourceWidth) * 4;
            }

            ThumbnailUtils::StoreDownsampledRGBA(*Package->GetPackageThumbnail(),
                RGBA8Source, SourceWidth, SourceHeight, SourcePitch);

            GRenderContext->UnMapStagingTexture(Staging);
        }
    }

    CObject* CTextureRenderTargetFactory::CreateNew(const FName& Name, CPackage* Package)
    {
        CTextureRenderTarget* RenderTarget = NewObject<CTextureRenderTarget>(Package, Name);
        // Allocate + clear the GPU image now so the asset is sampleable the moment it's created.
        RenderTarget->BuildResource();
        // Capture the freshly-built image so the content browser has a thumbnail instead of the generic icon.
        GenerateRenderTargetThumbnail(RenderTarget);
        return RenderTarget;
    }
}
