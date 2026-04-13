#pragma once

#include "Prism/Widget.h"
#include "Prism/WidgetDeclaration.h"

namespace Lumina::Prism
{
    // Single-child decorator that paints a filled background and an optional
    // outline before drawing its content.
    class SBorder : public SWidget
    {
    public:
        PRISM_BEGIN_ARGS(SBorder)
            PRISM_ARGUMENT_DEFAULT(FPrismColor,  BackgroundColor, FPrismColor(0.12f, 0.12f, 0.14f, 1.0f))
            PRISM_ARGUMENT_DEFAULT(FPrismColor,  BorderColor,     FPrismColor(0.0f,  0.0f,  0.0f,  0.0f))
            PRISM_ARGUMENT_DEFAULT(float,        BorderThickness, 0.0f)
            PRISM_ARGUMENT_DEFAULT(float,        CornerRadius,    0.0f)
            PRISM_ARGUMENT_DEFAULT(FPrismMargin, Padding,         FPrismMargin(4.0f))
            PRISM_ARGUMENT_DEFAULT(EPrismHAlign, HAlign,          EPrismHAlign::Fill)
            PRISM_ARGUMENT_DEFAULT(EPrismVAlign, VAlign,          EPrismVAlign::Fill)
            PRISM_DEFAULT_SLOT(Content)
        PRISM_END_ARGS()

        void Construct(const FArguments& A)
        {
            BackgroundColor = A._BackgroundColor;
            BorderColor     = A._BorderColor;
            BorderThickness = A._BorderThickness;
            CornerRadius    = A._CornerRadius;
            Padding         = A._Padding;
            HAlign          = A._HAlign;
            VAlign          = A._VAlign;
            Child           = A._Content;
        }

        void SetContent(const FWidgetPtr& InChild) { Child = InChild; }

        size_t     GetChildCount() const override        { return Child ? 1 : 0; }
        FWidgetPtr GetChildAt(size_t Index) const override { return Index == 0 ? Child : nullptr; }

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
            if (HAlign == EPrismHAlign::Center)
            {
                Offset.x += (Inner.x - ChildSize.x) * 0.5f;
            }
            if (HAlign == EPrismHAlign::Right)
            {
                Offset.x +=  Inner.x - ChildSize.x;
            }
            if (VAlign == EPrismVAlign::Center)
            {
                Offset.y += (Inner.y - ChildSize.y) * 0.5f;
            }
            if (VAlign == EPrismVAlign::Bottom)
            {
                Offset.y +=  Inner.y - ChildSize.y;
            }

            Child->SetGeometry(Allotted.MakeChild(Offset, ChildSize));
            Child->ArrangeChildren(Child->GetGeometry());
        }

        uint16 OnPaint(const FPrismPaintContext& Context) const override
        {
            const FPrismRect Rect = GetGeometry().GetRect();
            Context.DrawRect(Rect, BackgroundColor, CornerRadius);
            if (BorderThickness > 0.0f && BorderColor.A > 0.0f)
            {
                Context.DrawBorder(Rect, BorderColor, BorderThickness, CornerRadius);
            }

            if (Child && Child->IsDrawn())
            {
                const FPrismPaintContext ChildCtx = Context.WithChildClip(Rect);
                return Child->OnPaint(ChildCtx);
            }
            return Context.GetLayer();
        }

    protected:
        glm::vec2 ComputeDesiredSize() const override
        {
            glm::vec2 Inner{0.0f};
            if (Child && Child->IsVisible())
            {
                Inner = Child->GetDesiredSize();
            }
            return Inner + Padding.GetTotal();
        }

    private:
        FWidgetPtr   Child;
        FPrismColor  BackgroundColor;
        FPrismColor  BorderColor;
        float        BorderThickness = 0.0f;
        float        CornerRadius    = 0.0f;
        FPrismMargin Padding;
        EPrismHAlign HAlign = EPrismHAlign::Fill;
        EPrismVAlign VAlign = EPrismVAlign::Fill;
    };
}
