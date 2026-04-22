#include "pch.h"
#include "Prefab.h"
#include "PrefabComponents.h"

#include "Core/Object/Package/Package.h"
#include "GUID/GUID.h"
#include "World/Entity/Components/EditorComponent.h"
#include "World/Entity/Components/NameComponent.h"
#include "World/Entity/Components/RelationshipComponent.h"
#include "World/Entity/Components/TransformComponent.h"
#include "World/Entity/EntityUtils.h"
#include "World/World.h"

namespace Lumina
{
    namespace
    {
        /** Returns true if a given component-storage type ID corresponds to a component we refuse to copy cross-registry. */
        bool IsNonReplicatedStorage(entt::id_type ID)
        {
            // Relationships are remapped manually after the copy pass.
            if (ID == entt::type_hash<FRelationshipComponent>::value()) return true;
            // Editor-only selection / visibility state must not bleed from the preview world into a prefab or vice versa.
            if (ID == entt::type_hash<FSelectedInEditorComponent>::value()) return true;
            if (ID == entt::type_hash<FHideInSceneOutliner>::value()) return true;
            if (ID == entt::type_hash<FEditorComponent>::value()) return true;
            return false;
        }

        FName GenerateStableID()
        {
            return FName(FGuid::New().ToShortString());
        }
    }

    void CPrefab::Serialize(FArchive& Ar)
    {
        CObject::Serialize(Ar);
        ECS::Utils::SerializeRegistry(Ar, Registry);
    }

    void CPrefab::CopyRegistry(entt::registry& Source, entt::registry& Dest, THashMap<entt::entity, entt::entity>& OutMap)
    {
        using namespace entt::literals;

        Source.view<entt::entity>().each([&](entt::entity SrcE)
        {
            OutMap[SrcE] = Dest.create();
        });

        for (auto&& [ID, SrcSet] : Source.storage())
        {
            if (IsNonReplicatedStorage(ID))
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
    }

    entt::entity CPrefab::Instantiate(CWorld* TargetWorld, const FTransform& OffsetTransform, entt::entity Parent)
    {
        if (TargetWorld == nullptr)
        {
            return entt::null;
        }

        entt::registry& WorldRegistry = TargetWorld->GetEntityRegistry();

        // Identify the root of the prefab hierarchy (the entity with no parent in the prefab).
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

        // Gather the full instance hierarchy keyed by StableID.
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

        // For each prefab entity, find the matching world instance and overwrite reflected components.
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
                // Preserve the placed root's world transform — the prefab's source transform is
                // the authoring-time default and would stomp the placement offset on PIE/refresh.
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
        });

        // Re-establish the SPrefabInstanceComponent with root flag preserved for the root entity.
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

        // Collect the set of entities to capture: the root and all descendants.
        TVector<entt::entity> EntitiesToCapture;
        EntitiesToCapture.reserve(16);
        EntitiesToCapture.push_back(RootEntity);
        ECS::Utils::ForEachDescendant(WorldRegistry, RootEntity, [&](entt::entity E)
        {
            EntitiesToCapture.push_back(E);
        });

        // Build a scratch registry that owns only the captured entities, so we can reuse CopyRegistry.
        entt::registry Scratch;
        THashMap<entt::entity, entt::entity> ToScratch;
        for (entt::entity SrcE : EntitiesToCapture)
        {
            ToScratch[SrcE] = Scratch.create();
        }

        using namespace entt::literals;
        for (auto&& [ID, SrcSet] : WorldRegistry.storage())
        {
            if (IsNonReplicatedStorage(ID))
            {
                continue;
            }
            if (ID == entt::type_hash<SPrefabInstanceComponent>::value())
            {
                continue;
            }

            entt::meta_type MetaType = entt::resolve(SrcSet.info());
            if (!MetaType)
            {
                continue;
            }

            for (entt::entity SrcE : EntitiesToCapture)
            {
                if (!SrcSet.contains(SrcE))
                {
                    continue;
                }
                entt::entity DestE = ToScratch[SrcE];
                void* SrcCompPtr = SrcSet.value(SrcE);
                entt::meta_any SrcAny = MetaType.from_void(SrcCompPtr);
                ECS::Utils::InvokeMetaFunc(MetaType, "emplace"_hs,
                    entt::forward_as_meta(Scratch), DestE, entt::forward_as_meta(SrcAny));
            }
        }

        auto Remap = [&](entt::entity& E)
        {
            if (E == entt::null) return;
            auto It = ToScratch.find(E);
            E = (It != ToScratch.end()) ? It->second : entt::null;
        };

        for (entt::entity SrcE : EntitiesToCapture)
        {
            const FRelationshipComponent* SrcRel = WorldRegistry.try_get<FRelationshipComponent>(SrcE);
            if (SrcRel == nullptr) continue;

            FRelationshipComponent DestRel = *SrcRel;
            Remap(DestRel.First);
            Remap(DestRel.Prev);
            Remap(DestRel.Next);
            Remap(DestRel.Parent);
            // The captured root has no parent (captured subtree is rooted here).
            if (SrcE == RootEntity)
            {
                DestRel.Parent = entt::null;
            }
            Scratch.emplace_or_replace<FRelationshipComponent>(ToScratch[SrcE], DestRel);
        }

        // Assign fresh stable IDs for any entity that doesn't already have one.
        for (entt::entity SrcE : EntitiesToCapture)
        {
            entt::entity DestE = ToScratch[SrcE];
            FName StableID;
            if (const SPrefabInstanceComponent* Inst = WorldRegistry.try_get<SPrefabInstanceComponent>(SrcE))
            {
                StableID = Inst->StableID;
            }
            if (StableID.IsNone())
            {
                StableID = GenerateStableID();
            }
            Scratch.emplace_or_replace<SPrefabComponent>(DestE).StableID = StableID;
        }

        // Replace this prefab's contents with the scratch.
        Registry.clear<>();
        Registry = entt::registry{};
        THashMap<entt::entity, entt::entity> UnusedMap;
        CopyRegistry(Scratch, Registry, UnusedMap);

        if (CPackage* Package = GetPackage())
        {
            Package->MarkDirty();
        }
    }
}
