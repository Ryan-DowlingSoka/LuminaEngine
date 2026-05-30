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

        /** Diff-applies prefab data to one instance (matched by StableID): adds missing, removes
         *  extra. Root's world transform is preserved (placed location wins). */
        void RefreshInstance(CWorld* World, entt::entity InstanceRoot);

        /** Refreshes every prefab instance in World; called from CWorld::InitializeWorld after registry swap. */
        static void RefreshAllInstancesInWorld(CWorld* World);

        /** Re-syncs every live instance of THIS prefab across all loaded worlds (GWorldManager contexts).
         *  Call after the prefab's data changes (e.g. on save) so open levels reflect the edit immediately. */
        void RefreshInstancesInLoadedWorlds();

        /** Strips SPrefabInstanceComponent from the subtree, unpairing it from this prefab.
         *  Returns false if InstanceRoot is not an instance root sourced from this prefab. */
        static bool DetachInstance(CWorld* World, entt::entity InstanceRoot);

        /** Replaces this prefab with a deep copy of RootEntity and descendants from SourceWorld. */
        void CaptureFromWorld(CWorld* SourceWorld, entt::entity RootEntity);

        /** Deep-copies entities Source->Dest (OutMap = src->dest ids); remaps relationships + entity-handle
         *  props (escaping refs cleared). SourceEntities limits the set; ExtraSkipStorage skips more types by id. */
        static void CopyRegistry(entt::registry& Source, entt::registry& Dest, THashMap<entt::entity, entt::entity>& OutMap,
            const TVector<entt::entity>* SourceEntities = nullptr,
            bool(*ExtraSkipStorage)(entt::id_type) = nullptr);

        entt::registry Registry;
    };
}
