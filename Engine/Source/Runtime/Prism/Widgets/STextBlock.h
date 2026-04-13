#pragma once

#include "Prism/Widget.h"
#include "Prism/WidgetDeclaration.h"
#include "Prism/PrismFontAtlas.h"

namespace Lumina::Prism
{
    // Text label. Font rasterization is backend-driven: the widget only emits
    // a text draw element with a logical font size, and the renderer resolves
    // that to an atlas sample during submission. For desired-size calculation
    // we use a cheap heuristic so layouts still compose;
    class STextBlock : public SWidget
    {
    public:
        PRISM_BEGIN_ARGS(STextBlock)
            PRISM_ARGUMENT(FString,            Text)
            PRISM_ARGUMENT_DEFAULT(FPrismColor, ColorAndOpacity, FPrismColor::White())
            PRISM_ARGUMENT_DEFAULT(float,       FontSize,        14.0f)
        PRISM_END_ARGS()

        void Construct(const FArguments& A)
        {
            Text            = A._Text;
            ColorAndOpacity = A._ColorAndOpacity;
            FontSize        = A._FontSize;
        }

        void           SetText(const FString& InText)      { Text = InText; }
        const FString& GetText() const                     { return Text; }
        void           SetColor(const FPrismColor& InColor){ ColorAndOpacity = InColor; }
        void           SetFontSize(float InSize)           { FontSize = InSize; }

        uint16 OnPaint(const FPrismPaintContext& Context) const override
        {
            Context.DrawText(GetGeometry().GetRect(), Text, FontSize, ColorAndOpacity);
            return Context.GetLayer();
        }

    protected:
        glm::vec2 ComputeDesiredSize() const override
        {
            const FPrismFontAtlas& Atlas = FPrismFontAtlas::GetDefault();
            if (!Atlas.IsReady() || Atlas.GetBasePixelSize() <= 0.0f)
            {
                // Heuristic fallback if the atlas hasn't uploaded yet
                // (typically only on the very first frame).
                const float Width  = static_cast<float>(Text.size()) * FontSize * 0.55f;
                const float Height = FontSize * 1.25f;
                return { Width, Height };
            }

            const float Scale      = FontSize / Atlas.GetBasePixelSize();
            const float LineHeight = Atlas.GetLineHeight() * Scale;

            float Width = 0.0f;
            for (size_t i = 0; i < Text.size(); ++i)
            {
                const uint32 Codepoint = (uint32)(unsigned char)Text[i];
                Width += Atlas.GetGlyph(Codepoint).Advance * Scale;
            }
            return { Width, LineHeight };
        }

    private:
        FString     Text;
        FPrismColor ColorAndOpacity = FPrismColor::White();
        float       FontSize = 14.0f;
    };
}
