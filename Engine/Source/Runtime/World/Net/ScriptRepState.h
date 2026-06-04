#pragma once

#include "Platform/GenericPlatform.h"
#include "Containers/Array.h"

namespace Lumina
{
    // Server-only, transient (non-reflected, never serialized). Holds the last-sent serialized bytes of each
    // --@replicated script field on this entity, indexed by its wire rep-index. Drives field-granular diffing
    // in ReplicateDirtyProperties so an unchanged field (including unchanged nested tables) isn't resent.
    // Auto-removed with the entity by entt; resized when the script's ReplicatedFields count changes.
    struct FScriptRepState
    {
        TVector<TVector<uint8>> LastSent;
    };
}
