#include "pch.h"
#include "EntityUtils.h"
#include "Components/CharacterComponent.h"
#include "Components/DirtyComponent.h"
#include "Components/EditorComponent.h"
#include "components/entitytags.h"
#include "Components/PhysicsComponent.h"
#include "Components/NameComponent.h"
#include "Components/RelationshipComponent.h"
#include "Components/NetworkComponent.h"
#include "Components/CSharpScriptComponent.h"
#include "components/tagcomponent.h"
#include "Components/TransformComponent.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Core/Object/Class.h"
#include "Core/Object/ObjectArray.h"
#include "Core/Object/Package/Package.h"
#include "Core/Reflection/Type/LuminaTypes.h"
#include "Core/Reflection/Type/Properties/ArrayProperty.h"
#include "Core/Reflection/Type/Properties/StructProperty.h"
#include "Components/Component.h"
#include "Memory/SmartPtr.h"
#include "World/World.h"
#include "World/WorldContext.h"
#include "World/Net/NetReplication.h"
#include <atomic>
#include <EASTL/hash_map.h>

namespace Lumina
{
    // Registry of per-component direct-call op tables (see FComponentOps). Populated at component
    // registration; resolved by the C# bridge once per type. Keyed by a hash of the exact name bytes
    // so a non-null-terminated name from C# matches the registered c_str name.
    namespace
    {
        eastl::hash_map<uint32, const FComponentOps*>& ComponentOpsMap()
        {
            static eastl::hash_map<uint32, const FComponentOps*> Map;
            return Map;
        }

        uint32 ComponentOpsKey(FStringView Name)
        {
            const eastl::string Terminated(Name.data(), Name.size());
            return entt::hashed_string(Terminated.c_str());
        }
    }

    void RegisterComponentOps(FStringView Name, const FComponentOps* Ops)
    {
        ComponentOpsMap()[ComponentOpsKey(Name)] = Ops;
    }

    const FComponentOps* FindComponentOps(FStringView Name)
    {
        const auto It = ComponentOpsMap().find(ComponentOpsKey(Name));
        return It != ComponentOpsMap().end() ? It->second : nullptr;
    }
}

using namespace entt::literals; 

namespace Lumina::ECS::Utils
{
    // --- Serialization ---

    bool SerializeEntity(FArchive& RESTRICT Ar, FEntityRegistry& RESTRICT Registry, entt::entity& RESTRICT Entity)
    {
        using namespace entt::literals;
        
        if (Ar.IsWriting())
        {
            Ar << Entity;

            FRelationshipComponent* RelationshipComponent = Registry.try_get<FRelationshipComponent>(Entity);
            bool bHasRelationship = (RelationshipComponent != nullptr);
            Ar << bHasRelationship;

            if (bHasRelationship)
            {
                Ar << *RelationshipComponent;
            }

            int64 NumComponentsPos = Ar.Tell();
            size_t NumComponents = 0;
            Ar << NumComponents;

            for (auto [ID, Set] : Registry.storage())
            {
                if (!Set.contains(Entity))
                {
                    continue;
                }

                {
                    void* ComponentPointer = Set.value(Entity);
                    entt::meta_type MetaType = entt::resolve(Set.info());
                    if (entt::meta_any ReturnValue = InvokeMetaFunc(MetaType, "static_struct"_hs))
                    {
                        CStruct* StructType = ReturnValue.cast<CStruct*>();
                        ASSERT(StructType);

                        FName Name = StructType->GetName();
                        Ar << Name;
                        
                        int64 ComponentStart = Ar.Tell();

                        int64 ComponentSize = 0;
                        Ar << ComponentSize;

                        int64 StartOfComponentData = Ar.Tell();

                        StructType->SerializeTaggedProperties(Ar, ComponentPointer);

                        int64 EndOfComponentData = Ar.Tell();

                        ComponentSize = EndOfComponentData - StartOfComponentData;

                        Ar.Seek(ComponentStart);
                        Ar << ComponentSize;
                        Ar.Seek(EndOfComponentData);
                        
                        NumComponents++;
                    }
                }
            }

            int64 SizeBefore = Ar.Tell();
            Ar.Seek(NumComponentsPos);    
            Ar << NumComponents;
            Ar.Seek(SizeBefore);
            
        }
        else if (Ar.IsReading())
        {
            Ar << Entity;

            if (!Registry.valid(Entity))
            {
                entt::entity New = Registry.create(Entity);
                ALERT_IF_NOT(New == Entity);
                Entity = New;
            }

            bool bHasRelationship = false;
            Ar << bHasRelationship;

            if (bHasRelationship)
            {
                FRelationshipComponent& RelationshipComponent = Registry.emplace_or_replace<FRelationshipComponent>(Entity);
                Ar << RelationshipComponent;
            }

            size_t NumComponents = 0;
            Ar << NumComponents;

            if (NumComponents > Ar.GetMaxSerializeSize())
            {
                LOG_ERROR("Archiver corrupted: entity claims {} components (max {})", NumComponents, Ar.GetMaxSerializeSize());
                Ar.SetHasError(true);
                return false;
            }

            for (size_t i = 0; i < NumComponents; ++i)
            {
                FName TypeName;
                Ar << TypeName;

                int64 ComponentSize = 0;
                Ar << ComponentSize;

                int64 ComponentStart = Ar.Tell();

                if (CStruct* Struct = FindObject<CStruct>(TypeName))
                {
                    if (Struct == STagComponent::StaticStruct())
                    {
                        STagComponent NewTagComponent;
                        Struct->SerializeTaggedProperties(Ar, &NewTagComponent);
                        auto HashedString = entt::hashed_string(NewTagComponent.Tag.c_str());

                        if (!Registry.storage<STagComponent>(HashedString).contains(Entity))
                        {
                            Registry.storage<STagComponent>(HashedString).emplace(Entity, NewTagComponent);
                        }
                    }
                    else
                    {
                        entt::hashed_string HashString(Struct->GetName().c_str());
                        if (entt::meta_type Meta = entt::resolve(HashString))
                        {
                            entt::meta_any Any = Meta.construct();

                            InvokeMetaFunc(Meta, "serialize"_hs, entt::forward_as_meta(Ar), entt::forward_as_meta(Any));
                            InvokeMetaFunc(Meta, "emplace"_hs, entt::forward_as_meta(Registry), Entity, entt::forward_as_meta(Any));
                        }
                        else
                        {
                            LOG_WARN("[ECS] Entity {}: component '{}' has CStruct but no entt meta_type; skipping ({} bytes).",
                                (uint32)Entity, TypeName, ComponentSize);
                        }
                    }
                }
                else
                {
                    LOG_WARN("[ECS] Entity {}: skipping unknown component '{}' ({} bytes). Disabled plugin? Save will drop this component.", (uint32)Entity, TypeName, ComponentSize);
                }

                int64 ComponentEnd = ComponentSize + ComponentStart;
                Ar.Seek(ComponentEnd);
            }
        }
        
        return !Ar.HasError();
    }

    bool SerializeRegistry(FArchive& Ar, FEntityRegistry& Registry)
    {
        using namespace entt::literals;
        
        if (Ar.IsWriting())
        {
            Registry.compact<>();
            auto View = Registry.view<entt::entity>(entt::exclude<FEditorComponent>);

            int64 PreSerializePos = Ar.Tell();
    
            int32 NumEntitiesSerialized = 0;
            Ar << NumEntitiesSerialized;

            View.each([&](entt::entity Entity)
            {
                int64 PreEntityPos = Ar.Tell();
        
                int64 EntitySaveSize = 0;
                Ar << EntitySaveSize;

                bool bSuccess = SerializeEntity(Ar, Registry, Entity);
                if (!bSuccess)
                {
                    // Rewind to before this entity's data and continue with next entity
                    Ar.Seek(PreEntityPos);
                    return;
                }

                NumEntitiesSerialized++;

                int64 PostEntityPos = Ar.Tell();

                // Calculate actual size written (excluding the size field itself)
                EntitySaveSize = PostEntityPos - PreEntityPos - sizeof(int64);
        
                // Go back and write the correct size
                Ar.Seek(PreEntityPos);
                Ar << EntitySaveSize;
        
                // Return to end position to continue with next entity
                Ar.Seek(PostEntityPos);
            });
    
            int64 PostSerializePos = Ar.Tell();

            // Go back and write the actual number of successfully serialized entities
            Ar.Seek(PreSerializePos);
            Ar << NumEntitiesSerialized;

            // Return to end of all serialized data
            Ar.Seek(PostSerializePos);
        }
        else if (Ar.IsReading())
        {
            int32 NumEntitiesSerialized = 0;
            Ar << NumEntitiesSerialized;

            if (NumEntitiesSerialized < 0 || (size_t)NumEntitiesSerialized > Ar.GetMaxSerializeSize())
            {
                LOG_ERROR("Archiver corrupted: registry claims {} entities (max {})", NumEntitiesSerialized, Ar.GetMaxSerializeSize());
                Ar.SetHasError(true);
                return false;
            }

            for (int32 i = 0; i < NumEntitiesSerialized; ++i)
            {
                int64 EntitySaveSize = 0;
                Ar << EntitySaveSize;

                int64 PreEntityPos = Ar.Tell();

                entt::entity NewEntity = entt::null;
                bool bSuccess = ECS::Utils::SerializeEntity(Ar, Registry, NewEntity);

                // Clear the per-entity error so one corrupt entity doesn't poison subsequent
                // SerializeEntity calls; the size-header seek below re-aligns the stream regardless.
                Ar.SetHasError(false);

                if (!bSuccess || NewEntity == entt::null)
                {
                    // Skip to the next entity using the saved size
                    LOG_ERROR("Failed to serialize entity: {}", (int)NewEntity);
                    Ar.Seek(PreEntityPos + EntitySaveSize);
                    continue;
                }

                Registry.emplace_or_replace<FNeedsTransformUpdate>(NewEntity);

                int64 PostEntityPos = Ar.Tell();
                int64 ActualBytesRead = PostEntityPos - PreEntityPos;

                if (ActualBytesRead != EntitySaveSize)
                {
                    // Data mismatch, seek to correct position to stay aligned
                    LOG_ERROR("Entity Serialization Mismatch For {}: Expected: {} - Read: {}", (int)NewEntity, EntitySaveSize, ActualBytesRead);
                    Ar.Seek(PreEntityPos + EntitySaveSize);
                }
            }
        }

        return !Ar.HasError();
    }
    
    bool EntityHasTag(const FName& Tag, FEntityRegistry& Registry, entt::entity Entity)
    {
        return Registry.storage<STagComponent>(entt::hashed_string(Tag.c_str())).contains(Entity);
    }
    
    // --- Hierarchy ---

    void AddToParent(FEntityRegistry& Registry, entt::entity Child, entt::entity Parent)
    {
        FRelationshipComponent& ChildRelationship = Registry.get_or_emplace<FRelationshipComponent>(Child);
        FRelationshipComponent& ParentRelationship = Registry.get_or_emplace<FRelationshipComponent>(Parent);

        ChildRelationship.Parent = Parent;

        ChildRelationship.Prev = entt::null;
        ChildRelationship.Next = ParentRelationship.First;

        if (ParentRelationship.First != entt::null)
        {
            FRelationshipComponent& OldFirstRelationship = Registry.get<FRelationshipComponent>(ParentRelationship.First);
            OldFirstRelationship.Prev = Child;
        }

        ParentRelationship.First = Child;
        ParentRelationship.Children++;
    }
    
    void ReparentEntity(FEntityRegistry& Registry, entt::entity Child, entt::entity Parent, bool bPreserveWorld)
    {
        // Self-parent or circular hierarchy causes an infinite loop in ForEachChild.
        if (Child == Parent)
        {
            LOG_ERROR("Cannot parent an entity to itself!");
            return;
        }

        if (Child == entt::null)
        {
            LOG_ERROR("Cannot parent a null entity!");
            return;
        }

        // Always guarded: a cycle here infinite-loops in ForEachChild traversal, so this
        // must reject in shipping builds too, not just debug.
        if (Parent != entt::null && IsDescendantOf(Registry, Parent, Child))
        {
            LOG_ERROR("Cannot create circular hierarchy - parent is a descendant of child!");
            return;
        }

        FRelationshipComponent& ChildRelationship = Registry.get_or_emplace<FRelationshipComponent>(Child);
        STransformComponent& ChildTransform = Registry.get<STransformComponent>(Child);

        if (ChildRelationship.Parent == Parent)
        {
            return;
        }
        
        FTransform NewLocalTransform;
        if (bPreserveWorld)
        {
            const FMatrix4 ChildWorldMatrix  = ChildTransform.GetWorldMatrix();
            const FMatrix4 ParentWorldMatrix = (Parent != entt::null)
                ? Registry.get<STransformComponent>(Parent).GetWorldMatrix() : FMatrix4(1.0f);
            const FMatrix4 NewLocalMatrix = Math::Inverse(ParentWorldMatrix) * ChildWorldMatrix;

            FVector3 Translation, Scale, Skew;
            FQuat    Rotation;
            FVector4 Perspective;
            Math::Decompose(NewLocalMatrix, Scale, Rotation, Translation, Skew, Perspective);
            NewLocalTransform.Location = Translation;
            NewLocalTransform.Rotation = Rotation;
            NewLocalTransform.Scale    = Scale;
        }

        RemoveFromParent(Registry, Child);

        if (Parent != entt::null)
        {
            AddToParent(Registry, Child, Parent);
        }
        else
        {
            ChildRelationship.Parent = entt::null;
        }

        if (Parent != entt::null && Registry.any_of<SDisabledTag>(Parent))
        {
            if (!Registry.any_of<SDisabledTag>(Child))
            {
                Registry.emplace<SDisabledTag>(Child);
            }
        }

        if (bPreserveWorld)
        {
            ChildTransform.SetLocalTransform(NewLocalTransform); // marks the transform dirty
        }
        else
        {
            // Keep the replicated local; recompose world under the new parent next resolve.
            Registry.emplace_or_replace<FNeedsTransformUpdate>(Child);
        }

        // A networked entity's attachment change must reach clients.
        if (CWorld** WorldPtr = Registry.ctx().find<CWorld*>())
        {
            if (CWorld* World = *WorldPtr)
            {
                const ENetMode Mode = World->GetNetMode();
                if ((Mode == ENetMode::ListenServer || Mode == ENetMode::DedicatedServer) && Registry.all_of<SNetworkComponent>(Child))
                {
                    Registry.emplace_or_replace<FNetDirty>(Child);
                }
            }
        }
    }

    void DestroyEntityHierarchy(FEntityRegistry& Registry, entt::entity Entity)
    {
        if (Entity == entt::null || !Registry.valid(Entity))
        {
            return;
        }

        TVector<entt::entity> ToDestroy;
        CollectDescendants(Registry, Entity, ToDestroy);

        // Detach from the parent's sibling list first so the parent's relationship component
        // stops pointing at freed entities, then destroy this entity and all descendants.
        if (Registry.any_of<FRelationshipComponent>(Entity))
        {
            RemoveFromParent(Registry, Entity);
        }

        ToDestroy.push_back(Entity);

        for (entt::entity E : ToDestroy)
        {
            if (Registry.valid(E))
            {
                Registry.destroy(E);
            }
        }
    }

    void DetachImmediateChildren(FEntityRegistry& Registry, entt::entity Entity)
    {
        TVector<entt::entity> ToDestroy;
        CollectChildren(Registry, Entity, ToDestroy);
        
        for (auto It = ToDestroy.rbegin(); It != ToDestroy.rend(); ++It)
        {
            if (Registry.valid(*It))
            {
                RemoveFromParent(Registry, *It);
            }
        }
    }

    void RemoveFromParent(FEntityRegistry& Registry, entt::entity Child)
    {
        FRelationshipComponent* ChildRelationship = Registry.try_get<FRelationshipComponent>(Child);
        if (!ChildRelationship || ChildRelationship->Parent == entt::null)
        {
            return;
        }

        // Snapshot the world transform while the parent chain is still intact; once detached the
        // entity has no parent, so its local space becomes its world space (see SetEntityWorldTransform).
        FTransform WorldSnapshot;
        bool bHasTransform = false;
        if (STransformComponent* TransformComponent = Registry.try_get<STransformComponent>(Child))
        {
            WorldSnapshot = TransformComponent->GetWorldTransform();
            bHasTransform = true;
        }

        entt::entity OldParent = ChildRelationship->Parent;
        FRelationshipComponent* ParentRelationship = Registry.try_get<FRelationshipComponent>(OldParent);

        if (!ParentRelationship)
        {
            return;
        }

        ParentRelationship->Children--;

        if (ChildRelationship->Prev != entt::null)
        {
            FRelationshipComponent& PrevRelationship = Registry.get<FRelationshipComponent>(ChildRelationship->Prev);
            PrevRelationship.Next = ChildRelationship->Next;
        }
        else
        {
            ParentRelationship->First = ChildRelationship->Next;
        }

        if (ChildRelationship->Next != entt::null)
        {
            FRelationshipComponent& NextRelationship = Registry.get<FRelationshipComponent>(ChildRelationship->Next);
            NextRelationship.Prev = ChildRelationship->Prev;
        }

        ChildRelationship->Parent = entt::null;
        ChildRelationship->Prev = entt::null;
        ChildRelationship->Next = entt::null;

        // Bake the snapshot back as local now that there's no parent to inherit from, so the
        // detached entity keeps its world placement.
        if (bHasTransform)
        {
            SetEntityWorldTransform(Registry, Child, WorldSnapshot);
        }
    }

    bool IsDescendantOf(FEntityRegistry& Registry, entt::entity Potential, entt::entity Ancestor)
    {
        if (Potential == entt::null || Ancestor == entt::null)
        {
            return false;
        }

        entt::entity Current = Potential;
        while (Current != entt::null)
        {
            FRelationshipComponent* Relationship = Registry.try_get<FRelationshipComponent>(Current);
            if (!Relationship)
            {
                break;
            }

            if (Relationship->Parent == Ancestor)
            {
                return true;
            }

            Current = Relationship->Parent;
        }

        return false;
    }

    bool IsChild(FEntityRegistry& Registry, entt::entity Entity)
    {
        FRelationshipComponent* Relationship = Registry.try_get<FRelationshipComponent>(Entity);
        return Relationship ? Relationship->Parent != entt::null : false;
    }

    bool IsParent(FEntityRegistry& Registry, entt::entity Entity)
    {
        return GetChildCount(Registry, Entity) != 0;
    }

    entt::entity GetRootEntity(FEntityRegistry& Registry, entt::entity Entity)
    {
        entt::entity Current = Entity;
        while (Current != entt::null)
        {
            FRelationshipComponent* Relationship = Registry.try_get<FRelationshipComponent>(Current);
            if (!Relationship || Relationship->Parent == entt::null)
            {
                break;
            }

            Current = Relationship->Parent;
        }

        return Current;
    }

    size_t GetChildCount(FEntityRegistry& Registry, entt::entity Parent)
    {
        FRelationshipComponent* Relationship = Registry.try_get<FRelationshipComponent>(Parent);
        return Relationship ? Relationship->Children : 0;
    }

    void CollectDescendants(FEntityRegistry& Registry, entt::entity Entity, TVector<entt::entity>& OutDescendants)
    {
        OutDescendants.push_back(Entity);

        FRelationshipComponent* Relationship = Registry.try_get<FRelationshipComponent>(Entity);
        if (!Relationship || Relationship->First == entt::null)
        {
            return;
        }

        entt::entity Current = Relationship->First;
        while (Current != entt::null)
        {
            CollectDescendants(Registry, Current, OutDescendants);

            FRelationshipComponent* CurrentRelationship = Registry.try_get<FRelationshipComponent>(Current);
            Current = CurrentRelationship ? CurrentRelationship->Next : entt::null;
        }
    }

    void CollectChildren(FEntityRegistry& Registry, entt::entity Entity, TVector<entt::entity>& OutChildren)
    {
        FRelationshipComponent* Relationship = Registry.try_get<FRelationshipComponent>(Entity);
        if (!Relationship || Relationship->First == entt::null)
        {
            return;
        }

        entt::entity Current = Relationship->First;
        while (Current != entt::null)
        {
            OutChildren.push_back(Current);

            FRelationshipComponent* CurrentRelationship = Registry.try_get<FRelationshipComponent>(Current);
            Current = CurrentRelationship ? CurrentRelationship->Next : entt::null;
        }
    }

    bool HasComponent(FEntityRegistry& Registry, entt::entity Entity, entt::meta_type Type)
    {
        if (entt::meta_any Any = InvokeMetaFunc(Type, "has"_hs, entt::forward_as_meta(Registry), Entity))
        {
            return Any.cast<bool>();
        }
        
        return false;
    }
    
    struct CACHE_ALIGN FTransformDirtyState
    {
        std::atomic<bool> bAnyDirty{ true };
    };
    
    static void OnTransformDirtied(FTransformDirtyState* State, FEntityRegistry& Registry, entt::entity Entity)
    {
        State->bAnyDirty.store(true, std::memory_order_release);
        if (STransformComponent* Transform = Registry.try_get<STransformComponent>(Entity))
        {
            Transform->bWorldDirty = true;
        }
    }

    static FTransformDirtyState* FindTransformDirtyState(FEntityRegistry& Registry)
    {
        if (TUniquePtr<FTransformDirtyState>* Holder = Registry.ctx().find<TUniquePtr<FTransformDirtyState>>())
        {
            return Holder->get();
        }
        return nullptr;
    }
    
    static FTransformDirtyState& EnsureTransformDirtyState(FEntityRegistry& Registry)
    {
        if (TUniquePtr<FTransformDirtyState>* Holder = Registry.ctx().find<TUniquePtr<FTransformDirtyState>>())
        {
            return *Holder->get();
        }

        FTransformDirtyState& State = *Registry.ctx().emplace<TUniquePtr<FTransformDirtyState>>(MakeUnique<FTransformDirtyState>());
        Registry.on_construct<FNeedsTransformUpdate>().connect<&OnTransformDirtied>(&State);
        return State;
    }
    
    std::atomic<bool>* EnsureTransformDirtySignal(FEntityRegistry& Registry)
    {
        return &EnsureTransformDirtyState(Registry).bAnyDirty;
    }

    // Recompute world = parentWorld * local for every descendant of Root.
    template<typename TTransformStorage>
    static void PropagateTransformsToDescendants(FEntityRegistry& Registry, TTransformStorage& TransformStorage, entt::entity Root, bool bClearDirty)
    {
        TFixedVector<entt::entity, 64> Stack;
        Stack.push_back(Root);

        while (!Stack.empty())
        {
            const entt::entity Parent = Stack.back();
            Stack.pop_back();

            const FTransform ParentWorld = TransformStorage.get(Parent).WorldTransform;
            ForEachChild(Registry, Parent, [&](entt::entity Child)
            {
                STransformComponent& ChildTransform = TransformStorage.get(Child);

                ChildTransform.WorldTransform = ParentWorld * ChildTransform.LocalTransform;
                ChildTransform.CachedMatrix   = ChildTransform.WorldTransform.GetMatrix();

                if (bClearDirty)
                {
                    ChildTransform.bWorldDirty = false;
                }

                Stack.push_back(Child);
            });
        }
    }

    void ResolveTransformChain(FEntityRegistry& Registry, entt::entity Entity)
    {
        LUMINA_PROFILE_SCOPE();
        
        if (const FTransformDirtyState* DirtyState = FindTransformDirtyState(Registry))
        {
            if (!DirtyState->bAnyDirty.load(std::memory_order_acquire))
            {
                return;
            }
        }
        
        TFixedVector<entt::entity, 64> AncestorChain;
        int32 TopmostDirtyIndex = -1;

        entt::entity Current = Entity;
        auto&& RelStorage = Registry.storage<FRelationshipComponent>();
        auto&& XFormStorage = Registry.storage<STransformComponent>();
        
        while (Current != entt::null)
        {
            if (XFormStorage.contains(Current) && XFormStorage.get(Current).bWorldDirty)
            {
                TopmostDirtyIndex = (int32)AncestorChain.size();
            }

            AncestorChain.push_back(Current);

            if (!Registry.all_of<FRelationshipComponent>(Current))
            {
                break;
            }

            entt::entity Parent = RelStorage.get(Current).Parent;
            if (Parent == entt::null || !Registry.valid(Parent))
            {
                break;
            }

            Current = Parent;
        }

        if (TopmostDirtyIndex < 0)
        {
            return;
        }

        for (int32 i = TopmostDirtyIndex; i >= 0; --i)
        {
            entt::entity Ancestor = AncestorChain[i];
            auto& Transform = XFormStorage.get(Ancestor);

            FRelationshipComponent* Rel = Registry.try_get<FRelationshipComponent>(Ancestor);
            if (Rel && Rel->Parent != entt::null && Registry.valid(Rel->Parent))
            {
                const FTransform& ParentWorld = XFormStorage.get(Rel->Parent).WorldTransform;
                Transform.WorldTransform = ParentWorld * Transform.LocalTransform;
            }
            else
            {
                Transform.WorldTransform = Transform.LocalTransform;
            }

            Transform.CachedMatrix = Transform.WorldTransform.GetMatrix();
        }

        // Propagate to the full subtree. Compute-only (no flag clearing), so no lock needed.
        PropagateTransformsToDescendants(Registry, XFormStorage, AncestorChain[TopmostDirtyIndex], /*bClearDirty*/ false);
    }

    void ResolveAllDirtyTransforms(FEntityRegistry& Registry)
    {
        LUMINA_PROFILE_SCOPE();

        FTransformDirtyState& DirtyState = EnsureTransformDirtyState(Registry);
        auto& TransformStorage = Registry.storage<STransformComponent>();
        
        bool bPendingTags = false;
        for (entt::entity Tagged : Registry.view<FNeedsTransformUpdate>())
        {
            if (TransformStorage.contains(Tagged))
            {
                TransformStorage.get(Tagged).bWorldDirty = true;
                bPendingTags = true;
            }
        }

        // bAnyDirty (or a pending tag) gates the scan; nothing dirty -> nothing to do.
        if (!bPendingTags && !DirtyState.bAnyDirty.load(std::memory_order_acquire))
        {
            return;
        }

        // Hierarchical: resolve only topmost-dirty roots top-down, so parallel subtrees stay disjoint.
        {
            auto HierView = Registry.view<STransformComponent, FRelationshipComponent>();

            TFixedVector<entt::entity, 100> DirtyEntities;
            for (auto Entity : HierView)
            {
                if (HierView.get<STransformComponent>(Entity).bWorldDirty)
                {
                    DirtyEntities.push_back(Entity);
                }
            }

            if (!DirtyEntities.empty())
            {
                auto ResolveOne = [&](uint32 Index)
                {
                    entt::entity DirtyEntity = DirtyEntities[Index];

                    auto& DirtyTransform    = HierView.get<STransformComponent>(DirtyEntity);
                    auto& DirtyRelationship = HierView.get<FRelationshipComponent>(DirtyEntity);

                    const bool bHasParent = DirtyRelationship.Parent != entt::null && Registry.valid(DirtyRelationship.Parent);

                    // Parent dirty too -> its descent covers this subtree. (Flags are cleared after the pass.)
                    if (bHasParent && TransformStorage.contains(DirtyRelationship.Parent)
                        && TransformStorage.get(DirtyRelationship.Parent).bWorldDirty)
                    {
                        return;
                    }

                    if (bHasParent)
                    {
                        const FTransform& ParentWorld = TransformStorage.get(DirtyRelationship.Parent).WorldTransform;
                        DirtyTransform.WorldTransform = ParentWorld * DirtyTransform.LocalTransform;
                    }
                    else
                    {
                        DirtyTransform.WorldTransform = DirtyTransform.LocalTransform;
                    }

                    DirtyTransform.CachedMatrix = DirtyTransform.WorldTransform.GetMatrix();

                    PropagateTransformsToDescendants(Registry, TransformStorage, DirtyEntity, /*bClearDirty*/ false);
                };

                if (DirtyEntities.size() > 1000)
                {
                    Task::ParallelFor((uint32)DirtyEntities.size(), ResolveOne);
                }
                else
                {
                    for (uint32 i = 0; i < (uint32)DirtyEntities.size(); ++i)
                    {
                        ResolveOne(i);
                    }
                }

                // Clear flags after the pass (keeps the parent-dirty read above stable).
                for (entt::entity Resolved : DirtyEntities)
                {
                    TransformStorage.get(Resolved).bWorldDirty = false;
                }
            }
        }

        // Flat (no relationship): world == local. Resolve + clear inline (no parent, so safe during the pass).
        {
            auto FlatView   = Registry.view<STransformComponent>(entt::exclude<FRelationshipComponent>);
            auto FlatHandle = FlatView.handle();

            auto ResolveFlat = [&](uint32 Index)
            {
                entt::entity Entity = (*FlatHandle)[Index];
                if (!FlatView.contains(Entity))
                {
                    return; // hierarchical (handled above) or a tombstone hole
                }
                STransformComponent& Transform = FlatView.get<STransformComponent>(Entity);
                if (!Transform.bWorldDirty)
                {
                    return;
                }
                Transform.WorldTransform = Transform.LocalTransform;
                Transform.CachedMatrix   = Transform.WorldTransform.GetMatrix();
                Transform.bWorldDirty    = false;
            };

            if (FlatHandle->size() > 1000)
            {
                Task::ParallelFor((uint32)FlatHandle->size(), ResolveFlat);
            }
            else
            {
                for (uint32 i = 0; i < (uint32)FlatHandle->size(); ++i)
                {
                    ResolveFlat(i);
                }
            }
        }

        Registry.clear<FNeedsTransformUpdate>();
        DirtyState.bAnyDirty.store(false, std::memory_order_release);
    }

    // --- Entity transform accessors ---

    FQuat GetEntityRotation(FEntityRegistry& Registry, entt::entity Entity)
    {
        auto* Transform = Registry.try_get<STransformComponent>(Entity);
        return Transform ? Transform->GetWorldRotation() : FQuat{};
    }

    FVector3 GetEntityScale(FEntityRegistry& Registry, entt::entity Entity)
    {
        auto* Transform = Registry.try_get<STransformComponent>(Entity);
        return Transform ? Transform->GetWorldScale() : FVector3{};
    }

    void SetEntityScale(FEntityRegistry& Registry, entt::entity Entity, const FVector3& Scale)
    {
        if (auto* Transform = Registry.try_get<STransformComponent>(Entity))
        {
            Transform->SetScale(Scale);
        }
    }

    namespace
    {
        // Remap one stored entity-handle id (uint32 of an entt::entity) in place: through Map if
        // present, else cleared to null when bClearUnmapped. entt::null is left as-is.
        void RemapEntityHandle(uint32& Value, const THashMap<entt::entity, entt::entity>& Map, bool bClearUnmapped)
        {
            const entt::entity Stored = static_cast<entt::entity>(Value);
            if (Stored == entt::null)
            {
                return;
            }

            auto It = Map.find(Stored);
            if (It != Map.end())
            {
                Value = static_cast<uint32>(entt::to_integral(It->second));
            }
            else if (bClearUnmapped)
            {
                Value = static_cast<uint32>(entt::to_integral(static_cast<entt::entity>(entt::null)));
            }
        }

        // Walk one struct's reflected properties: remap uint32 "Entity"-tagged handles through Map,
        // recurse into nested struct fields, and walk arrays of handles or of structs.
        void RemapEntityRefsInStruct(CStruct* Struct, void* Data, const THashMap<entt::entity, entt::entity>& Map, bool bClearUnmapped)
        {
            for (CStruct* Cur = Struct; Cur != nullptr; Cur = Cur->GetSuperStruct())
            {
                for (FProperty* Property = Cur->LinkedProperty; Property != nullptr; Property = static_cast<FProperty*>(Property->Next))
                {
                    if (Property->IsA(EPropertyTypeFlags::UInt32) && Property->HasMetadata("Entity"))
                    {
                        uint32 Value = 0;
                        Property->GetValue(Data, &Value);
                        RemapEntityHandle(Value, Map, bClearUnmapped);
                        Property->SetValue(Data, Value);
                    }
                    else if (Property->IsA(EPropertyTypeFlags::Struct))
                    {
                        if (CStruct* Inner = static_cast<FStructProperty*>(Property)->GetStruct())
                        {
                            RemapEntityRefsInStruct(Inner, Property->GetValuePtr<void>(Data), Map, bClearUnmapped);
                        }
                    }
                    else if (Property->IsA(EPropertyTypeFlags::Vector))
                    {
                        FArrayProperty* ArrayProperty = static_cast<FArrayProperty*>(Property);
                        FProperty* Inner = ArrayProperty->GetInternalProperty();
                        if (Inner == nullptr)
                        {
                            continue;
                        }

                        void* ArrayPtr = Property->GetValuePtr<void>(Data);

                        if (Inner->IsA(EPropertyTypeFlags::UInt32) && Property->HasMetadata("Entity"))
                        {
                            ArrayProperty->ForEach<uint32>(ArrayPtr, [&](uint32* Elem, SIZE_T)
                            {
                                RemapEntityHandle(*Elem, Map, bClearUnmapped);
                            });
                        }
                        else if (Inner->IsA(EPropertyTypeFlags::Struct))
                        {
                            if (CStruct* ElemStruct = static_cast<FStructProperty*>(Inner)->GetStruct())
                            {
                                ArrayProperty->ForEach(ArrayPtr, [&](void* Elem, SIZE_T)
                                {
                                    RemapEntityRefsInStruct(ElemStruct, Elem, Map, bClearUnmapped);
                                });
                            }
                        }
                    }
                }
            }
        }
    }

    // --- Reflection / queries + misc ---

    void RemapEntityReferences(FEntityRegistry& Registry, entt::entity Entity, const THashMap<entt::entity, entt::entity>& Map, bool bClearUnmapped)
    {
        using namespace entt::literals;

        for (auto&& [ID, Storage] : Registry.storage())
        {
            if (!Storage.contains(Entity))
            {
                continue;
            }

            entt::meta_type MetaType = entt::resolve(Storage.info());
            if (!MetaType)
            {
                continue;
            }

            entt::meta_any Result = InvokeMetaFunc(MetaType, "static_struct"_hs);
            if (!Result)
            {
                continue;
            }

            if (CStruct* Struct = Result.cast<CStruct*>())
            {
                RemapEntityRefsInStruct(Struct, Storage.value(Entity), Map, bClearUnmapped);
            }
        }
    }

    void SetEntityWorldTransform(FEntityRegistry& Registry, entt::entity Entity, const FTransform& WorldTransform)
    {
        STransformComponent* Transform = Registry.try_get<STransformComponent>(Entity);
        if (Transform == nullptr)
        {
            return;
        }

        FMatrix4 ParentWorldMatrix(1.0f);
        if (const FRelationshipComponent* Relationship = Registry.try_get<FRelationshipComponent>(Entity))
        {
            if (Relationship->Parent != entt::null)
            {
                ParentWorldMatrix = Registry.get<STransformComponent>(Relationship->Parent).GetWorldMatrix();
            }
        }

        FMatrix4 LocalMatrix = Math::Inverse(ParentWorldMatrix) * WorldTransform.GetMatrix();

        FVector3 Translation, Scale, Skew;
        FQuat Rotation;
        FVector4 Perspective;
        Math::Decompose(LocalMatrix, Scale, Rotation, Translation, Skew, Perspective);

        FTransform NewLocal;
        NewLocal.Location = Translation;
        NewLocal.Rotation = Rotation;
        NewLocal.Scale    = Scale;
        Transform->SetLocalTransform(NewLocal);
    }

    void DestroyEntity(FEntityRegistry& Registry, entt::entity Entity)
    {
        Registry.destroy(Entity);
    }

    entt::id_type GetTypeID(FStringView Name)
    {
        return entt::hashed_string(Name.data());
    }

    entt::id_type GetTypeID(const CStruct* Type)
    {
        return entt::hashed_string(Type->GetName().c_str());
    }

    void SetEntityBodyType(FEntityRegistry& Registry, entt::entity Entity)
    {
        Registry.emplace_or_replace<FNeedsPhysicsBodyUpdate>(Entity);
    }

    // Tag a body for the physics sync to reposition it. Single-threaded path (bodies aren't mass-moved).
    void MarkPhysicsBodyDirtyIfBodied(FEntityRegistry& Registry, entt::entity Entity)
    {
        if (Registry.any_of<SRigidBodyComponent, SCharacterPhysicsComponent>(Entity))
        {
            Registry.emplace_or_replace<FNeedsPhysicsBodyUpdate>(Entity);
        }
    }

    // External (non-setter) dirtying. The tag flows through OnTransformDirtied to set the flag. Single-threaded.
    void MarkTransformDirty(FEntityRegistry& Registry, entt::entity Entity)
    {
        Registry.emplace_or_replace<FNeedsTransformUpdate>(Entity);
        MarkPhysicsBodyDirtyIfBodied(Registry, Entity);
    }
}
