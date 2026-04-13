#pragma once

#include "Prism/Widget.h"
#include "Prism/WidgetDeclaration.h"
#include "Containers/Function.h"

namespace Lumina::Prism
{
    // Minimal pressable button with normal / hovered / pressed tint states.
    // Click delivery is eager, the delegate fires on mouse-up if the cursor
    // is still over the button. A release outside the button cancels the
    // press.
    class PButton : public PWidget
    {
    public:
        using FOnClicked = TFunction<FPrismReply()>;

        PRISM_BEGIN_ARGS(SButton)
            PRISM_ARGUMENT(FString,  Text)
            PRISM_ARGUMENT_DEFAULT(FPrismColor,   NormalColor,  FPrismColor(0.18f, 0.18f, 0.22f, 1.0f))
            PRISM_ARGUMENT_DEFAULT(FPrismColor,   HoverColor,   FPrismColor(0.24f, 0.24f, 0.30f, 1.0f))
            PRISM_ARGUMENT_DEFAULT(FPrismColor,   PressedColor, FPrismColor(0.10f, 0.10f, 0.14f, 1.0f))
            PRISM_ARGUMENT_DEFAULT(FPrismColor,   TextColor,    FPrismColor::White())
            PRISM_ARGUMENT_DEFAULT(float,         FontSize,     14.0f)
            PRISM_ARGUMENT_DEFAULT(float,         CornerRadius, 3.0f)
            PRISM_ARGUMENT_DEFAULT(FPrismMargin,  Padding,      FPrismMargin(10.0f, 4.0f))
            PRISM_EVENT(FOnClicked,               OnClicked)
            PRISM_DEFAULT_SLOT(Content)
        PRISM_END_ARGS()

        void Construct(const FArguments& A)
        {
            Text         = A._Text;
            NormalColor  = A._NormalColor;
            HoverColor   = A._HoverColor;
            PressedColor = A._PressedColor;
            TextColor    = A._TextColor;
            FontSize     = A._FontSize;
            CornerRadius = A._CornerRadius;
            Padding      = A._Padding;
            OnClicked    = A._OnClicked;
            Child        = A._Content;
        }

        size_t     GetChildCount() const override        { return Child ? 1 : 0; }
        FWidgetPtr GetChildAt(size_t Index) const override { return Index == 0 ? Child : nullptr; }

        void ArrangeChildren(const FPrismGeometry& Allotted) override
        {
            if (!Child || !Child->IsVisible())
            {
                return;
            }

            const glm::vec2 Inner   = glm::max(glm::vec2(0.0f), Allotted.LocalSize - Padding.GetTotal());
            const glm::vec2 Desired = glm::min(Child->GetDesiredSize(), Inner);
            const glm::vec2 Offset  = Padding.GetTopLeft() + (Inner - Desired) * 0.5f;
            Child->SetGeometry(Allotted.MakeChild(Offset, Desired));
            Child->ArrangeChildren(Child->GetGeometry());
        }

        uint16 OnPaint(const FPrismPaintContext& Context) const override
        {
            const FPrismRect Rect = GetGeometry().GetRect();
            const FPrismColor BgColor = bPressed ? PressedColor : (bHovered ? HoverColor : NormalColor);
            Context.DrawRect(Rect, BgColor, CornerRadius);

            if (Child && Child->IsDrawn())
            {
                const FPrismPaintContext ChildCtx = Context.WithChildClip(Rect);
                return Child->OnPaint(ChildCtx);
            }

            if (!Text.empty())
            {
                // Center the label within the padded content rect.
                const glm::vec2 TextSize(float(Text.size()) * FontSize * 0.55f, FontSize * 1.25f);
                const glm::vec2 Origin = Rect.Min + Padding.GetTopLeft()
                                       + (Rect.GetSize() - Padding.GetTotal() - TextSize) * 0.5f;
                const FPrismRect TextRect = FPrismRect::FromSize(Origin, TextSize);
                Context.DrawText(TextRect, Text, FontSize, TextColor);
            }
            return Context.GetLayer();
        }

        FPrismReply OnMouseEnter(const FPrismPointerEvent&) override
        {
            bHovered = true;
            return FPrismReply::Handled().UseCursor(EPrismCursor::Hand);
        }

        FPrismReply OnMouseLeave(const FPrismPointerEvent&) override
        {
            bHovered = false;
            bPressed = false;
            return FPrismReply::Handled();
        }

        FPrismReply OnMouseButtonDown(const FPrismPointerEvent& E) override
        {
            if (E.Button != EPrismMouseButton::Left)
            {
                return FPrismReply::Unhandled();
            }
            bPressed = true;
            return FPrismReply::Handled().CaptureMouse(GetSharedThis());
        }

        FPrismReply OnMouseButtonUp(const FPrismPointerEvent& E) override
        {
            if (E.Button != EPrismMouseButton::Left)
            {
                return FPrismReply::Unhandled();
            }
            const bool bWasPressed = bPressed;
            bPressed = false;
            FPrismReply Reply = FPrismReply::Handled().ReleaseMouseCapture();
            if (bWasPressed && bHovered && OnClicked)
            {
                FPrismReply User = OnClicked();
                if (User.IsHandled())
                {
                    return User.ReleaseMouseCapture();
                }
            }
            return Reply;
        }

    protected:
        glm::vec2 ComputeDesiredSize() const override
        {
            glm::vec2 Inner{0.0f};
            if (Child && Child->IsVisible())
            {
                Inner = Child->GetDesiredSize();
            }
            else if (!Text.empty())
            {
                Inner = glm::vec2(float(Text.size()) * FontSize * 0.55f, FontSize * 1.25f);
            }
            return Inner + Padding.GetTotal();
        }

        FWidgetPtr GetSharedThis() { return shared_from_this(); }

    private:
        FWidgetPtr   Child;
        FString      Text;
        FPrismColor  NormalColor;
        FPrismColor  HoverColor;
        FPrismColor  PressedColor;
        FPrismColor  TextColor     = FPrismColor::White();
        float        FontSize      = 14.0f;
        float        CornerRadius  = 3.0f;
        FPrismMargin Padding;
        FOnClicked   OnClicked;
        bool         bHovered = false;
        bool         bPressed = false;
    };
}
