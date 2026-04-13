#pragma once

#include "PrismTypes.h"
#include "Containers/Array.h"
#include "Containers/String.h"

namespace Lumina::Prism
{
    enum class EPrismDrawElementType : uint8
    {
        Rect,
        Border,
        Text,
        Image,
        Line,
    };

    // A single GPU-bound draw primitive emitted during painting. Backends turn
    // these into batched triangle lists; keeping it POD-ish makes batching cheap.
    struct FPrismDrawElement
    {
        EPrismDrawElementType Type   = EPrismDrawElementType::Rect;
        uint16                LayerId = 0;
        FPrismRect            Rect;
        FPrismRect            ClipRect;
        FPrismColor           Color  = FPrismColor::White();

        // Border / corner radius.
        float                 Thickness    = 0.0f;
        float                 CornerRadius = 0.0f;

        // Image: opaque handle the backend resolves to a GPU texture.
        // Zero means "use the white 1x1 fallback".
        uint64                TextureHandle = 0;
        glm::vec2             UV0{0.0f};
        glm::vec2             UV1{1.0f};

        // Text: stored out of line so the struct stays small for the common case.
        uint32                TextIndex = 0;
        float                 FontSize  = 14.0f;

        // Line primitive endpoints reuse Rect.Min/Rect.Max as p0/p1.
    };

    // Growable list of draw elements produced by a paint traversal. The list
    // also owns string storage so text elements can reference stable memory.
    class FPrismDrawList
    {
    public:
        void Reset()
        {
            Elements.clear();
            TextStorage.clear();
        }

        void AddRect(uint16 Layer, const FPrismRect& R, const FPrismRect& Clip, const FPrismColor& Color, float CornerRadius = 0.0f)
        {
            FPrismDrawElement E;
            E.Type         = EPrismDrawElementType::Rect;
            E.LayerId      = Layer;
            E.Rect         = R;
            E.ClipRect     = Clip;
            E.Color        = Color;
            E.CornerRadius = CornerRadius;
            Elements.push_back(E);
        }

        void AddBorder(uint16 Layer, const FPrismRect& R, const FPrismRect& Clip, const FPrismColor& Color, float Thickness, float CornerRadius = 0.0f)
        {
            FPrismDrawElement E;
            E.Type         = EPrismDrawElementType::Border;
            E.LayerId      = Layer;
            E.Rect         = R;
            E.ClipRect     = Clip;
            E.Color        = Color;
            E.Thickness    = Thickness;
            E.CornerRadius = CornerRadius;
            Elements.push_back(E);
        }

        void AddImage(uint16 Layer, const FPrismRect& R, const FPrismRect& Clip, uint64 Texture, const glm::vec2& UV0, const glm::vec2& UV1, const FPrismColor& Tint)
        {
            FPrismDrawElement E;
            E.Type          = EPrismDrawElementType::Image;
            E.LayerId       = Layer;
            E.Rect          = R;
            E.ClipRect      = Clip;
            E.Color         = Tint;
            E.TextureHandle = Texture;
            E.UV0           = UV0;
            E.UV1           = UV1;
            Elements.push_back(E);
        }

        void AddText(uint16 Layer, const FPrismRect& R, const FPrismRect& Clip, const FString& Text, float FontSize, const FPrismColor& Color)
        {
            FPrismDrawElement E;
            E.Type      = EPrismDrawElementType::Text;
            E.LayerId   = Layer;
            E.Rect      = R;
            E.ClipRect  = Clip;
            E.Color     = Color;
            E.FontSize  = FontSize;
            E.TextIndex = (uint32)TextStorage.size();
            TextStorage.push_back(Text);
            Elements.push_back(E);
        }

        void AddLine(uint16 Layer, const glm::vec2& A, const glm::vec2& B, const FPrismRect& Clip, const FPrismColor& Color, float Thickness)
        {
            FPrismDrawElement E;
            E.Type      = EPrismDrawElementType::Line;
            E.LayerId   = Layer;
            E.Rect      = FPrismRect(A, B);
            E.ClipRect  = Clip;
            E.Color     = Color;
            E.Thickness = Thickness;
            Elements.push_back(E);
        }

        const TVector<FPrismDrawElement>& GetElements() const { return Elements; }
        const FString&                    GetText(uint32 Index) const { return TextStorage[Index]; }

    private:
        TVector<FPrismDrawElement> Elements;
        TVector<FString>           TextStorage;
    };
}
