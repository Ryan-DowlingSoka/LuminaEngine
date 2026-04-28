#pragma once

#include "Containers/Array.h"
#include "Events/EventProcessor.h"
#include "Memory/SmartPtr.h"

namespace Lumina
{
    class CWorld;
    class FEvent;
    class FInputContext;

    class FInputViewport
    {
    public:

        RUNTIME_API FInputViewport();
        RUNTIME_API ~FInputViewport();
        LE_NO_COPYMOVE(FInputViewport);

        RUNTIME_API void   SetWorld(CWorld* InWorld) { World = InWorld; }
        RUNTIME_API CWorld* GetWorld() const { return World; }

        RUNTIME_API void SetWindowRect(int MinX, int MinY, int MaxX, int MaxY);
        RUNTIME_API void SetRenderTargetSize(uint32 W, uint32 H);

        RUNTIME_API void SetHovered(bool b) { bHoveredFlag = b; }
        RUNTIME_API bool IsHovered() const  { return bHoveredFlag; }

        RUNTIME_API void SetFocused(bool b) { bFocusedFlag = b; }
        RUNTIME_API bool IsFocused() const  { return bFocusedFlag; }

        RUNTIME_API FInputContext&       GetContext()       { return *Context; }
        RUNTIME_API const FInputContext& GetContext() const { return *Context; }

        bool RouteEvent(FEvent& Event);
        bool ContainsWindowPoint(int WindowX, int WindowY) const;

    private:

        bool ForwardKeyEventToRmlUi(FEvent& Event);
        bool ForwardMouseEventToRmlUi(FEvent& Event);

        TUniquePtr<FInputContext> Context;
        CWorld* World = nullptr;

        bool bHoveredFlag = false;
        bool bFocusedFlag = false;
    };

    class FInputViewportRegistry : public IEventHandler
    {
    public:

        RUNTIME_API static FInputViewportRegistry& Get();

        RUNTIME_API void Register(FInputViewport* Viewport);
        RUNTIME_API void Unregister(FInputViewport* Viewport);

        RUNTIME_API FInputViewport* GetActiveViewport()  const { return ActiveViewport; }
        RUNTIME_API FInputViewport* GetHoveredViewport() const { return HoveredViewport; }
        RUNTIME_API FInputViewport* GetFocusedViewport() const { return FocusedViewport; }

        RUNTIME_API void SetActiveViewport(FInputViewport* Viewport);
        RUNTIME_API void SetHoveredViewport(FInputViewport* Viewport);
        RUNTIME_API void SetFocusedViewport(FInputViewport* Viewport);

        // SetMouseMode on the context alone doesn't touch the window cursor.
        RUNTIME_API void ReapplyActiveCursorMode();

        RUNTIME_API void EndFrame(double DeltaSeconds);
        RUNTIME_API void DispatchActions();
        RUNTIME_API void OnWindowFocusLost();

        bool OnEvent(FEvent& Event) override;

    private:

        void ApplyActiveCursorMode();

        TVector<FInputViewport*> Viewports;
        FInputViewport* ActiveViewport  = nullptr;
        FInputViewport* HoveredViewport = nullptr;
        FInputViewport* FocusedViewport = nullptr;
    };
}
