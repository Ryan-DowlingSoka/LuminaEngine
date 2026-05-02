#pragma once

#include "Core/Object/ObjectMacros.h"
#include "ScriptExports.h"
#include "ScriptPropertyOverrides.generated.h"

namespace Lumina
{
    // Reflected wrapper for per-instance overrides. Items is not a PROPERTY because
    // FScriptPropertyValue is a variant; custom Serialize is picked up via THasSerialize.
    REFLECT()
    struct RUNTIME_API FScriptPropertyOverrides
    {
        GENERATED_BODY()

        TVector<Lua::FScriptPropertyEntry> Items;

        bool Serialize(FArchive& Ar);
    };
}
