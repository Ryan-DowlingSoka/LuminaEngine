#pragma once

#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Events/KeyCodes.h"
#include "Events/MouseCodes.h"

namespace Lumina
{
    enum class EInputBindingType : uint8
    {
        Key,
        MouseButton,
        Axis1D,
    };

    struct FInputBinding
    {
        EInputBindingType Type = EInputBindingType::Key;

        // EKey::Num / EMouseKey::Num is the unbound sentinel.
        EKey      Key         = EKey::Num;
        EMouseKey MouseButton = EMouseKey::Num;

        EKey  AxisPositive = EKey::Num;
        EKey  AxisNegative = EKey::Num;
        float AxisScale    = 1.0f;

        bool bRequireCtrl  = false;
        bool bRequireShift = false;
        bool bRequireAlt   = false;
    };

    struct FInputAction
    {
        FName                  Name;
        TVector<FInputBinding> Bindings;
        // True keeps the action firing while the active context is EInputMode::UI.
        bool                   bRunsInUI = false;
    };
}
