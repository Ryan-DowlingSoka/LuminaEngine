#pragma once

#include "PrismTypes.h"
#include "PrismPaintContext.h"
#include "PrismEvent.h"
#include "Memory/SmartPtr.h"
#include "Containers/String.h"

namespace Lumina::Prism
{
    class SWidget;
    using FWidgetPtr = TSharedPtr<SWidget>;

    // Base class for every Prism widget. Widgets are owned via TSharedPtr and
    // follow a two-phase layout protocol:
    //
    //   1. ComputeDesiredSize() bubbles up to produce an ideal size for each
    //      widget, cached on the widget itself so subsequent queries are free.
    //   2. The parent assigns a final geometry via SetGeometry() and calls
    //      ArrangeChildren() to position its children inside that geometry.
    //
    // Painting walks the resolved tree and emits draw elements into a draw
    // list via the FPrismPaintContext handed in from the parent.
    class SWidget : public TSharedFromThis<SWidget>
    {
    public:
        SWidget() = default;
        virtual ~SWidget() = default;

        // ------------------------------------------------------------------
        // Layout
        // ------------------------------------------------------------------
        const glm::vec2&     GetDesiredSize() const { return CachedDesiredSize; }
        const FPrismGeometry& GetGeometry()   const { return Geometry; }

        void SetGeometry(const FPrismGeometry& InGeometry) { Geometry = InGeometry; }

        // Recompute this widget's desired size. Walks children so callers only
        // need to invoke it on the tree root.
        const glm::vec2& CacheDesiredSize()
        {
            CachedDesiredSize = ComputeDesiredSize();
            return CachedDesiredSize;
        }

        virtual void ArrangeChildren(const FPrismGeometry& /*Allotted*/) {}
        
        // Returns the max layer the widget emitted to, so siblings can stack
        // above it. Default implementation paints nothing.
        virtual uint16 OnPaint(const FPrismPaintContext& /*Context*/) const { return 0; }

        // ------------------------------------------------------------------
        // Input
        // ------------------------------------------------------------------
        virtual FPrismReply OnMouseButtonDown(const FPrismPointerEvent&) { return FPrismReply::Unhandled(); }
        virtual FPrismReply OnMouseButtonUp  (const FPrismPointerEvent&) { return FPrismReply::Unhandled(); }
        virtual FPrismReply OnMouseMove      (const FPrismPointerEvent&) { return FPrismReply::Unhandled(); }
        virtual FPrismReply OnMouseWheel     (const FPrismPointerEvent&) { return FPrismReply::Unhandled(); }
        virtual FPrismReply OnMouseEnter     (const FPrismPointerEvent&) { return FPrismReply::Unhandled(); }
        virtual FPrismReply OnMouseLeave     (const FPrismPointerEvent&) { return FPrismReply::Unhandled(); }
        virtual FPrismReply OnKeyDown        (const FPrismKeyEvent&)     { return FPrismReply::Unhandled(); }
        virtual FPrismReply OnKeyUp          (const FPrismKeyEvent&)     { return FPrismReply::Unhandled(); }
        virtual FPrismReply OnChar           (const FPrismKeyEvent&)     { return FPrismReply::Unhandled(); }
        
        // Called when the widget is discovered at a hit-test point. Returns
        // true if traversal should descend into the widget's children.
        virtual bool HitTest(const glm::vec2& AbsolutePoint) const
        {
            return Visibility != EPrismVisibility::Collapsed
                && Visibility != EPrismVisibility::Hidden
                && Visibility != EPrismVisibility::HitTestInvisible
                && Geometry.ContainsAbsolute(AbsolutePoint);
        }

        // Allow parents to visit children without knowing the concrete type.
        // Default is a leaf widget; containers override to expose children.
        virtual size_t      GetChildCount() const          { return 0; }
        virtual FWidgetPtr  GetChildAt(size_t) const       { return nullptr; }
        
        void                SetVisibility(EPrismVisibility V) { Visibility = V; }
        EPrismVisibility    GetVisibility() const             { return Visibility; }
        bool                IsVisible() const                 { return Visibility != EPrismVisibility::Collapsed; }
        bool                IsDrawn()   const                 { return Visibility == EPrismVisibility::Visible || Visibility == EPrismVisibility::HitTestInvisible; }

        void                SetTag(const FString& InTag)      { Tag = InTag; }
        const FString&      GetTag() const                    { return Tag; }

    protected:
        virtual glm::vec2 ComputeDesiredSize() const { return glm::vec2(0.0f); }

        FPrismGeometry    Geometry;
        glm::vec2         CachedDesiredSize{0.0f};
        EPrismVisibility  Visibility = EPrismVisibility::Visible;
        FString           Tag;
    };
}
