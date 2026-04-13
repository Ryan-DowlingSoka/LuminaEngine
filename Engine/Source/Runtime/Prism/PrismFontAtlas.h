#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"
#include "Renderer/RenderResource.h"

namespace Lumina::Prism
{
    struct FPrismGlyph
    {
        glm::vec2 UV0{0.0f};
        glm::vec2 UV1{0.0f};
        glm::vec2 Size{0.0f};
        glm::vec2 Offset{0.0f};
        float     Advance = 0.0f;
    };
    
    class RUNTIME_API FPrismFontAtlas
    {
    public:
        FPrismFontAtlas() = default;
        ~FPrismFontAtlas() = default;
        LE_NO_COPYMOVE(FPrismFontAtlas);

        // Rasterize the TTF at FontPath into the atlas and register it with
        // the bindless texture manager. Safe to call multiple times; only
        // the first successful call does the work.
        bool Initialize(const FString& FontPath, float InBasePixelSize = 32.0f);

        bool IsReady() const { return bReady; }

        // Bindless index used by EmitText vertices. Only valid after
        // Initialize() succeeds.
        uint32 GetBindlessIndex() const { return BindlessIndex; }

        float GetBasePixelSize() const { return BasePixelSize; }
        float GetAscent()        const { return Ascent;        }
        float GetDescent()       const { return Descent;       }
        float GetLineGap()       const { return LineGap;       }
        float GetLineHeight()    const { return Ascent - Descent + LineGap; }

        // Glyph lookup. Returns the replacement glyph (space) for anything
        // outside the baked ASCII range.
        const FPrismGlyph& GetGlyph(uint32 Codepoint) const;

        // Process-wide default font. Created on first access; subsequent
        // calls return the same instance. Safe to call from widget code
        // during sizing.
        static FPrismFontAtlas& GetDefault();

    private:
        
        static constexpr uint32 kFirstCodepoint = 32;
        static constexpr uint32 kLastCodepoint  = 126;
        static constexpr uint32 kGlyphCount     = kLastCodepoint - kFirstCodepoint + 1;

        bool          bReady         = false;
        float         BasePixelSize  = 0.0f;
        float         Ascent         = 0.0f;
        float         Descent        = 0.0f;
        float         LineGap        = 0.0f;
        uint32        BindlessIndex  = 0;
        FPrismGlyph   Glyphs[kGlyphCount]{};
        FPrismGlyph   FallbackGlyph{};
        FRHIImageRef  AtlasImage;
    };
}
