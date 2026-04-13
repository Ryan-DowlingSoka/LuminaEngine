#pragma once

#include "Prism/Widget.h"
#include "Prism/WidgetDeclaration.h"

namespace Lumina::Prism
{
    // Single-child container that injects padding, min/max size clamping and
    // alignment for its content.
    class PBox : public PWidget
    {
    public:
        PRISM_BEGIN_ARGS(SBox)
            PRISM_ARGUMENT_DEFAULT(FPrismMargin,   Padding,         FPrismMargin(0.0f))
            PRISM_ARGUMENT_DEFAULT(EPrismHAlign,   HAlign,          EPrismHAlign::Fill)
            PRISM_ARGUMENT_DEFAULT(EPrismVAlign,   VAlign,          EPrismVAlign::Fill)
            PRISM_ARGUMENT_DEFAULT(float,          WidthOverride,   0.0f)
            PRISM_ARGUMENT_DEFAULT(float,          HeightOverride,  0.0f)
            PRISM_ARGUMENT_DEFAULT(float,          MinWidth,        0.0f)
            PRISM_ARGUMENT_DEFAULT(float,          MinHeight,       0.0f)
            PRISM_ARGUMENT_DEFAULT(float,          MaxWidth,        0.0f)
            PRISM_ARGUMENT_DEFAULT(float,          MaxHeight,       0.0f)
            PRISM_DEFAULT_SLOT(Content)
        PRISM_END_ARGS()

        void Construct(const FArguments& A)
        {
            Padding        = A._Padding;
            HAlign         = A._HAlign;
            VAlign         = A._VAlign;
            WidthOverride  = A._WidthOverride;
            HeightOverride = A._HeightOverride;
            MinWidth       = A._MinWidth;
            MinHeight      = A._MinHeight;
            MaxWidth       = A._MaxWidth;
            MaxHeight      = A._MaxHeight;
            Child          = A._Content;
        }

        void SetContent(const FWidgetPtr& InChild) { Child = InChild; }
        const FWidgetPtr& GetContent() const       { return Child; }

        size_t     GetChildCount() const override       { return Child ? 1 : 0; }
        FWidgetPtr GetChildAt(size_t Index) const override
        {
            return (Index == 0) ? Child : nullptr;
        }

        void ArrangeChildren(const FPrismGeometry& Allotted) override
        {
            if (!Child || !Child->IsVisible())
            {
                return;
            }

            const glm::vec2 Inner = glm::max(glm::vec2(0.0f), Allotted.LocalSize - Padding.GetTotal());
            const glm::vec2 Desired = Child->GetDesiredSize();

            glm::vec2 ChildSize = Desired;
            if (HAlign == EPrismHAlign::Fill)
            {
                ChildSize.x = Inner.x;
            }
            if (VAlign == EPrismVAlign::Fill)
            {
                ChildSize.y = Inner.y;
            }
            ChildSize = glm::min(ChildSize, Inner);

            glm::vec2 Offset = Padding.GetTopLeft();
            switch (HAlign)
            {
            case EPrismHAlign::Center: Offset.x += (Inner.x - ChildSize.x) * 0.5f; break;
            case EPrismHAlign::Right:  Offset.x +=  Inner.x - ChildSize.x;         break;
            default: break;
            }
            switch (VAlign)
            {
            case EPrismVAlign::Center: Offset.y += (Inner.y - ChildSize.y) * 0.5f; break;
            case EPrismVAlign::Bottom: Offset.y +=  Inner.y - ChildSize.y;         break;
            default: break;
            }

            Child->SetGeometry(Allotted.MakeChild(Offset, ChildSize));
            Child->ArrangeChildren(Child->GetGeometry());
        }

        uint16 OnPaint(const FPrismPaintContext& Context) const override
        {
            if (!Child || !Child->IsDrawn())
            {
                return Context.GetLayer();
            }
            return Child->OnPaint(Context);
        }

    protected:
        glm::vec2 ComputeDesiredSize() const override
        {
            glm::vec2 Inner{0.0f};
            if (Child && Child->IsVisible())
            {
                // PWidget::CacheDesiredSize is non-const; traversal is done
                // from the application before paint, so here we only read the
                // cached value.
                Inner = Child->GetDesiredSize();
            }

            glm::vec2 Total = Inner + Padding.GetTotal();

            if (WidthOverride  > 0.0f)
            {
                Total.x = WidthOverride;
            }
            if (HeightOverride > 0.0f)
            {
                Total.y = HeightOverride;
            }
            if (MinWidth  > 0.0f)
            {
                Total.x = glm::max(Total.x, MinWidth);
            }
            if (MinHeight > 0.0f)
            {
                Total.y = glm::max(Total.y, MinHeight);
            }
            if (MaxWidth  > 0.0f)
            {
                Total.x = glm::min(Total.x, MaxWidth);
            }
            if (MaxHeight > 0.0f)
            {
                Total.y = glm::min(Total.y, MaxHeight);
            }

            return Total;
        }

    private:
        FWidgetPtr    Child;
        FPrismMargin  Padding;
        EPrismHAlign  HAlign         = EPrismHAlign::Fill;
        EPrismVAlign  VAlign         = EPrismVAlign::Fill;
        float         WidthOverride  = 0.0f;
        float         HeightOverride = 0.0f;
        float         MinWidth  = 0.0f;
        float         MinHeight = 0.0f;
        float         MaxWidth  = 0.0f;
        float         MaxHeight = 0.0f;
    };
}
