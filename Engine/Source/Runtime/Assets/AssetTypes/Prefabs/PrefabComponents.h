#pragma once

#include "Containers/Name.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Core/Object/ObjectMacros.h"
#include "PrefabComponents.generated.h"

namespace Lumina
{
    class CPrefab;

    /**
     * Placed on every entity that belongs to a CPrefab's registry.
     * StableID is unique within the prefab and survives save/load, letting us pair prefab entities
     * with their world instances so prefab edits can propagate to already-placed instances.
     */
    REFLECT(Component, HideInComponentList)
    struct RUNTIME_API SPrefabComponent
    {
        GENERATED_BODY()

        PROPERTY(ReadOnly)
        FName StableID;
    };

    /**
     * Marks an entity in a world as an instance of a prefab.
     * On world post-load (and whenever a prefab changes) we walk these and copy fresh component data
     * from the source prefab so live instances stay in sync with the asset.
     */
    REFLECT(Component)
    struct RUNTIME_API SPrefabInstanceComponent
    {
        GENERATED_BODY()

        PROPERTY(ReadOnly, Category = "Prefab")
        TObjectPtr<CPrefab> SourcePrefab;

        PROPERTY(ReadOnly, Category = "Prefab")
        FName StableID;

        PROPERTY(ReadOnly, Category = "Prefab")
        bool bIsRoot = false;
    };
}
