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

        /** Returns root entity of new instance (entt::null on failure). OffsetTransform applied to root. */
        entt::entity Instantiate(CWorld* TargetWorld, const FTransform& OffsetTransform = FTransform(), entt::entity Parent = entt::null);

        /** Re-applies prefab data to one instance, matched by SPrefabInstanceComponent::StableID. */
        void RefreshInstance(CWorld* World, entt::entity InstanceRoot);

        /** Refreshes every prefab instance in World; called from CWorld::PostLoad. */
        static void RefreshAllInstancesInWorld(CWorld* World);

        /** Replaces this prefab with a deep copy of RootEntity and descendants from SourceWorld. */
        void CaptureFromWorld(CWorld* SourceWorld, entt::entity RootEntity);

        /** Deep-copies entities; OutMap holds src->dest entity ids. Non-reflected storages are skipped. */
        static void CopyRegistry(entt::registry& Source, entt::registry& Dest, THashMap<entt::entity, entt::entity>& OutMap);

        entt::registry Registry;
    };
}
