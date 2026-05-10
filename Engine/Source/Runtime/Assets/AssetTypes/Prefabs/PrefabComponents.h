#pragma once

#include "Containers/Name.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Core/Object/ObjectMacros.h"
#include "PrefabComponents.generated.h"

namespace Lumina
{
    class CPrefab;

    /** Placed on every prefab-registry entity; StableID survives save/load to pair entities with instances. */
    REFLECT(Component, HideInComponentList)
    struct RUNTIME_API SPrefabComponent
    {
        GENERATED_BODY()

        PROPERTY(ReadOnly)
        FName StableID;
    };

    /** Marks a world entity as a prefab instance; refreshed from the source prefab on world load. */
    REFLECT(Component, Category = "Prefab")
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
