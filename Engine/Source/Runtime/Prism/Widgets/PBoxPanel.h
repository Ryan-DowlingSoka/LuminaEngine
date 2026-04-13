#pragma once

#include "Prism/Widget.h"
#include "Prism/WidgetDeclaration.h"
#include "Containers/Array.h"

namespace Lumina::Prism
{
    // SBoxPanel lays out its children left-to-right or top-to-bottom. Each
    // slot can be either "auto-sized" (take the child's desired size) or
    // "filled" with a stretch factor that divides the remaining space among
    // greedy children proportionally.
    class PBoxPanel : public PWidget
    {
    public:
        struct FSlot
        {
            FWidgetPtr     Widget;
            FPrismMargin   Padding;
            EPrismHAlign   HAlign     = EPrismHAlign::Fill;
            EPrismVAlign   VAlign     = EPrismVAlign::Fill;
            float          FillFactor = 0.0f; // 0 = auto, >0 = proportional fill
            bool           bAutoSize  = true;

            FSlot& SetPadding(const FPrismMargin& P)  { Padding = P; return *this; }
            FSlot& SetPadding(float Uniform)          { Padding = FPrismMargin(Uniform); return *this; }
            FSlot& SetHAlign(EPrismHAlign H)          { HAlign = H; return *this; }
            FSlot& SetVAlign(EPrismVAlign V)          { VAlign = V; return *this; }
            FSlot& AutoSize()                         { bAutoSize = true;  FillFactor = 0.0f; return *this; }
            FSlot& Fill(float Factor = 1.0f)          { bAutoSize = false; FillFactor = Factor; return *this; }
            FSlot& operator[](const FWidgetPtr& In)   { Widget = In; return *this; }
        };

        explicit PBoxPanel(EPrismOrientation InOrient) : Orientation(InOrient) {}

        FSlot& AddSlot()
        {
            Slots.emplace_back();
            return Slots.back();
        }

        size_t     GetChildCount() const override        { return Slots.size(); }
        FWidgetPtr GetChildAt(size_t Index) const override
        {
            return (Index < Slots.size()) ? Slots[Index].Widget : nullptr;
        }

        void ArrangeChildren(const FPrismGeometry& Allotted) override
        {
            const bool bHoriz = Orientation == EPrismOrientation::Horizontal;
            const float MainAxisTotal = bHoriz ? Allotted.LocalSize.x : Allotted.LocalSize.y;
            const float CrossAxis     = bHoriz ? Allotted.LocalSize.y : Allotted.LocalSize.x;

            // Sum up rigid desired size and total fill factor.
            float UsedRigid = 0.0f;
            float TotalFill = 0.0f;
            for (const FSlot& S : Slots)
            {
                if (!S.Widget || !S.Widget->IsVisible())
                {
                    continue;
                }
                const glm::vec2 Desired = S.Widget->GetDesiredSize();
                const glm::vec2 Pad     = S.Padding.GetTotal();
                if (S.bAutoSize)
                {
                    UsedRigid += (bHoriz ? Desired.x + Pad.x : Desired.y + Pad.y);
                }
                else
                {
                    UsedRigid += (bHoriz ? Pad.x : Pad.y);
                    TotalFill += glm::max(S.FillFactor, 0.0f);
                }
            }

            const float Remaining = glm::max(0.0f, MainAxisTotal - UsedRigid);

            float Cursor = 0.0f;
            for (FSlot& S : Slots)
            {
                if (!S.Widget || !S.Widget->IsVisible())
                {
                    continue;
                }

                const glm::vec2 Desired = S.Widget->GetDesiredSize();
                const glm::vec2 Pad     = S.Padding.GetTotal();

                float MainSize;
                if (S.bAutoSize)
                {
                    MainSize = bHoriz ? Desired.x : Desired.y;
                }
                else
                {
                    const float Share = (TotalFill > 0.0f) ? (S.FillFactor / TotalFill) : 0.0f;
                    MainSize = Remaining * Share;
                }

                float CrossSize = bHoriz ? Desired.y : Desired.x;
                const float CrossInner = glm::max(0.0f, CrossAxis - (bHoriz ? Pad.y : Pad.x));
                const EPrismHAlign HA = S.HAlign;
                const EPrismVAlign VA = S.VAlign;
                const bool bCrossFill = bHoriz ? (VA == EPrismVAlign::Fill) : (HA == EPrismHAlign::Fill);
                if (bCrossFill)
                {
                    CrossSize = CrossInner;
                }
                CrossSize = glm::min(CrossSize, CrossInner);

                float CrossOffset = 0.0f;
                if (bHoriz)
                {
                    switch (VA)
                    {
                    case EPrismVAlign::Center: CrossOffset = (CrossInner - CrossSize) * 0.5f; break;
                    case EPrismVAlign::Bottom: CrossOffset =  CrossInner - CrossSize;         break;
                    default: break;
                    }
                }
                else
                {
                    switch (HA)
                    {
                    case EPrismHAlign::Center: CrossOffset = (CrossInner - CrossSize) * 0.5f; break;
                    case EPrismHAlign::Right:  CrossOffset =  CrossInner - CrossSize;         break;
                    default: break;
                    }
                }

                const glm::vec2 LocalOffset = bHoriz
                    ? glm::vec2(Cursor + S.Padding.Left, S.Padding.Top + CrossOffset)
                    : glm::vec2(S.Padding.Left + CrossOffset, Cursor + S.Padding.Top);

                const glm::vec2 ChildSize = bHoriz
                    ? glm::vec2(MainSize, CrossSize)
                    : glm::vec2(CrossSize, MainSize);

                S.Widget->SetGeometry(Allotted.MakeChild(LocalOffset, ChildSize));
                S.Widget->ArrangeChildren(S.Widget->GetGeometry());

                Cursor += MainSize + (bHoriz ? Pad.x : Pad.y);
            }
        }

        uint16 OnPaint(const FPrismPaintContext& Context) const override
        {
            uint16 MaxLayer = Context.GetLayer();
            for (const FSlot& S : Slots)
            {
                if (!S.Widget || !S.Widget->IsDrawn())
                {
                    continue;
                }
                const FPrismRect ChildRect = S.Widget->GetGeometry().GetRect();
                const FPrismPaintContext ChildCtx = Context.WithChildClip(ChildRect);
                const uint16 L = S.Widget->OnPaint(ChildCtx);
                if (L > MaxLayer)
                {
                    MaxLayer = L;
                }
            }
            return MaxLayer;
        }

    protected:
        glm::vec2 ComputeDesiredSize() const override
        {
            const bool bHoriz = Orientation == EPrismOrientation::Horizontal;
            glm::vec2 Total{0.0f};
            for (const FSlot& S : Slots)
            {
                if (!S.Widget || !S.Widget->IsVisible())
                {
                    continue;
                }
                const glm::vec2 Desired = S.Widget->GetDesiredSize() + S.Padding.GetTotal();
                if (bHoriz)
                {
                    Total.x += Desired.x;
                    Total.y  = glm::max(Total.y, Desired.y);
                }
                else
                {
                    Total.y += Desired.y;
                    Total.x  = glm::max(Total.x, Desired.x);
                }
            }
            return Total;
        }

        EPrismOrientation Orientation = EPrismOrientation::Horizontal;
        TVector<FSlot>    Slots;
    };

    class PHorizontalBox : public PBoxPanel
    {
    public:
        PHorizontalBox() : PBoxPanel(EPrismOrientation::Horizontal) {}
        PRISM_BEGIN_ARGS(SHorizontalBox) PRISM_END_ARGS()
        void Construct(const FArguments&) {}
        static FSlot Slot() { return FSlot{}; }
    };

    class PVerticalBox : public PBoxPanel
    {
    public:
        PVerticalBox() : PBoxPanel(EPrismOrientation::Vertical) {}
        PRISM_BEGIN_ARGS(SVerticalBox) PRISM_END_ARGS()
        void Construct(const FArguments&) {}
        static FSlot Slot() { return FSlot{}; }
    };
}
