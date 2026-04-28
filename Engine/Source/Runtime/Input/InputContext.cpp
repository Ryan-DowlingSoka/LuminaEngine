#include "pch.h"
#include "InputContext.h"

#include "Events/Event.h"
#include "Input/InputActionMap.h"

namespace Lumina
{
    void FInputContext::SetMouseMode(EMouseMode Mode)
    {
        MouseMode = Mode;
    }

    void FInputContext::SetWindowRect(int MinX, int MinY, int MaxX, int MaxY)
    {
        RectMinX = MinX;
        RectMinY = MinY;
        RectMaxX = MaxX;
        RectMaxY = MaxY;
    }

    bool FInputContext::ContainsWindowPoint(int WindowX, int WindowY) const
    {
        return WindowX >= RectMinX && WindowX < RectMaxX
            && WindowY >= RectMinY && WindowY < RectMaxY;
    }

    void FInputContext::SetRenderTargetSize(uint32 W, uint32 H)
    {
        RTWidth = W;
        RTHeight = H;
    }

    bool FInputContext::WindowToContext(double WindowX, double WindowY, double& OutX, double& OutY) const
    {
        const double PanelW = double(RectMaxX - RectMinX);
        const double PanelH = double(RectMaxY - RectMinY);
        if (PanelW <= 0.0 || PanelH <= 0.0)
        {
            OutX = WindowX;
            OutY = WindowY;
            return false;
        }

        const double U = (WindowX - double(RectMinX)) / PanelW;
        const double V = (WindowY - double(RectMinY)) / PanelH;

        if (RTWidth > 0 && RTHeight > 0)
        {
            OutX = U * double(RTWidth);
            OutY = V * double(RTHeight);
        }
        else
        {
            OutX = WindowX - double(RectMinX);
            OutY = WindowY - double(RectMinY);
        }

        return U >= 0.0 && U <= 1.0 && V >= 0.0 && V <= 1.0;
    }

    bool FInputContext::OnEvent(FEvent& Event)
    {
        if (Event.IsA<FMouseMovedEvent>())
        {
            FMouseMovedEvent& MouseEvent = Event.As<FMouseMovedEvent>();
            // Deltas stay in window pixels; position is translated to RT space.
            MouseDeltaX += MouseEvent.GetDeltaX();
            MouseDeltaY += MouseEvent.GetDeltaY();

            double Cx = 0.0;
            double Cy = 0.0;
            WindowToContext(MouseEvent.GetX(), MouseEvent.GetY(), Cx, Cy);
            MouseX = Cx;
            MouseY = Cy;
        }
        else if (Event.IsA<FMouseButtonPressedEvent>())
        {
            FMouseButtonEvent& MouseButtonEvent = Event.As<FMouseButtonPressedEvent>();
            const uint32 MouseCode = (uint32)MouseButtonEvent.GetButton();
            MouseStates[MouseCode]        = Input::EMouseState::Pressed;
            MouseKeyDownTimes[MouseCode]  = 0.0f;
        }
        else if (Event.IsA<FMouseButtonReleasedEvent>())
        {
            FMouseButtonEvent& MouseButtonEvent = Event.As<FMouseButtonReleasedEvent>();
            const uint32 MouseCode = (uint32)MouseButtonEvent.GetButton();
            MouseStates[MouseCode]        = Input::EMouseState::Released;
            MouseKeyDownTimes[MouseCode]  = -1.0f;
        }
        else if (Event.IsA<FKeyPressedEvent>())
        {
            FKeyPressedEvent& KeyEvent = Event.As<FKeyPressedEvent>();
            const uint32 KeyCode = (uint32)KeyEvent.GetKeyCode();
            KeyStates[KeyCode] = KeyEvent.IsRepeat()
                ? Input::EKeyState::Repeated
                : Input::EKeyState::Pressed;
        }
        else if (Event.IsA<FKeyReleasedEvent>())
        {
            FKeyReleasedEvent& KeyEvent = Event.As<FKeyReleasedEvent>();
            const uint32 KeyCode = (uint32)KeyEvent.GetKeyCode();
            KeyStates[KeyCode] = Input::EKeyState::Released;
        }
        else if (Event.IsA<FMouseScrolledEvent>())
        {
            FMouseScrolledEvent& MouseEvent = Event.As<FMouseScrolledEvent>();
            MouseZ = MouseEvent.GetOffset();
            const uint32 KeyCode = (uint32)MouseEvent.GetCode();
            MouseStates[KeyCode] = MouseZ > 0.0
                ? Input::EMouseState::Up
                : Input::EMouseState::Held;
        }

        return false;
    }

    void FInputContext::EndFrame(double DeltaSeconds)
    {
        for (auto& State : MouseStates)
        {
            if (State == Input::EMouseState::Pressed)  State = Input::EMouseState::Held;
            if (State == Input::EMouseState::Released) State = Input::EMouseState::Up;
        }

        for (auto& State : KeyStates)
        {
            if (State == Input::EKeyState::Pressed)  State = Input::EKeyState::Held;
            if (State == Input::EKeyState::Released) State = Input::EKeyState::Up;
        }

        for (uint32 i = 0; i < (uint32)EMouseKey::Num; ++i)
        {
            if (MouseKeyDownTimes[i] >= 0.0f)
            {
                MouseKeyDownTimes[i] += (float)DeltaSeconds;
            }
        }

        MouseDeltaX = 0.0;
        MouseDeltaY = 0.0;
        MouseZ      = 0.0;
    }

    void FInputContext::ResetState()
    {
        for (auto& S : KeyStates)   S = Input::EKeyState::Up;
        for (auto& S : MouseStates) S = Input::EMouseState::Up;
        for (auto& T : MouseKeyDownTimes) T = -1.0f;
        MouseDeltaX = 0.0;
        MouseDeltaY = 0.0;
        MouseZ      = 0.0;
        CachedModifierState = 0;
        // Without this a focus regain fires a spurious Released callback.
        ActionDownLastFrame.clear();
    }

    bool FInputContext::WasActionDownLastFrame(FName ActionName) const
    {
        const auto It = ActionDownLastFrame.find(ActionName);
        return It != ActionDownLastFrame.end() ? It->second : false;
    }

    void FInputContext::SetActionDownLastFrame(FName ActionName, bool bDown)
    {
        ActionDownLastFrame[ActionName] = bDown;
    }

    uint64 FInputContext::RegisterActionCallback(FName ActionName, EActionTrigger Trigger, Lua::FRef Function)
    {
        FActionCallback Cb;
        Cb.ActionName = ActionName;
        Cb.Trigger    = Trigger;
        Cb.Function   = std::move(Function);
        Cb.Id         = NextCallbackId++;
        const uint64 Id = Cb.Id;
        ActionCallbacks.push_back(std::move(Cb));
        return Id;
    }

    void FInputContext::UnregisterActionCallback(uint64 Id)
    {
        for (auto It = ActionCallbacks.begin(); It != ActionCallbacks.end(); ++It)
        {
            if (It->Id == Id)
            {
                ActionCallbacks.erase(It);
                return;
            }
        }
    }

    void FInputContext::ClearActionCallbacks()
    {
        ActionCallbacks.clear();
    }

    void FInputContext::DispatchActionCallbacks()
    {
        if (ActionCallbacks.empty())
        {
            return;
        }

        // Callbacks may unregister themselves; iterate a snapshot.
        TVector<FActionCallback*> Snapshot;
        Snapshot.reserve(ActionCallbacks.size());
        for (FActionCallback& Cb : ActionCallbacks)
        {
            Snapshot.push_back(&Cb);
        }

        const FInputActionMap& Map = FInputActionMap::Get();
        for (FActionCallback* Cb : Snapshot)
        {
            const bool bShouldFire = (Cb->Trigger == EActionTrigger::Pressed)
                ? Map.IsActionPressed (Cb->ActionName, *this)
                : Map.IsActionReleased(Cb->ActionName, *this);
            if (bShouldFire && Cb->Function.IsInvokable())
            {
                Cb->Function();
            }
        }
    }

    void FInputContext::UpdateActionEdgeState()
    {
        const FInputActionMap& Map = FInputActionMap::Get();
        for (const FInputAction& Action : Map.GetAllActions())
        {
            const bool bDownNow = Map.IsActionDown(Action.Name, *this);
            ActionDownLastFrame[Action.Name] = bDownNow;
        }
    }
}
