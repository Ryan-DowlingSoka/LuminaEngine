#pragma once

#include "Widget.h"
#include "PrismDrawElement.h"
#include "PrismPaintContext.h"
#include "PrismEvent.h"
#include "PrismRenderer.h"
#include "Memory/SmartPtr.h"

namespace Lumina::Prism
{
    // FPrismApplication owns the root widget tree, routes platform input
    // events into it, runs the layout pass each frame, and hands the paint
    // output to the backend renderer. One instance per platform window.
    class RUNTIME_API FPrismApplication
    {
    public:
        FPrismApplication();
        ~FPrismApplication() = default;
        
        
        void Shutdown();

        // Root widget. Replacing it drops the previous tree entirely, the
        // SharedPtr ensures sub-trees still held by user code stay alive.
        void              SetRootWidget(const FWidgetPtr& InRoot);
        const FWidgetPtr& GetRootWidget() const { return Root; }

        // Window surface that layout targets. Update whenever the platform
        // window resizes so the next Tick re-flows the tree.
        void              SetWindowSize(const glm::vec2& InSize);
        const glm::vec2&  GetWindowSize() const { return WindowSize; }
        
        FPrismRenderer&   GetRenderer() const { return *Renderer; }


        // Runs layout + paint and submits the draw list to the backend. This
        // is the single entry point the engine's game loop should call.
        void              Tick(float DeltaTime);
        
        
        // Each of these returns true if the event was consumed by a widget.
        // They hit-test the resolved geometry from the last Tick, so for
        // best responsiveness the host should Tick before dispatching input.
        bool DispatchMouseMove      (const FPrismPointerEvent& E);
        bool DispatchMouseButtonDown(const FPrismPointerEvent& E);
        bool DispatchMouseButtonUp  (const FPrismPointerEvent& E);
        bool DispatchMouseWheel     (const FPrismPointerEvent& E);
        bool DispatchKeyDown        (const FPrismKeyEvent& E);
        bool DispatchKeyUp          (const FPrismKeyEvent& E);
        bool DispatchChar           (const FPrismKeyEvent& E);

        EPrismCursor GetCurrentCursor() const { return CurrentCursor; }

    private:
        // Recursively caches desired sizes on every visible widget so that
        // parents can read their children without a second traversal.
        void CacheDesiredSizes(const FWidgetPtr& Node);

        // Depth-first hit test. Returns the leaf-most widget whose rect
        // contains Point and accepts hit testing.
        FWidgetPtr HitTest(const FWidgetPtr& Node, const glm::vec2& Point);

        // Apply delegate side effects (focus change, capture, cursor).
        void ApplyReply(const FPrismReply& Reply);

        FWidgetPtr                  Root;
        FWidgetPtr                  HoveredWidget;
        FWidgetPtr                  CapturedWidget;
        FWidgetPtr                  FocusedWidget;
        glm::vec2                   WindowSize{0.0f};
        FPrismDrawList              DrawList;
        TUniquePtr<FPrismRenderer>  Renderer;
        EPrismCursor                CurrentCursor = EPrismCursor::Default;
    };
}
