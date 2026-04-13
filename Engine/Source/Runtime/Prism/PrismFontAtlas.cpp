#include "pch.h"
#include "PrismFontAtlas.h"

#include "FileSystem/FileSystem.h"
#include "Paths/Paths.h"
#include "Renderer/CommandList.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderManager.h"
#include "Renderer/RHIGlobals.h"
#include "Renderer/TextureManager.h"

#define STBTT_DEF static
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"
#include "Platform/Filesystem/FileHelper.h"

namespace Lumina::Prism
{
    namespace
    {
        // Atlas resolution. 1024x1024 @ R8 is 1 MiB and fits the whole
        // baked ASCII range at 32px with room to spare, so we don't need
        // a rect-packer yet. Maybe look back later?
        constexpr uint32 kAtlasWidth   = 1024;
        constexpr uint32 kAtlasHeight  = 1024;
        constexpr uint32 kPaddingPx    = 2;
    }

    bool FPrismFontAtlas::Initialize(const FString& FontPath, float InBasePixelSize)
    {
        if (bReady)
        {
            return true;
        }

        TVector<uint8> FontBytes;
        if (!FileHelper::LoadFileToArray(FontBytes, FontPath) || FontBytes.empty())
        {
            return false;
        }

        stbtt_fontinfo Info{};
        if (!stbtt_InitFont(&Info, FontBytes.data(), 0))
        {
            return false;
        }

        BasePixelSize       = InBasePixelSize;
        const float Scale   = stbtt_ScaleForPixelHeight(&Info, InBasePixelSize);

        int AscentI = 0, DescentI = 0, LineGapI = 0;
        stbtt_GetFontVMetrics(&Info, &AscentI, &DescentI, &LineGapI);
        Ascent  = AscentI  * Scale;
        Descent = DescentI * Scale;
        LineGap = LineGapI * Scale;

        TVector<uint8> AtlasPixels;
        AtlasPixels.resize(size_t(kAtlasWidth) * size_t(kAtlasHeight), 0);

        uint32 PenX = kPaddingPx;
        uint32 PenY = kPaddingPx;
        uint32 RowMaxH = 0;

        for (uint32 i = 0; i < kGlyphCount; ++i)
        {
            const int Codepoint = int(kFirstCodepoint + i);

            int AdvI = 0, LsbI = 0;
            stbtt_GetCodepointHMetrics(&Info, Codepoint, &AdvI, &LsbI);

            int X0 = 0, Y0 = 0, X1 = 0, Y1 = 0;
            stbtt_GetCodepointBitmapBox(&Info, Codepoint, Scale, Scale, &X0, &Y0, &X1, &Y1);
            const int GlyphW = X1 - X0;
            const int GlyphH = Y1 - Y0;

            FPrismGlyph& Out = Glyphs[i];
            Out.Advance = AdvI * Scale;
            Out.Offset  = glm::vec2(float(X0), float(Y0));
            Out.Size    = glm::vec2(float(GlyphW), float(GlyphH));

            if (GlyphW <= 0 || GlyphH <= 0)
            {
                Out.UV0 = glm::vec2(0.0f);
                Out.UV1 = glm::vec2(0.0f);
                continue;
            }

            // Wrap to the next row if this glyph won't fit on the current one.
            if (PenX + static_cast<uint32>(GlyphW) + kPaddingPx >= kAtlasWidth)
            {
                PenX = kPaddingPx;
                PenY += RowMaxH + kPaddingPx;
                RowMaxH = 0;
            }
            if (PenY + static_cast<uint32>(GlyphH) + kPaddingPx >= kAtlasHeight)
            {
                // Ran out of atlas room, skip remaining glyphs.
                break;
            }

            uint8* Dst = AtlasPixels.data() + (size_t(PenY) * kAtlasWidth) + PenX;
            stbtt_MakeCodepointBitmap(&Info, Dst, GlyphW, GlyphH, kAtlasWidth, Scale, Scale, Codepoint);

            Out.UV0 = glm::vec2(float(PenX)        / float(kAtlasWidth), float(PenY)          / float(kAtlasHeight));
            Out.UV1 = glm::vec2(float(PenX+GlyphW) / float(kAtlasWidth), float(PenY+GlyphH) / float(kAtlasHeight));

            PenX += static_cast<uint32>(GlyphW) + kPaddingPx;
            RowMaxH = glm::max(RowMaxH, static_cast<uint32>(GlyphH));
        }

        FallbackGlyph = Glyphs[static_cast<uint32>(' ') - kFirstCodepoint];

        FRHIImageDesc ImageDesc;
        ImageDesc.Format             = EFormat::R8_UNORM;
        ImageDesc.Extent             = glm::uvec2(kAtlasWidth, kAtlasHeight);
        ImageDesc.NumMips            = 1;
        ImageDesc.DebugName          = "Prism.FontAtlas";
        ImageDesc.InitialState       = EResourceStates::ShaderResource;
        ImageDesc.bKeepInitialState  = true;
        ImageDesc.Flags.SetFlag(EImageCreateFlags::ShaderResource);

        AtlasImage = GRenderContext->CreateImage(ImageDesc);
        if (!AtlasImage)
        {
            return false;
        }

        FRHICommandListRef CopyList = GRenderContext->CreateCommandList(FCommandListInfo::Transfer());
        CopyList->Open();
        CopyList->WriteImage(AtlasImage.GetReference(), 0, 0, AtlasPixels.data(), kAtlasWidth, 0);
        CopyList->Close();
        GRenderContext->ExecuteCommandList(CopyList, ECommandQueue::Transfer);

        GRenderManager->GetTextureManager().AddTexture(AtlasImage.GetReference());
        BindlessIndex = static_cast<uint32>(AtlasImage->GetTextureCacheIndex());
        bReady        = true;
        return true;
    }

    const FPrismGlyph& FPrismFontAtlas::GetGlyph(uint32 Codepoint) const
    {
        if (Codepoint < kFirstCodepoint || Codepoint > kLastCodepoint)
        {
            return FallbackGlyph;
        }
        return Glyphs[Codepoint - kFirstCodepoint];
    }

    FPrismFontAtlas& FPrismFontAtlas::GetDefault()
    {
        static FPrismFontAtlas Instance;
        static bool bAttempted = false;
        if (!bAttempted && !Instance.IsReady())
        {
            const FString FontPath = Paths::GetEngineFontDirectory() + "/JetbrainsMono/JetBrainsMono-Regular.ttf";
            if (Instance.Initialize(FontPath, 32.0f) || GRenderContext != nullptr)
            {
                // Lock out further attempts once the renderer exists, a
                // failure at that point is a missing font, not a startup
                // race, and retrying every frame would just spam logs.
                bAttempted = true;
            }
        }
        return Instance;
    }
}
