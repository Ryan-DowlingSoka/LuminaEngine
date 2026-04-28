#pragma once

#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Input/InputAction.h"

namespace Lumina
{
    class FInputContext;

    class FInputActionMap
    {
    public:

        RUNTIME_API static FInputActionMap& Get();

        RUNTIME_API bool LoadFromConfig();

        RUNTIME_API void RegisterAction(FInputAction Action);
        RUNTIME_API void UnregisterAction(FName Name);
        RUNTIME_API void Clear();

        RUNTIME_API const FInputAction* FindAction(FName Name) const;

        RUNTIME_API bool  IsActionDown    (FName Name, const FInputContext& Context) const;
        RUNTIME_API bool  IsActionPressed (FName Name, const FInputContext& Context) const;
        RUNTIME_API bool  IsActionReleased(FName Name, const FInputContext& Context) const;
        RUNTIME_API float GetActionAxis   (FName Name, const FInputContext& Context) const;

        const TVector<FInputAction>& GetAllActions() const { return Actions; }
        TVector<FInputAction>&       GetAllActionsMutable() { return Actions; }

        RUNTIME_API bool SaveToProjectConfig() const;
        RUNTIME_API void SetActions(TVector<FInputAction> NewActions);

        RUNTIME_API static FString  KeyToString(EKey Key);
        RUNTIME_API static FString  MouseButtonToString(EMouseKey Button);
        RUNTIME_API static EKey     KeyFromString(FStringView Token);
        RUNTIME_API static EMouseKey MouseButtonFromString(FStringView Token);
        RUNTIME_API static const TVector<EKey>&      AllSupportedKeys();
        RUNTIME_API static const TVector<EMouseKey>& AllSupportedMouseButtons();

    private:

        bool  PassesUIGate(const FInputAction& Action, const FInputContext& Context) const;
        bool  EvaluateDown(const FInputAction& Action, const FInputContext& Context) const;
        float EvaluateAxis(const FInputAction& Action, const FInputContext& Context) const;

        TVector<FInputAction> Actions;
    };
}
