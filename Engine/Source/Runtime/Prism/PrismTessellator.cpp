#include "PCH.h"
#include "PrismTessellator.h"
#include "PrismFontAtlas.h"

namespace Lumina::Prism
{
    namespace
    {
        constexpr float kDefaultFontAdvance = 0.55f;
        constexpr float kDefaultFontHeight  = 1.25f;

        inline glm::vec4 ClipVec4(const FPrismRect& C)
        {
            return { C.Min.x, C.Min.y, C.Max.x, C.Max.y };
        }

        inline glm::vec4 ColorVec4(const FPrismColor& C)
        {
            // Premultiply alpha in advance so the blend pipeline only has to
            // do (One, OneMinusSrcAlpha).
            return { C.R * C.A, C.G * C.A, C.B * C.A, C.A };
        }
    }

    uint32 FPrismTessellator::AppendQuad(FPrismVertexStream& Out,
                                         const glm::vec2& MinPos, const glm::vec2& MaxPos,
                                         const glm::vec2& UV0,    const glm::vec2& UV1,
                                         const glm::vec4& Color,  const glm::vec4& ClipRect,
                                         const glm::vec2& LocalSize,
                                         const glm::vec4& Params)
    {
        const uint32 Base = (uint32)Out.Vertices.size();

        FPrismVertex V;
        V.Color     = Color;
        V.ClipRect  = ClipRect;
        V.LocalSize = LocalSize;
        V.Params    = Params;

        V.Position = { MinPos.x, MinPos.y }; V.UV = { UV0.x, UV0.y }; Out.Vertices.push_back(V); // TL
        V.Position = { MaxPos.x, MinPos.y }; V.UV = { UV1.x, UV0.y }; Out.Vertices.push_back(V); // TR
        V.Position = { MaxPos.x, MaxPos.y }; V.UV = { UV1.x, UV1.y }; Out.Vertices.push_back(V); // BR
        V.Position = { MinPos.x, MaxPos.y }; V.UV = { UV0.x, UV1.y }; Out.Vertices.push_back(V); // BL

        Out.Indices.push_back(Base + 0);
        Out.Indices.push_back(Base + 1);
        Out.Indices.push_back(Base + 2);
        Out.Indices.push_back(Base + 0);
        Out.Indices.push_back(Base + 2);
        Out.Indices.push_back(Base + 3);

        return Base;
    }

    void FPrismTessellator::EmitRect(FPrismVertexStream& Out, const FPrismDrawElement& E) const
    {
        const glm::vec4 Params(E.CornerRadius, 0.0f, float(EPrismPrimType::Rect), 0.0f);
        AppendQuad(Out,
            E.Rect.Min, E.Rect.Max,
            glm::vec2(0.0f), glm::vec2(1.0f),
            ColorVec4(E.Color),
            ClipVec4(E.ClipRect),
            E.Rect.GetSize(),
            Params);
    }

    void FPrismTessellator::EmitBorder(FPrismVertexStream& Out, const FPrismDrawElement& E) const
    {
        // Inflate the quad by half the thickness so the outline isn't clipped
        // against the primitive bounds. The SDF math in the pixel shader
        // handles the actual stroke shape.
        const float HT = E.Thickness * 0.5f;
        const glm::vec2 ExpandedMin = E.Rect.Min - glm::vec2(HT);
        const glm::vec2 ExpandedMax = E.Rect.Max + glm::vec2(HT);
        const glm::vec2 Size        = ExpandedMax - ExpandedMin;

        const glm::vec4 Params(E.CornerRadius, E.Thickness, float(EPrismPrimType::Border), 0.0f);
        AppendQuad(Out,
            ExpandedMin, ExpandedMax,
            glm::vec2(0.0f), glm::vec2(1.0f),
            ColorVec4(E.Color),
            ClipVec4(E.ClipRect),
            Size,
            Params);
    }

    void FPrismTessellator::EmitImage(FPrismVertexStream& Out, const FPrismDrawElement& E) const
    {
        const uint32    TexIdx = (uint32)E.TextureHandle; // backend maps atlas to index
        const glm::vec4 Params(0.0f, 0.0f, float(EPrismPrimType::Image), float(TexIdx));
        AppendQuad(Out,
            E.Rect.Min, E.Rect.Max,
            E.UV0, E.UV1,
            ColorVec4(E.Color),
            ClipVec4(E.ClipRect),
            E.Rect.GetSize(),
            Params);
    }

    void FPrismTessellator::EmitLine(FPrismVertexStream& Out, const FPrismDrawElement& E) const
    {
        // Line endpoints live in Rect.Min and Rect.Max. Build a thick quad
        // by offsetting along the line normal.
        const glm::vec2 A = E.Rect.Min;
        const glm::vec2 B = E.Rect.Max;
        glm::vec2 Dir = B - A;
        const float Len = glm::length(Dir);
        if (Len < 1e-4f) return;
        Dir /= Len;
        const glm::vec2 Normal(-Dir.y, Dir.x);
        const float HT = glm::max(E.Thickness, 1.0f) * 0.5f;
        const glm::vec2 Offset = Normal * HT;

        const glm::vec4 Color    = ColorVec4(E.Color);
        const glm::vec4 ClipRect = ClipVec4(E.ClipRect);
        const glm::vec4 Params(0.0f, 0.0f, float(EPrismPrimType::Line), 0.0f);
        const glm::vec2 LocalSize(Len, HT * 2.0f);

        const uint32 Base = (uint32)Out.Vertices.size();
        FPrismVertex V;
        V.Color     = Color;
        V.ClipRect  = ClipRect;
        V.LocalSize = LocalSize;
        V.Params    = Params;

        V.Position = A - Offset; V.UV = { 0.0f, 0.0f }; Out.Vertices.push_back(V);
        V.Position = B - Offset; V.UV = { 1.0f, 0.0f }; Out.Vertices.push_back(V);
        V.Position = B + Offset; V.UV = { 1.0f, 1.0f }; Out.Vertices.push_back(V);
        V.Position = A + Offset; V.UV = { 0.0f, 1.0f }; Out.Vertices.push_back(V);

        Out.Indices.push_back(Base + 0);
        Out.Indices.push_back(Base + 1);
        Out.Indices.push_back(Base + 2);
        Out.Indices.push_back(Base + 0);
        Out.Indices.push_back(Base + 2);
        Out.Indices.push_back(Base + 3);
    }

    void FPrismTessellator::EmitText(FPrismVertexStream& Out, const FPrismDrawElement& E, const FString& Text, const FPrismFontAtlas* FontAtlas) const
    {
        if (Text.empty())
        {
            return;
        }

        const glm::vec4 Color    = ColorVec4(E.Color);
        const glm::vec4 ClipRect = ClipVec4(E.ClipRect);

        // Fallback path when the atlas hasn't finished initializing yet:
        // emit colored boxes so text positions are still visible.
        if (!FontAtlas || !FontAtlas->IsReady())
        {
            const float Advance  = E.FontSize * kDefaultFontAdvance;
            const float GlyphH   = E.FontSize;
            const float BaselineY = E.Rect.Min.y + (E.FontSize * kDefaultFontHeight - GlyphH) * 0.5f;

            float Cursor = E.Rect.Min.x;
            for (size_t i = 0; i < Text.size(); ++i)
            {
                const unsigned char Ch = (unsigned char)Text[i];
                if (Ch == ' ')
                {
                    Cursor += Advance;
                    continue;
                }
                const glm::vec2 Min(Cursor, BaselineY);
                const glm::vec2 Max(Cursor + Advance * 0.9f, BaselineY + GlyphH);
                const glm::vec4 Params(0.0f, 0.0f, static_cast<float>(EPrismPrimType::Rect), 0.0f);
                AppendQuad(Out, Min, Max, glm::vec2(0.0f), glm::vec2(1.0f), Color, ClipRect, Max - Min, Params);
                Cursor += Advance;
            }
            return;
        }

        // Real atlas path. Every glyph is a textured quad sampled through
        // the PRIM_TEXT path in Prism.slang; the atlas stores the glyph
        // coverage in the red channel, and we multiply by the vertex color.
        const float BasePx    = FontAtlas->GetBasePixelSize();
        const float Scale     = (BasePx > 0.0f) ? (E.FontSize / BasePx) : 1.0f;
        const float AscentPx  = FontAtlas->GetAscent()     * Scale;
        const float Baseline  = E.Rect.Min.y + AscentPx;
        const uint32 AtlasIdx = FontAtlas->GetBindlessIndex();

        const glm::vec4 Params(0.0f, 0.0f, float(EPrismPrimType::Text), float(AtlasIdx));

        float Cursor = E.Rect.Min.x;
        for (size_t i = 0; i < Text.size(); ++i)
        {
            const uint32 Codepoint = (unsigned char)Text[i];
            const FPrismGlyph& G = FontAtlas->GetGlyph(Codepoint);

            if (G.Size.x > 0.0f && G.Size.y > 0.0f)
            {
                const glm::vec2 Min(Cursor + G.Offset.x * Scale, Baseline + G.Offset.y * Scale);
                const glm::vec2 Max(Min.x + G.Size.x * Scale,    Min.y + G.Size.y * Scale);
                AppendQuad(Out, Min, Max, G.UV0, G.UV1, Color, ClipRect, Max - Min, Params);
            }

            Cursor += G.Advance * Scale;
        }
    }

    void FPrismTessellator::Tessellate(const FPrismDrawList& InDrawList, FPrismVertexStream& Out, const FPrismFontAtlas* FontAtlas) const
    {
        Out.Reset();

        const auto& Elements = InDrawList.GetElements();
        Out.Vertices.reserve(Elements.size() * 4);
        Out.Indices.reserve(Elements.size() * 6);

        for (const FPrismDrawElement& E : Elements)
        {
            switch (E.Type)
            {
            case EPrismDrawElementType::Rect:   EmitRect  (Out, E); break;
            case EPrismDrawElementType::Border: EmitBorder(Out, E); break;
            case EPrismDrawElementType::Image:  EmitImage (Out, E); break;
            case EPrismDrawElementType::Line:   EmitLine  (Out, E); break;
            case EPrismDrawElementType::Text:
                EmitText(Out, E, InDrawList.GetText(E.TextIndex), FontAtlas);
                break;
            }
        }
    }
}
