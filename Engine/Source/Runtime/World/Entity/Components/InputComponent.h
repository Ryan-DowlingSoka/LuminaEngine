#pragma once

#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Core/Object/ObjectMacros.h"
#include "Input/InputActionMap.h"
#include "Input/InputContext.h"
#include "Input/InputProcessor.h"
#include "Input/InputViewport.h"
#include "InputComponent.generated.h"

namespace Lumina
{
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

        FUNCTION(Script)
        bool IsActionDown(const FName& Name) const
        {
            if (!bEnabled) return false;
            const FInputViewport* V = FInputViewportRegistry::Get().GetActiveViewport();
            if (V == nullptr) return false;
            return FInputActionMap::Get().IsActionDown(Name, V->GetContext());
        }

        FUNCTION(Script)
        bool IsActionPressed(const FName& Name) const
        {
            if (!bEnabled) return false;
            const FInputViewport* V = FInputViewportRegistry::Get().GetActiveViewport();
            if (V == nullptr) return false;
            return FInputActionMap::Get().IsActionPressed(Name, V->GetContext());
        }

        FUNCTION(Script)
        bool IsActionReleased(const FName& Name) const
        {
            if (!bEnabled) return false;
            const FInputViewport* V = FInputViewportRegistry::Get().GetActiveViewport();
            if (V == nullptr) return false;
            return FInputActionMap::Get().IsActionReleased(Name, V->GetContext());
        }

        FUNCTION(Script)
        float GetActionAxis(const FName& Name) const
        {
            if (!bEnabled) return 0.0f;
            const FInputViewport* V = FInputViewportRegistry::Get().GetActiveViewport();
            if (V == nullptr) return 0.0f;
            return FInputActionMap::Get().GetActionAxis(Name, V->GetContext());
        }

        FUNCTION(Script) 
        double GetMouseX()      const { return bEnabled ? FInputProcessor::Get().GetMouseX()      : 0.0; }
        
        FUNCTION(Script) 
        double GetMouseY()      const { return bEnabled ? FInputProcessor::Get().GetMouseY()      : 0.0; }
        
        FUNCTION(Script) 
        double GetMouseZ()      const { return bEnabled ? FInputProcessor::Get().GetMouseZ()      : 0.0; }
        
        FUNCTION(Script)
        double GetMouseDeltaX() const { return bEnabled ? FInputProcessor::Get().GetMouseDeltaX() : 0.0; }
        
        FUNCTION(Script) 
        double GetMouseDeltaY() const { return bEnabled ? FInputProcessor::Get().GetMouseDeltaY() : 0.0; }
    };
}
