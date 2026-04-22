#pragma once
#include "Containers/Name.h"
#include "Core/Object/Object.h"
#include "Core/Math/Transform.h"
#include "Prefab.generated.h"

namespace Lumina
{
    class CWorld;

    REFLECT()
    class RUNTIME_API CPrefab : public CObject
    {
        GENERATED_BODY()
    public:

        void Serialize(FArchive& Ar) override;

        /**
         * Instantiates the prefab's entities into TargetWorld.
         * Returns the root entity of the new instance hierarchy (entt::null on failure).
         * Parent, if valid, is used as the parent of the new root entity.
         * OffsetTransform is applied to the root entity's local transform after instantiation.
         */
        entt::entity Instantiate(CWorld* TargetWorld, const FTransform& OffsetTransform = FTransform(), entt::entity Parent = entt::null);

        /**
         * Re-applies this prefab's component data to a single instance (matched by SPrefabInstanceComponent::StableID).
         * Overrides existing component data on the instance but does not remove instance-only components.
         */
        void RefreshInstance(CWorld* World, entt::entity InstanceRoot);

        /**
         * Walks the world for SPrefabInstanceComponent roots and refreshes each one from its source prefab.
         * Called during CWorld::PostLoad so prefab changes propagate automatically on load.
         */
        static void RefreshAllInstancesInWorld(CWorld* World);

        /**
         * Replaces this prefab's contents with a deep copy of RootEntity and its descendants from SourceWorld.
         * Each captured entity is tagged with a fresh SPrefabComponent::StableID.
         */
        void CaptureFromWorld(CWorld* SourceWorld, entt::entity RootEntity);

        /**
         * Deep copies Source's entities into Dest. Fills OutMap with source->dest entity id mapping.
         * Handles relationship fixup and reflected components; non-reflected storages are skipped.
         */
        static void CopyRegistry(entt::registry& Source, entt::registry& Dest, THashMap<entt::entity, entt::entity>& OutMap);

        entt::registry Registry;
    };
}
