#pragma once

#include "InputComponent.generated.h"

namespace Lumina
{
    REFLECT(Component, Category = "Gameplay")
    struct RUNTIME_API SInputComponent
    {
        GENERATED_BODY()
        
        // @TODO Can't have an empty component be reflected rn.
        uint8 bFoobar:1;
    };
}
