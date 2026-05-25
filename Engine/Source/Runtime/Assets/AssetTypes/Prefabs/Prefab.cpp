#include "pch.h"
#include "Prefab.h"
#include "PrefabComponents.h"

#include "Core/Object/Package/Package.h"
#include "GUID/GUID.h"
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

        entt::entity PrefabRoot = entt::null;
        Registry.view<entt::entity>().each([&](entt::entity E)
        {
            if (PrefabRoot != entt::null)
            {
                return;
            }

            const FRelationshipComponent* Rel = Registry.try_get<FRelationshipComponent>(E);
            const bool bHasParent = Rel && Rel->Parent != entt::null;
            if (!bHasParent)
            {
                PrefabRoot = E;
            }
        });

        if (PrefabRoot == entt::null)
        {
            return entt::null;
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

        // Prefab-entity -> instance-entity, so entity-handle fields copied from the prefab can be
        // remapped onto the matching instance entities (mirrors what CopyRegistry does on spawn).
        THashMap<entt::entity, entt::entity> PrefabToInstance;
        Registry.view<SPrefabComponent>().each([&](entt::entity PrefabE, const SPrefabComponent& PrefabComp)
        {
            auto It = InstanceByStableID.find(PrefabComp.StableID);
            if (It != InstanceByStableID.end())
            {
                PrefabToInstance[PrefabE] = It->second;
            }
        });

        Registry.view<SPrefabComponent>().each([&](entt::entity PrefabE, const SPrefabComponent& PrefabComp)
        {
            auto It = InstanceByStableID.find(PrefabComp.StableID);
            if (It == InstanceByStableID.end())
            {
                return;
            }

            entt::entity WorldE = It->second;
            const bool bIsRoot = (WorldE == InstanceRoot);

            for (auto&& [ID, PrefabStorage] : Registry.storage())
            {
                if (IsNonReplicatedStorage(ID))
                {
                    continue;
                }
                if (ID == entt::type_hash<SPrefabComponent>::value())
                {
                    continue;
                }
                // Preserve placed-root world transform; prefab's source transform would stomp placement.
                if (bIsRoot && ID == entt::type_hash<STransformComponent>::value())
                {
                    continue;
                }
                if (!PrefabStorage.contains(PrefabE))
                {
                    continue;
                }

                entt::meta_type MetaType = entt::resolve(PrefabStorage.info());
                if (!MetaType)
                {
                    continue;
                }

                void* SrcCompPtr = PrefabStorage.value(PrefabE);
                entt::meta_any SrcAny = MetaType.from_void(SrcCompPtr);
                ECS::Utils::InvokeMetaFunc(MetaType, "emplace"_hs,
                    entt::forward_as_meta(WorldRegistry), WorldE, entt::forward_as_meta(SrcAny));
            }

            // Entity-handle fields just copied from the prefab still hold prefab ids; remap them
            // onto the instance entities (ids with no matching instance are cleared).
            ECS::Utils::RemapEntityReferences(WorldRegistry, WorldE, PrefabToInstance, /*bClearUnmapped*/ true);
        });

        for (auto& [StableID, WorldE] : InstanceByStableID)
        {
            const bool bIsRoot = (WorldE == InstanceRoot);
            SPrefabInstanceComponent& Inst = WorldRegistry.emplace_or_replace<SPrefabInstanceComponent>(WorldE);
            Inst.SourcePrefab = this;
            Inst.StableID = StableID;
            Inst.bIsRoot = bIsRoot;
        }
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
