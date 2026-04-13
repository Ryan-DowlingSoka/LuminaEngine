#pragma once

#include "PrismDrawElement.h"
#include "PrismTypes.h"

namespace Lumina::Prism
{
    // Context handed to SWidget::OnPaint. It forwards all drawing to a shared
    // FPrismDrawList owned by the application and tracks the current clipping
    // rectangle / layer depth so child widgets automatically render above their
    // parent without the caller needing to manage layer bookkeeping.
    class FPrismPaintContext
    {
    public:
        FPrismPaintContext(FPrismDrawList& InList, const FPrismRect& InClip, uint16 InLayer)
            : DrawList(&InList), ClipRect(InClip), Layer(InLayer) {}

        FPrismDrawList&   GetDrawList() const { return *DrawList; }
        const FPrismRect& GetClipRect() const { return ClipRect; }
        uint16            GetLayer()    const { return Layer; }

        // Produce a child context with an inner clip rect and higher layer.
        FPrismPaintContext WithChildClip(const FPrismRect& ChildClip, uint16 LayerBump = 1) const
        {
            return FPrismPaintContext(*DrawList, ClipRect.Intersect(ChildClip), uint16(Layer + LayerBump));
        }

        void DrawRect(const FPrismRect& R, const FPrismColor& Color, float CornerRadius = 0.0f) const
        {
            DrawList->AddRect(Layer, R, ClipRect, Color, CornerRadius);
        }

        void DrawBorder(const FPrismRect& R, const FPrismColor& Color, float Thickness, float CornerRadius = 0.0f) const
        {
            DrawList->AddBorder(Layer, R, ClipRect, Color, Thickness, CornerRadius);
        }

        void DrawImage(const FPrismRect& R, uint64 Texture, const glm::vec2& UV0, const glm::vec2& UV1, const FPrismColor& Tint) const
        {
            DrawList->AddImage(Layer, R, ClipRect, Texture, UV0, UV1, Tint);
        }

        void DrawText(const FPrismRect& R, const FString& Text, float FontSize, const FPrismColor& Color) const
        {
            DrawList->AddText(Layer, R, ClipRect, Text, FontSize, Color);
        }

        void DrawLine(const glm::vec2& A, const glm::vec2& B, const FPrismColor& Color, float Thickness) const
        {
            DrawList->AddLine(Layer, A, B, ClipRect, Color, Thickness);
        }

    private:
        FPrismDrawList* DrawList = nullptr;
        FPrismRect      ClipRect;
        uint16          Layer = 0;
    };
}
