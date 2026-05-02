#pragma once
#include "Core/Object/ObjectMacros.h"
#include "ScriptPath.generated.h"

namespace Lumina
{
    REFLECT()
    struct RUNTIME_API FScriptPath
    {
        GENERATED_BODY()
        
        PROPERTY(Editable)
        FString Path;
    };
}
