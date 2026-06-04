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

    // Server-only, transient (non-reflected). Last-sent serialized bytes of each PROPERTY(Replicated) field,
    // keyed by replicated component wire-index -> per-field bytes. Drives native field-granular diffing in
    // WriteEntityComponents so an unchanged component field isn't resent. Auto-removed with the entity by entt;
    // re-seeded on the spawn baseline. Mirrors FScriptRepState for native reflected components.
    struct FComponentRepState
    {
        THashMap<uint32, TVector<TVector<uint8>>> LastSent;

        // Server game-clock time (seconds) of the last PropertyUpdate sent for this entity. Drives
        // oldest-first scheduling in ReplicateDirtyProperties so a per-tick byte budget never starves an entity.
        double LastReplicatedTime = 0.0;
    };
}
