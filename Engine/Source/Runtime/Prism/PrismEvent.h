#pragma once

#include "PrismTypes.h"
#include "Memory/SmartPtr.h"

namespace Lumina::Prism
{
    class SWidget;

    enum class EPrismMouseButton : uint8
    {
        Left,
        Right,
        Middle,
        Count,
    };

    enum class EPrismInputEventType : uint8
    {
        MouseMove,
        MouseButtonDown,
        MouseButtonUp,
        MouseWheel,
        KeyDown,
        KeyUp,
        Char,
        FocusReceived,
        FocusLost,
    };

    struct FPrismModifierKeys
    {
        bool bShift = false;
        bool bCtrl  = false;
        bool bAlt   = false;
        bool bSuper = false;
    };

    struct FPrismPointerEvent
    {
        glm::vec2          ScreenPosition{0.0f};
        glm::vec2          Delta{0.0f};
        float              WheelDelta = 0.0f;
        EPrismMouseButton  Button = EPrismMouseButton::Left;
        FPrismModifierKeys Modifiers;
    };

    struct FPrismKeyEvent
    {
        int32              KeyCode = 0;
        uint32             CharCode = 0;
        bool               bRepeat = false;
        FPrismModifierKeys Modifiers;
    };

    // Widgets return an FPrismReply from event handlers to tell the application
    // whether the event was consumed and what side effects it wants (capture,
    // focus change, cursor override).
    class FPrismReply
    {
    public:
        static FPrismReply Handled()   { FPrismReply R; R.bHandled = true;  return R; }
        static FPrismReply Unhandled() { FPrismReply R; R.bHandled = false; return R; }

        FPrismReply& CaptureMouse(const TSharedPtr<SWidget>& Widget)  { MouseCapture = Widget;  return *this; }
        FPrismReply& ReleaseMouseCapture()                            { bReleaseCapture = true; return *this; }
        FPrismReply& SetUserFocus(const TSharedPtr<SWidget>& Widget)  { FocusTarget = Widget;   return *this; }
        FPrismReply& UseCursor(EPrismCursor C)                        { Cursor = C; bHasCursor = true; return *this; }

        bool                  IsHandled()       const { return bHandled; }
        bool                  WantsReleaseCapture() const { return bReleaseCapture; }
        bool                  HasCursor()       const { return bHasCursor; }
        EPrismCursor          GetCursor()       const { return Cursor; }
        const TSharedPtr<SWidget>& GetMouseCapture() const { return MouseCapture; }
        const TSharedPtr<SWidget>& GetFocusTarget()  const { return FocusTarget; }

    private:
        bool                 bHandled        = false;
        bool                 bReleaseCapture = false;
        bool                 bHasCursor      = false;
        EPrismCursor         Cursor          = EPrismCursor::Default;
        TSharedPtr<SWidget>  MouseCapture;
        TSharedPtr<SWidget>  FocusTarget;
    };
}
