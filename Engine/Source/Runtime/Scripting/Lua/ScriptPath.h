#pragma once
#include "Core/Object/ObjectMacros.h"
#include "ScriptPath.generated.h"

namespace Lumina
{
    REFLECT()
    struct RUNTIME_API FScriptPath
    {
        GENERATED_BODY()
        
        /** Relative path to the Luau script file within the project. */
        PROPERTY(Editable)
        FString Path;
    };
}
