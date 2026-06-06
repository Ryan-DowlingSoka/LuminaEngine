#pragma once

#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Core/Object/ObjectMacros.h"
#include "Input/InputActionMap.h"
#include "Input/InputContext.h"
#include "Input/InputProcessor.h"
#include "Input/InputViewport.h"
#include "Events/KeyCodes.h"
#include "Events/MouseCodes.h"
#include "InputComponent.generated.h"

namespace Lumina
{
    // Map a friendly Lua key name ("W", "Space", "Shift", "Ctrl") to an EKey. Letters are ASCII == EKey.
    inline bool KeyNameToEKey(const FName& Name, EKey& Out)
    {
        const char* S = Name.c_str();
        if (S == nullptr || S[0] == '\0') { return false; }
        if (S[1] == '\0')
        {
            char C = S[0];
            if (C >= 'a' && C <= 'z') { C = static_cast<char>(C - 'a' + 'A'); }
            if (C >= 'A' && C <= 'Z') { Out = static_cast<EKey>(static_cast<uint16>(C)); return true; }
            if (C >= '0' && C <= '9') { Out = static_cast<EKey>(static_cast<uint16>(C)); return true; }
            if (C == ' ')             { Out = EKey::Space; return true; }
        }
        if (strcmp(S, "Space") == 0)                                    { Out = EKey::Space;       return true; }
        if (strcmp(S, "Shift") == 0 || strcmp(S, "LeftShift") == 0)     { Out = EKey::LeftShift;   return true; }
        if (strcmp(S, "Ctrl") == 0  || strcmp(S, "LeftControl") == 0)   { Out = EKey::LeftControl; return true; }
        return false;
    }

    REFLECT(Component, Category = "Gameplay")
    struct RUNTIME_API SInputComponent
    {
        GENERATED_BODY()

        /** When false, every query returns its safe default (false / 0). */
        PROPERTY(Editable)
        bool bEnabled = true;

        /** Reserved for future split-screen routing; currently unused. */
        PROPERTY(Editable)
        int32 PlayerIndex = 0;

        /** IDs of action callbacks this component bound, for teardown. */
        TVector<uint64> ActionCallbackIds;

        // Owning world (runtime, not serialized). Set by CWorld so input queries resolve to THIS world's
        // input viewport rather than the single global "active" one -- lets each PIE world be driven
        // independently (focus a viewport, control that world).
        CWorld* World = nullptr;

        //~ Per-frame input snapshot, written by SInputSystem before gameplay/scripts run. Queries below read
        //  this data -- no globals at call time -- so input behaves identically in editor and shipping and is
        //  the per-entity command we can later serialize for networked play. Not reflected (transient).
        bool   bReceivingInput = false;
        double SnapMouseX = 0.0;
        double SnapMouseY = 0.0;
        double SnapMouseZ = 0.0;
        double SnapMouseDeltaX = 0.0;
        double SnapMouseDeltaY = 0.0;
        TArray<Input::EKeyState,   (uint32)EKey::Num>      KeyStates   = {};
        TArray<Input::EMouseState, (uint32)EMouseKey::Num> MouseStates = {};

        // SInputSystem fills the snapshot from this world's OWN context once per frame.
        void SnapshotFrom(const FInputContext& Ctx, bool bReceiving)
        {
            bReceivingInput = bReceiving;
            if (!bReceiving)
            {
                ResetSnapshot();
                return;
            }
            for (uint32 i = 0; i < (uint32)EKey::Num; ++i)      { KeyStates[i]   = Ctx.GetKeyState((EKey)i); }
            for (uint32 i = 0; i < (uint32)EMouseKey::Num; ++i) { MouseStates[i] = Ctx.GetMouseButtonState((EMouseKey)i); }
            SnapMouseX      = Ctx.GetMouseX();
            SnapMouseY      = Ctx.GetMouseY();
            SnapMouseZ      = Ctx.GetMouseZ();
            SnapMouseDeltaX = Ctx.GetMouseDeltaX();
            SnapMouseDeltaY = Ctx.GetMouseDeltaY();
        }

        void ResetSnapshot()
        {
            bReceivingInput = false;
            for (auto& S : KeyStates)   { S = Input::EKeyState::Up; }
            for (auto& S : MouseStates) { S = Input::EMouseState::Up; }
            SnapMouseX = SnapMouseY = SnapMouseZ = SnapMouseDeltaX = SnapMouseDeltaY = 0.0;
        }

        //~ Actions stay live + per-world (gated per-action by the action map); they were already correct.

        FUNCTION(Script)
        bool IsActionDown(const FName& Name) const
        {
            if (!bEnabled)
            {
                return false;
            }
            const FInputViewport* V = FInputViewportRegistry::Get().FindViewportForWorld(World);
            if (V == nullptr)
            {
                return false;
            }
            return FInputActionMap::Get().IsActionDown(Name, V->GetContext());
        }

        FUNCTION(Script)
        bool IsActionPressed(const FName& Name) const
        {
            if (!bEnabled)
            {
                return false;
            }
            const FInputViewport* V = FInputViewportRegistry::Get().FindViewportForWorld(World);
            if (V == nullptr)
            {
                return false;
            }
            return FInputActionMap::Get().IsActionPressed(Name, V->GetContext());
        }

        FUNCTION(Script)
        bool IsActionReleased(const FName& Name) const
        {
            if (!bEnabled)
            {
                return false;
            }
            const FInputViewport* V = FInputViewportRegistry::Get().FindViewportForWorld(World);
            if (V == nullptr)
            {
                return false;
            }
            return FInputActionMap::Get().IsActionReleased(Name, V->GetContext());
        }

        FUNCTION(Script)
        float GetActionAxis(const FName& Name) const
        {
            if (!bEnabled)
            {
                return 0.0f;
            }
            const FInputViewport* V = FInputViewportRegistry::Get().FindViewportForWorld(World);
            if (V == nullptr)
            {
                return 0.0f;
            }
            return FInputActionMap::Get().GetActionAxis(Name, V->GetContext());
        }

        //~ Raw key/mouse state -- read straight from the snapshot, true only when this world is receiving input.

        // True when THIS component's world owns the focused input viewport (so scripts can gate mouse-look
        // etc. to the active window in multi-world PIE; raw keys already return false off the focused world).
        FUNCTION(Script)
        bool IsInputActive() const { return bEnabled && bReceivingInput; }

        // Raw key state (e.g. "W", "Space", "Shift"). Only the FOCUSED viewport's world reports keys, so
        // multiple PIE worlds don't all move at once. For bound gameplay actions prefer IsActionDown.
        FUNCTION(Script)
        bool IsKeyDown(const FName& KeyName) const
        {
            if (!bEnabled)
            {
                return false;
            }
            EKey Key;
            if (!KeyNameToEKey(KeyName, Key)) { return false; }
            const Input::EKeyState S = KeyStates[(uint32)Key];
            return S == Input::EKeyState::Pressed || S == Input::EKeyState::Held || S == Input::EKeyState::Repeated;
        }

        FUNCTION(Script)
        bool IsKeyPressed(const FName& KeyName) const
        {
            if (!bEnabled)
            {
                return false;
            }
            EKey Key;
            return KeyNameToEKey(KeyName, Key) && KeyStates[(uint32)Key] == Input::EKeyState::Pressed;
        }

        FUNCTION(Script)
        bool IsKeyReleased(const FName& KeyName) const
        {
            if (!bEnabled)
            {
                return false;
            }
            EKey Key;
            return KeyNameToEKey(KeyName, Key) && KeyStates[(uint32)Key] == Input::EKeyState::Released;
        }

        FUNCTION(Script)
        double GetMouseX()      const { return bEnabled ? SnapMouseX      : 0.0; }

        FUNCTION(Script)
        double GetMouseY()      const { return bEnabled ? SnapMouseY      : 0.0; }

        FUNCTION(Script)
        double GetMouseZ()      const { return bEnabled ? SnapMouseZ      : 0.0; }

        FUNCTION(Script)
        double GetMouseDeltaX() const { return bEnabled ? SnapMouseDeltaX : 0.0; }

        FUNCTION(Script)
        double GetMouseDeltaY() const { return bEnabled ? SnapMouseDeltaY : 0.0; }
    };
}
