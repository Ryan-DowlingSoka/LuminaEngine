#include "pch.h"
#include "Prefab.h"
#include "PrefabComponents.h"

#include "Core/Object/Package/Package.h"
#include "GUID/GUID.h"
#include "World/Entity/Components/DirtyComponent.h"
#include "World/Entity/Components/EditorComponent.h"
#include "World/Entity/Components/NameComponent.h"
#include "World/Entity/Components/PhysicsComponent.h"
#include "World/Entity/Components/RelationshipComponent.h"
#include "World/Entity/Components/TransformComponent.h"
#include "World/Entity/EntityUtils.h"
#include "World/World.h"

namespace Lumina
{
    namespace
    {
        /** Component types skipped by the cross-registry copy pass. */
        bool IsNonReplicatedStorage(entt::id_type ID)
        {
            // Relationships are remapped manually after the copy pass.
            if (ID == entt::type_hash<FRelationshipComponent>::value()) return true;
            // Editor-only state must not leak between worlds and prefabs.
            if (ID == entt::type_hash<FSelectedInEditorComponent>::value()) return true;
            if (ID == entt::type_hash<FHideInSceneOutliner>::value()) return true;
            if (ID == entt::type_hash<FEditorComponent>::value()) return true;
            return false;
        }

        // Runtime-only / spawn-tagging components a refresh diff must never touch when
        // deciding "instance has X, prefab doesn't, therefore remove X". These live on
        // the instance for legitimate runtime reasons and would re-spawn next frame.
        bool IsRuntimeOnlyComponent(entt::id_type ID)
        {
            if (ID == entt::type_hash<SPrefabInstanceComponent>::value()) return true;
            if (ID == entt::type_hash<SRigidBodyComponent>::value())      return true;
            if (ID == entt::type_hash<FNeedsTransformUpdate>::value())    return true;
            if (ID == entt::type_hash<FNeedsPhysicsBodyUpdate>::value())  return true;
            return false;
        }

        FName GenerateStableID()
        {
            return FName(FGuid::New().ToShortString());
        }

        // CopyRegistry extra-skip used when capturing out of a live world: a captured nested
        // instance's tracking component must not leak into the new prefab (it gets fresh
        // SPrefabComponent tags instead).
        bool ShouldSkipInstanceComponent(entt::id_type ID)
        {
            return ID == entt::type_hash<SPrefabInstanceComponent>::value();
        }

        // Tag the spawn root and every descendant so the transform system reconciles their
        // world matrices this frame. Without this, freshly spawned entities can render at
        // stale world positions for one frame.
        void MarkSubtreeTransformsDirty(entt::registry& Registry, entt::entity Root)
        {
            if (!Registry.valid(Root))
            {
                return;
            }
            Registry.emplace_or_replace<FNeedsTransformUpdate>(Root);
            ECS::Utils::ForEachDescendant(Registry, Root, [&](entt::entity Desc)
            {
                Registry.emplace_or_replace<FNeedsTransformUpdate>(Desc);
            });
        }
    }

    void CPrefab::Serialize(FArchive& Ar)
    {
        CObject::Serialize(Ar);
        ECS::Utils::SerializeRegistry(Ar, Registry);
    }

    void CPrefab::CopyRegistry(entt::registry& Source, entt::registry& Dest, THashMap<entt::entity, entt::entity>& OutMap,
        const TVector<entt::entity>* SourceEntities, bool(*ExtraSkipStorage)(entt::id_type))
    {
        using namespace entt::literals;

        if (SourceEntities != nullptr)
        {
            for (entt::entity SrcE : *SourceEntities)
            {
                if (Source.valid(SrcE))
                {
                    OutMap[SrcE] = Dest.create();
                }
            }
        }
        else
        {
            Source.view<entt::entity>().each([&](entt::entity SrcE)
            {
                OutMap[SrcE] = Dest.create();
            });
        }

        for (auto&& [ID, SrcSet] : Source.storage())
        {
            // Rigid bodies carry a runtime Jolt BodyID that must not be copied; handled below.
            if (IsNonReplicatedStorage(ID)
                || ID == entt::type_hash<SRigidBodyComponent>::value()
                || (ExtraSkipStorage != nullptr && ExtraSkipStorage(ID)))
            {
                continue;
            }

            entt::meta_type MetaType = entt::resolve(SrcSet.info());
            if (!MetaType)
            {
                continue;
            }

            for (entt::entity SrcE : SrcSet)
            {
                auto It = OutMap.find(SrcE);
                if (It == OutMap.end())
                {
                    continue;
                }

                entt::entity DestE = It->second;
                void* SrcCompPtr = SrcSet.value(SrcE);

                entt::meta_any SrcAny = MetaType.from_void(SrcCompPtr);
                ECS::Utils::InvokeMetaFunc(MetaType, "emplace"_hs,
                    entt::forward_as_meta(Dest), DestE, entt::forward_as_meta(SrcAny));
            }
        }

        // Rigid bodies own a runtime Jolt BodyID; carrying it to a copy makes the physics scene
        // think the body already exists (TryBuildRigidBodyCreationSettings -> AlreadyExists) and
        // skip creation. Re-emplace with an invalid id so on_construct builds a fresh body - same
        // contract as CWorld::DuplicateEntity.
        for (auto& [SrcE, DestE] : OutMap)
        {
            if (const SRigidBodyComponent* SrcBody = Source.try_get<SRigidBodyComponent>(SrcE))
            {
                SRigidBodyComponent NewBody = *SrcBody;
                NewBody.BodyID = 0xFFFFFFFFu;
                Dest.emplace_or_replace<SRigidBodyComponent>(DestE, NewBody);
            }
        }

        auto Remap = [&](entt::entity& E)
        {
            if (E != entt::null)
            {
                auto It = OutMap.find(E);
                E = (It != OutMap.end()) ? It->second : entt::null;
            }
        };

        Source.view<FRelationshipComponent>().each([&](entt::entity SrcE, const FRelationshipComponent& SrcRel)
        {
            auto It = OutMap.find(SrcE);
            if (It == OutMap.end())
            {
                return;
            }

            FRelationshipComponent DestRel = SrcRel;
            Remap(DestRel.First);
            Remap(DestRel.Prev);
            Remap(DestRel.Next);
            Remap(DestRel.Parent);
            Dest.emplace_or_replace<FRelationshipComponent>(It->second, DestRel);
        });

        // Remap reflected entity-handle fields onto the copied entities; references escaping the
        // copied set are cleared so a stale id can't alias an unrelated entity in Dest.
        for (auto& [SrcE, DestE] : OutMap)
        {
            ECS::Utils::RemapEntityReferences(Dest, DestE, OutMap, /*bClearUnmapped*/ true);
        }
    }

    entt::entity CPrefab::Instantiate(CWorld* TargetWorld, const FTransform& OffsetTransform, entt::entity Parent)
    {
        if (TargetWorld == nullptr)
        {
            return entt::null;
        }

        entt::registry& WorldRegistry = TargetWorld->GetEntityRegistry();

        // Collect every parentless entity so we can pick a canonical root and rescue stragglers.
        // A well-formed prefab has exactly one; multi-root prefabs are a data error (or an
        // editor regression), but we handle them rather than silently orphaning entities.
        TVector<entt::entity> PrefabRoots;
        PrefabRoots.reserve(2);
        Registry.view<entt::entity>().each([&](entt::entity E)
        {
            const FRelationshipComponent* Rel = Registry.try_get<FRelationshipComponent>(E);
            const bool bHasParent = Rel && Rel->Parent != entt::null;
            if (!bHasParent)
            {
                PrefabRoots.push_back(E);
            }
        });

        if (PrefabRoots.empty())
        {
            return entt::null;
        }

        const entt::entity PrefabRoot = PrefabRoots[0];
        if (PrefabRoots.size() > 1)
        {
            LOG_WARN("Prefab '{}' has {} parentless entities; reparenting extras under the first.",
                     GetName().c_str(), (uint32)PrefabRoots.size());
        }

        THashMap<entt::entity, entt::entity> Map;
        CopyRegistry(Registry, WorldRegistry, Map);

        const entt::entity WorldRoot = Map[PrefabRoot];

        for (auto& [SrcE, DestE] : Map)
        {
            FName StableID;
            if (const SPrefabComponent* PrefabComp = WorldRegistry.try_get<SPrefabComponent>(DestE))
            {
                StableID = PrefabComp->StableID;
            }
            else
            {
                StableID = GenerateStableID();
            }

            WorldRegistry.remove<SPrefabComponent>(DestE);

            SPrefabInstanceComponent& Instance = WorldRegistry.emplace_or_replace<SPrefabInstanceComponent>(DestE);
            Instance.SourcePrefab = this;
            Instance.StableID = StableID;
            Instance.bIsRoot = (DestE == WorldRoot);
        }

        // Rescue any extra parentless entities so the spawn has a single hierarchical root.
        for (size_t i = 1; i < PrefabRoots.size(); ++i)
        {
            const entt::entity Extra = Map[PrefabRoots[i]];
            if (Extra != entt::null && WorldRegistry.valid(Extra))
            {
                ECS::Utils::ReparentEntity(WorldRegistry, Extra, WorldRoot);
            }
        }

        if (STransformComponent* RootTransform = WorldRegistry.try_get<STransformComponent>(WorldRoot))
        {
            RootTransform->SetLocalTransform(OffsetTransform);
        }
        else
        {
            WorldRegistry.emplace<STransformComponent>(WorldRoot, OffsetTransform);
        }

        if (Parent != entt::null && WorldRegistry.valid(Parent))
        {
            ECS::Utils::ReparentEntity(WorldRegistry, WorldRoot, Parent);
        }

        // Without this the transform system can render the spawn at a stale world matrix for one
        // frame (the components were emplaced via meta, which doesn't fire on_update).
        MarkSubtreeTransformsDirty(WorldRegistry, WorldRoot);

        return WorldRoot;
    }

    void CPrefab::RefreshInstance(CWorld* World, entt::entity InstanceRoot)
    {
        using namespace entt::literals;

        if (World == nullptr)
        {
            return;
        }

        entt::registry& WorldRegistry = World->GetEntityRegistry();
        if (!WorldRegistry.valid(InstanceRoot))
        {
            return;
        }

        SPrefabInstanceComponent* RootInstance = WorldRegistry.try_get<SPrefabInstanceComponent>(InstanceRoot);
        if (RootInstance == nullptr || RootInstance->SourcePrefab != this)
        {
            return;
        }

        // ---- 1. Index instance entities by StableID ------------------------
        THashMap<FName, entt::entity> InstanceByStableID;
        InstanceByStableID[RootInstance->StableID] = InstanceRoot;
        ECS::Utils::ForEachDescendant(WorldRegistry, InstanceRoot, [&](entt::entity Descendant)
        {
            if (const SPrefabInstanceComponent* Inst = WorldRegistry.try_get<SPrefabInstanceComponent>(Descendant))
            {
                if (Inst->SourcePrefab == this && !Inst->StableID.IsNone())
                {
                    InstanceByStableID[Inst->StableID] = Descendant;
                }
            }
        });

        // ---- 2. Index prefab entities by StableID -------------------------
        THashMap<FName, entt::entity> PrefabByStableID;
        Registry.view<SPrefabComponent>().each([&](entt::entity PrefabE, const SPrefabComponent& PrefabComp)
        {
            if (!PrefabComp.StableID.IsNone())
            {
                PrefabByStableID[PrefabComp.StableID] = PrefabE;
            }
        });

        // ---- 3. Destroy instance entities whose prefab counterpart is gone ----
        // Don't touch the root: the user placed it, and the prefab can be empty mid-edit.
        TVector<entt::entity> ToDestroy;
        for (auto& [StableID, WorldE] : InstanceByStableID)
        {
            if (WorldE == InstanceRoot)
            {
                continue;
            }
            if (PrefabByStableID.find(StableID) == PrefabByStableID.end())
            {
                ToDestroy.push_back(WorldE);
            }
        }
        for (entt::entity E : ToDestroy)
        {
            const auto It = eastl::find_if(InstanceByStableID.begin(), InstanceByStableID.end(),
                [&](const auto& Pair) { return Pair.second == E; });
            if (It != InstanceByStableID.end())
            {
                InstanceByStableID.erase(It);
            }
            if (WorldRegistry.valid(E))
            {
                ECS::Utils::DestroyEntityHierarchy(WorldRegistry, E);
            }
        }

        // ---- 4. Spawn instance entities for new prefab entries -------------
        for (auto& [StableID, PrefabE] : PrefabByStableID)
        {
            if (InstanceByStableID.find(StableID) != InstanceByStableID.end())
            {
                continue;
            }
            const entt::entity NewE = WorldRegistry.create();
            InstanceByStableID[StableID] = NewE;

            SPrefabInstanceComponent& Inst = WorldRegistry.emplace<SPrefabInstanceComponent>(NewE);
            Inst.SourcePrefab = this;
            Inst.StableID     = StableID;
            Inst.bIsRoot      = false;
        }

        // ---- 5. Prefab-entity -> instance-entity remap table (for entity-handle fields) ----
        THashMap<entt::entity, entt::entity> PrefabToInstance;
        for (auto& [StableID, PrefabE] : PrefabByStableID)
        {
            auto It = InstanceByStableID.find(StableID);
            if (It != InstanceByStableID.end())
            {
                PrefabToInstance[PrefabE] = It->second;
            }
        }

        // ---- 6. Copy/replace components from prefab -> instance, then prune removed ones ---
        Registry.view<SPrefabComponent>().each([&](entt::entity PrefabE, const SPrefabComponent& PrefabComp)
        {
            auto It = InstanceByStableID.find(PrefabComp.StableID);
            if (It == InstanceByStableID.end())
            {
                return;
            }

            entt::entity WorldE = It->second;
            const bool bIsRoot = (WorldE == InstanceRoot);

            // Track which storages the prefab has for this entity so we can remove ones
            // the instance still carries that the prefab dropped.
            THashSet<entt::id_type> PrefabComponentIDs;
            PrefabComponentIDs.reserve(8);

            for (auto&& [ID, PrefabStorage] : Registry.storage())
            {
                if (IsNonReplicatedStorage(ID)) continue;
                if (ID == entt::type_hash<SPrefabComponent>::value()) continue;
                if (!PrefabStorage.contains(PrefabE)) continue;

                PrefabComponentIDs.insert(ID);

                // Preserve placed-root local transform; otherwise the stored prefab-root pose
                // would teleport the instance back to authoring origin.
                if (bIsRoot && ID == entt::type_hash<STransformComponent>::value())
                {
                    continue;
                }

                entt::meta_type MetaType = entt::resolve(PrefabStorage.info());
                if (!MetaType) continue;

                void* SrcCompPtr = PrefabStorage.value(PrefabE);
                entt::meta_any SrcAny = MetaType.from_void(SrcCompPtr);
                ECS::Utils::InvokeMetaFunc(MetaType, "emplace"_hs,
                    entt::forward_as_meta(WorldRegistry), WorldE, entt::forward_as_meta(SrcAny));
            }

            // Remove components present on the instance that the prefab no longer ships.
            // Skip non-replicated, runtime-only, and the instance-tracking component itself.
            TVector<entt::id_type> ToRemoveStorages;
            for (auto&& [ID, WorldStorage] : WorldRegistry.storage())
            {
                if (IsNonReplicatedStorage(ID))    continue;
                if (IsRuntimeOnlyComponent(ID))    continue;
                if (PrefabComponentIDs.find(ID) != PrefabComponentIDs.end()) continue;
                if (bIsRoot && ID == entt::type_hash<STransformComponent>::value()) continue;
                if (!WorldStorage.contains(WorldE)) continue;
                ToRemoveStorages.push_back(ID);
            }
            for (entt::id_type ID : ToRemoveStorages)
            {
                if (auto* Storage = WorldRegistry.storage(ID))
                {
                    Storage->remove(WorldE);
                }
            }

            // Entity-handle fields just copied from the prefab still hold prefab ids; remap them
            // onto the instance entities (ids with no matching instance are cleared).
            ECS::Utils::RemapEntityReferences(WorldRegistry, WorldE, PrefabToInstance, /*bClearUnmapped*/ true);
        });

        // ---- 7. Re-stamp instance tracking components and rebuild hierarchy ---
        for (auto& [StableID, WorldE] : InstanceByStableID)
        {
            const bool bIsRoot = (WorldE == InstanceRoot);
            SPrefabInstanceComponent& Inst = WorldRegistry.emplace_or_replace<SPrefabInstanceComponent>(WorldE);
            Inst.SourcePrefab = this;
            Inst.StableID = StableID;
            Inst.bIsRoot = bIsRoot;
        }

        // Hierarchy: mirror the prefab's parent chain onto the instance, but never reparent
        // the placed root (its parent is wherever the user dropped it in the world).
        for (auto& [StableID, WorldE] : InstanceByStableID)
        {
            if (WorldE == InstanceRoot) continue;

            const auto PrefabIt = PrefabByStableID.find(StableID);
            if (PrefabIt == PrefabByStableID.end()) continue;

            const FRelationshipComponent* PrefabRel = Registry.try_get<FRelationshipComponent>(PrefabIt->second);
            const entt::entity PrefabParent = PrefabRel ? PrefabRel->Parent : entt::null;

            entt::entity DesiredWorldParent = InstanceRoot;
            if (PrefabParent != entt::null)
            {
                if (const SPrefabComponent* ParentTag = Registry.try_get<SPrefabComponent>(PrefabParent))
                {
                    auto ParentIt = InstanceByStableID.find(ParentTag->StableID);
                    if (ParentIt != InstanceByStableID.end())
                    {
                        DesiredWorldParent = ParentIt->second;
                    }
                }
            }

            const FRelationshipComponent* CurrentRel = WorldRegistry.try_get<FRelationshipComponent>(WorldE);
            const entt::entity CurrentParent = CurrentRel ? CurrentRel->Parent : entt::null;
            if (CurrentParent != DesiredWorldParent && WorldRegistry.valid(DesiredWorldParent))
            {
                ECS::Utils::ReparentEntity(WorldRegistry, WorldE, DesiredWorldParent);
            }
        }

        // Newly added entities + reparented ones need their world matrices recomputed.
        MarkSubtreeTransformsDirty(WorldRegistry, InstanceRoot);
    }

    void CPrefab::RefreshAllInstancesInWorld(CWorld* World)
    {
        if (World == nullptr)
        {
            return;
        }

        entt::registry& WorldRegistry = World->GetEntityRegistry();

        TVector<entt::entity> Roots;
        Roots.reserve(32);

        WorldRegistry.view<SPrefabInstanceComponent>().each([&](entt::entity E, const SPrefabInstanceComponent& Inst)
        {
            if (Inst.bIsRoot && Inst.SourcePrefab != nullptr)
            {
                Roots.push_back(E);
            }
        });

        for (entt::entity Root : Roots)
        {
            SPrefabInstanceComponent* Inst = WorldRegistry.try_get<SPrefabInstanceComponent>(Root);
            if (Inst == nullptr || Inst->SourcePrefab == nullptr)
            {
                continue;
            }
            Inst->SourcePrefab->RefreshInstance(World, Root);
        }
    }

    bool CPrefab::DetachInstance(CWorld* World, entt::entity InstanceRoot)
    {
        if (World == nullptr)
        {
            return false;
        }

        entt::registry& WorldRegistry = World->GetEntityRegistry();
        if (!WorldRegistry.valid(InstanceRoot))
        {
            return false;
        }

        const SPrefabInstanceComponent* RootInstance = WorldRegistry.try_get<SPrefabInstanceComponent>(InstanceRoot);
        if (RootInstance == nullptr || !RootInstance->bIsRoot)
        {
            return false;
        }

        // Strip the tracking component from the root + every descendant tagged for the same
        // source prefab. After this the subtree is just plain entities; world load will no
        // longer try to refresh them against the source asset.
        TVector<entt::entity> ToStrip;
        ToStrip.reserve(16);
        ToStrip.push_back(InstanceRoot);
        ECS::Utils::ForEachDescendant(WorldRegistry, InstanceRoot, [&](entt::entity Desc)
        {
            if (WorldRegistry.any_of<SPrefabInstanceComponent>(Desc))
            {
                ToStrip.push_back(Desc);
            }
        });

        for (entt::entity E : ToStrip)
        {
            WorldRegistry.remove<SPrefabInstanceComponent>(E);
        }
        return true;
    }

    void CPrefab::CaptureFromWorld(CWorld* SourceWorld, entt::entity RootEntity)
    {
        if (SourceWorld == nullptr)
        {
            return;
        }

        entt::registry& WorldRegistry = SourceWorld->GetEntityRegistry();
        if (!WorldRegistry.valid(RootEntity))
        {
            return;
        }

        TVector<entt::entity> EntitiesToCapture;
        EntitiesToCapture.reserve(16);
        EntitiesToCapture.push_back(RootEntity);
        ECS::Utils::ForEachDescendant(WorldRegistry, RootEntity, [&](entt::entity E)
        {
            EntitiesToCapture.push_back(E);
        });

        // Copy the captured subtree straight into a fresh registry. CopyRegistry remaps the
        // hierarchy (the root's parent and any escaping siblings fall to null) and entity-handle
        // fields, and skips nested instances' tracking components.
        Registry = entt::registry{};
        THashMap<entt::entity, entt::entity> Map;
        CopyRegistry(WorldRegistry, Registry, Map, &EntitiesToCapture, &ShouldSkipInstanceComponent);

        // Tag every captured entity with a stable id (reused from an existing instance tag when
        // present) so RefreshInstance can later match prefab entities to their placed counterparts.
        for (entt::entity SrcE : EntitiesToCapture)
        {
            auto It = Map.find(SrcE);
            if (It == Map.end())
            {
                continue;
            }

            FName StableID;
            if (const SPrefabInstanceComponent* Inst = WorldRegistry.try_get<SPrefabInstanceComponent>(SrcE))
            {
                StableID = Inst->StableID;
            }
            if (StableID.IsNone())
            {
                StableID = GenerateStableID();
            }
            Registry.emplace_or_replace<SPrefabComponent>(It->second).StableID = StableID;
        }

        if (CPackage* Package = GetPackage())
        {
            Package->MarkDirty();
        }
    }
}
