#pragma once

#include "Core/Object/ObjectMacros.h"
#include "ScriptExports.h"
#include "ScriptPropertyOverrides.generated.h"

namespace Lumina
{
    /**
     * Reflected wrapper so SScriptComponent can expose per-instance override
     * storage to editor + serializer. The element vector is intentionally NOT
     * a reflected PROPERTY; FScriptPropertyValue is a variant, so the wrapper
     * provides its own Serialize() which MakeStructOps<T> picks up via
     * Concepts::THasSerialize and routes through CStruct::SerializeTaggedProperties.
     */
    REFLECT()
    struct RUNTIME_API FScriptPropertyOverrides
    {
        GENERATED_BODY()

        TVector<Lua::FScriptPropertyEntry> Items;

        bool Serialize(FArchive& Ar);
    };
}
