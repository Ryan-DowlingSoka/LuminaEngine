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

        /** Re-applies prefab data to one instance, matched by SPrefabInstanceComponent::StableID.
         *  Diff semantics: entities/components present in the prefab but missing on the instance are
         *  added, and entities/components present on the instance but missing from the prefab are
         *  destroyed/removed. The root's world transform is preserved (placed location wins). */
        void RefreshInstance(CWorld* World, entt::entity InstanceRoot);

        /** Refreshes every prefab instance in World; called from CWorld::InitializeWorld
         *  immediately after the loaded registry is swapped in. */
        static void RefreshAllInstancesInWorld(CWorld* World);

        /** Strip SPrefabInstanceComponent from the instance subtree so the entities become plain,
         *  user-editable entities no longer paired with this prefab. Returns false if InstanceRoot
         *  is not a prefab instance root sourced from this prefab. */
        static bool DetachInstance(CWorld* World, entt::entity InstanceRoot);

        /** Replaces this prefab with a deep copy of RootEntity and descendants from SourceWorld. */
        void CaptureFromWorld(CWorld* SourceWorld, entt::entity RootEntity);

        /**
         * Deep-copies entities from Source into Dest; OutMap holds src->dest ids. Non-reflected
         * storages are skipped; FRelationshipComponent links and reflected entity-handle properties
         * (PROPERTY meta "Entity") are remapped, with references escaping the copied set cleared to
         * null. When SourceEntities is non-null only those entities are copied (otherwise the whole
         * registry). ExtraSkipStorage skips additional component types by id - e.g. the editor-only
         * set when copying out of a live editor world.
         */
        static void CopyRegistry(entt::registry& Source, entt::registry& Dest, THashMap<entt::entity, entt::entity>& OutMap,
            const TVector<entt::entity>* SourceEntities = nullptr,
            bool(*ExtraSkipStorage)(entt::id_type) = nullptr);

        entt::registry Registry;
    };
}
