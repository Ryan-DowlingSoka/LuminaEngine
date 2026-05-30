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

            constexpr uint32 ThumbWidth    = 256;
            constexpr uint32 ThumbHeight   = 256;
            constexpr uint32 BytesPerPixel = 4;

            FPackageThumbnail* Thumbnail = Package->GetPackageThumbnail();
            Thumbnail->ImageWidth  = ThumbWidth;
            Thumbnail->ImageHeight = ThumbHeight;
            Thumbnail->ImageData.resize(ThumbWidth * ThumbHeight * BytesPerPixel);
            Thumbnail->LoadState.store(FPackageThumbnail::EState::None, std::memory_order_relaxed);

            uint8* DestData = Thumbnail->ImageData.data();

            const float ScaleX = static_cast<float>(SourceWidth)  / ThumbWidth;
            const float ScaleY = static_cast<float>(SourceHeight) / ThumbHeight;

            auto SampleTexel = [&](uint32 X, uint32 Y, float OutRGBA[4])
            {
                const uint8* Row = SourceData + static_cast<size_t>(Y) * RowPitch;
                if (bIsHalf)
                {
                    const uint16* Px = reinterpret_cast<const uint16*>(Row) + static_cast<size_t>(X) * 4;
                    for (uint32 Channel = 0; Channel < 4; ++Channel)
                    {
                        OutRGBA[Channel] = Math::Clamp(HalfToFloat(Px[Channel]), 0.0f, 1.0f) * 255.0f;
                    }
                }
                else
                {
                    const uint8* Px = Row + static_cast<size_t>(X) * BytesPerPixel;
                    for (uint32 Channel = 0; Channel < 4; ++Channel)
                    {
                        OutRGBA[Channel] = static_cast<float>(Px[Channel]);
                    }
                }
            };

            for (uint32 DestY = 0; DestY < ThumbHeight; ++DestY)
            {
                const uint32 FlippedDestY = ThumbHeight - 1 - DestY;
                for (uint32 DestX = 0; DestX < ThumbWidth; ++DestX)
                {
                    const float SrcX = DestX * ScaleX;
                    const float SrcY = DestY * ScaleY;

                    const uint32 X0 = static_cast<uint32>(SrcX);
                    const uint32 Y0 = static_cast<uint32>(SrcY);
                    const uint32 X1 = Math::Min(X0 + 1, SourceWidth  - 1);
                    const uint32 Y1 = Math::Min(Y0 + 1, SourceHeight - 1);

                    const float FracX = SrcX - X0;
                    const float FracY = SrcY - Y0;

                    float P00[4], P10[4], P01[4], P11[4];
                    SampleTexel(X0, Y0, P00);
                    SampleTexel(X1, Y0, P10);
                    SampleTexel(X0, Y1, P01);
                    SampleTexel(X1, Y1, P11);

                    uint8* DestPixel = DestData + (static_cast<size_t>(FlippedDestY) * ThumbWidth + DestX) * BytesPerPixel;
                    for (uint32 Channel = 0; Channel < 4; ++Channel)
                    {
                        const float Top    = Math::Lerp(P00[Channel], P10[Channel], FracX);
                        const float Bottom = Math::Lerp(P01[Channel], P11[Channel], FracX);
                        DestPixel[Channel] = static_cast<uint8>(Math::Clamp(Math::Lerp(Top, Bottom, FracY) + 0.5f, 0.0f, 255.0f));
                    }
                }
            }

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
