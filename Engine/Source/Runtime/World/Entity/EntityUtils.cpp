#include "pch.h"
#include "EntityUtils.h"
#include "Components/CharacterComponent.h"
#include "Components/DirtyComponent.h"
#include "Components/EditorComponent.h"
#include "components/entitytags.h"
#include "Components/PhysicsComponent.h"
#include "Components/RelationshipComponent.h"
#include "Components/ScriptComponent.h"
#include "components/tagcomponent.h"
#include "Components/TransformComponent.h"
#include "Core/Object/Class.h"
#include "Scripting/Lua/ScriptTypes.h"

using namespace entt::literals; 

namespace Lumina::ECS::Utils
{
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
                if (Set.contains(Entity))
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
                    }
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

                // Clear the per-entity error so one corrupt entity does not poison
                // every subsequent SerializeEntity call. The size-header seek below
                // re-aligns the stream regardless.
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
    
    void ReparentEntity(FEntityRegistry& Registry, entt::entity Child, entt::entity Parent)
    {
        // These guards are correctness-critical, not just debug aids: a self-parent or circular
        // hierarchy produces an infinite loop inside ForEachChild / UpdateChildrenRecursive.
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

        #if LE_DEBUG
        if (Parent != entt::null && IsDescendantOf(Registry, Parent, Child))
        {
            LOG_ERROR("Cannot create circular hierarchy - parent is a descendant of child!");
            return;
        }
        #endif
        
        FRelationshipComponent& ChildRelationship = Registry.get_or_emplace<FRelationshipComponent>(Child);
        STransformComponent& ChildTransform = Registry.get<STransformComponent>(Child);
        
        if (ChildRelationship.Parent == Parent)
        {
            return;
        }

        glm::mat4 ChildWorldMatrix = ChildTransform.GetWorldMatrix();
        glm::mat4 ParentWorldMatrix = glm::mat4(1.0f);
        
        if (Parent != entt::null)
        {
            ParentWorldMatrix = Registry.get<STransformComponent>(Parent).GetWorldMatrix();
        }

        glm::mat4 NewLocalMatrix = glm::inverse(ParentWorldMatrix) * ChildWorldMatrix;

        glm::vec3 Translation, Scale, Skew;
        glm::quat Rotation;
        glm::vec4 Perspective;
        glm::decompose(NewLocalMatrix, Scale, Rotation, Translation, Skew, Perspective);

        RemoveFromParent(Registry, Child);

        if (Parent != entt::null)
        {
            AddToParent(Registry, Child, Parent);
        }
        else
        {
            ChildRelationship.Parent = entt::null;
        }
        
        if (Registry.any_of<SDisabledTag>(Parent))
        {
            if (!Registry.any_of<SDisabledTag>(Child))
            {
                Registry.emplace<SDisabledTag>(Child);
            }
        }

        FTransform NewTransform;
        NewTransform.Location   = Translation;
        NewTransform.Rotation   = Rotation;
        NewTransform.Scale      = Scale;
        
        ChildTransform.SetLocalTransform(NewTransform);
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
        
        STransformComponent* TransformComponent = Registry.try_get<STransformComponent>(Child);
        if (!ALERT_IF_NOT(TransformComponent, "Missing child transform component"))
        {
            return;
        }
        
        TransformComponent->SetWorldTransform(TransformComponent->WorldTransform);
        Registry.emplace_or_replace<STransformComponent>(Child, *TransformComponent);
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

    size_t GetChildCount(FEntityRegistry& Registry, entt::entity Parent)
    {
        FRelationshipComponent* Relationship = Registry.try_get<FRelationshipComponent>(Parent);
        return Relationship ? Relationship->Children : 0;
    }

    size_t GetSiblingIndex(FEntityRegistry& Registry, entt::entity Entity)
    {
        FRelationshipComponent* Relationship = Registry.try_get<FRelationshipComponent>(Entity);
        if (!Relationship || Relationship->Parent == entt::null)
        {
            return 0;
        }

        size_t Index = 0;
        entt::entity Current = Registry.get<FRelationshipComponent>(Relationship->Parent).First;

        while (Current != entt::null && Current != Entity)
        {
            Index++;
            FRelationshipComponent* CurrentRelationship = Registry.try_get<FRelationshipComponent>(Current);
            Current = CurrentRelationship ? CurrentRelationship->Next : entt::null;
        }

        return Index;
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

    FRecursiveMutex& GetTransformResolveMutex()
    {
        static FRecursiveMutex Instance;
        return Instance;
    }

    void ResolveTransformChain(FEntityRegistry& Registry, entt::entity Entity)
    {
        LUMINA_PROFILE_SCOPE();

        // Serialize all chain resolves AND any concurrent MarkDirty calls.
        // Multiple workers calling GetWorldMatrix on different entities
        // would otherwise both mutate the FNeedsTransformUpdate pool
        // (erase/remove inside, emplace from MarkDirty) and corrupt its
        // sparse set. Recursive so a resolver that triggers another
        // resolver downstream doesn't deadlock.
        FRecursiveScopeLock Lock(GetTransformResolveMutex());

        // Walk to the root and record the topmost dirty ancestor. Even
        // when Entity itself is clean, a dirty ancestor invalidates our
        // cached world matrix - moving a parent does not mark its
        // descendants dirty, so a query against the descendant must
        // chase the dirty bit upward.
        TFixedVector<entt::entity, 64> AncestorChain;
        int32 TopmostDirtyIndex = -1;

        entt::entity Current = Entity;
        while (Current != entt::null)
        {
            if (Registry.all_of<FNeedsTransformUpdate>(Current))
            {
                TopmostDirtyIndex = (int32)AncestorChain.size();
            }

            AncestorChain.push_back(Current);

            if (!Registry.all_of<FRelationshipComponent>(Current))
            {
                break;
            }

            entt::entity Parent = Registry.get<FRelationshipComponent>(Current).Parent;
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

        // Refresh the chain from the topmost dirty ancestor down to Entity.
        // Each step reads its parent's just-recomputed CachedMatrix, so
        // intermediate clean ancestors get refreshed too (their cache is
        // stale by virtue of having a dirty ancestor above them).
        for (int32 i = TopmostDirtyIndex; i >= 0; --i)
        {
            entt::entity Ancestor = AncestorChain[i];
            auto& Transform = Registry.get<STransformComponent>(Ancestor);

            FRelationshipComponent* Rel = Registry.try_get<FRelationshipComponent>(Ancestor);
            if (Rel && Rel->Parent != entt::null && Registry.valid(Rel->Parent))
            {
                glm::mat4 ParentWorld = Registry.get<STransformComponent>(Rel->Parent).CachedMatrix;
                glm::mat4 Local       = Transform.LocalTransform.GetMatrix();
                Transform.WorldTransform = FTransform(ParentWorld * Local);
            }
            else
            {
                Transform.WorldTransform = Transform.LocalTransform;
            }

            Transform.CachedMatrix = Transform.WorldTransform.GetMatrix();
            Registry.remove<FNeedsTransformUpdate>(Ancestor);
        }

        // Push the new world transforms across the full subtree rooted at
        // the topmost dirty ancestor - sibling branches of Entity also
        // referenced that ancestor's matrix. Skipping them would leave
        // their CachedMatrix stale with no dirty bit to trigger a later
        // resolve, so a subsequent query would return wrong data.
        TFunction<void(entt::entity)> UpdateChildrenRecursive;
        UpdateChildrenRecursive = [&](entt::entity ParentEntity)
        {
            ForEachChild(Registry, ParentEntity, [&](entt::entity Child)
            {
                auto& ParentTransform = Registry.get<STransformComponent>(ParentEntity);
                auto& ChildTransform  = Registry.get<STransformComponent>(Child);

                ChildTransform.WorldTransform = FTransform(ParentTransform.CachedMatrix * ChildTransform.LocalTransform.GetMatrix());
                ChildTransform.CachedMatrix   = ChildTransform.WorldTransform.GetMatrix();

                Registry.remove<FNeedsTransformUpdate>(Child);

                UpdateChildrenRecursive(Child);
            });
        };

        UpdateChildrenRecursive(AncestorChain[TopmostDirtyIndex]);
    }

    void ResolveAllDirtyTransforms(FEntityRegistry& Registry)
    {
        LUMINA_PROFILE_SCOPE();

        // Two passes: parented entities (need parent x local) and roots
        // (world == local). Iterating only FNeedsTransformUpdate keeps the
        // empty-frame cost negligible.
        auto SingleView        = Registry.view<FNeedsTransformUpdate, STransformComponent>(entt::exclude<FRelationshipComponent>);
        auto RelationshipGroup = Registry.group<FNeedsTransformUpdate, FRelationshipComponent>(entt::get<STransformComponent>);

        if (!RelationshipGroup.empty())
        {
            // Snapshot dirty entities before mutating the group; iterating
            // a group while members are removed during the body is unsafe.
            TFixedVector<entt::entity, 100> DirtyEntities;
            DirtyEntities.reserve(RelationshipGroup.size());
            for (auto Entity : RelationshipGroup)
            {
                DirtyEntities.push_back(Entity);
            }

            auto ResolveOne = [&](uint32 Index)
            {
                entt::entity DirtyEntity = DirtyEntities[Index];

                auto& DirtyTransform    = RelationshipGroup.get<STransformComponent>(DirtyEntity);
                auto& DirtyRelationship = RelationshipGroup.get<FRelationshipComponent>(DirtyEntity);

                if (DirtyRelationship.Parent != entt::null && Registry.valid(DirtyRelationship.Parent))
                {
                    glm::mat4 ParentWorld = Registry.get<STransformComponent>(DirtyRelationship.Parent).WorldTransform.GetMatrix();
                    glm::mat4 LocalMat    = DirtyTransform.LocalTransform.GetMatrix();
                    DirtyTransform.WorldTransform = FTransform(ParentWorld * LocalMat);
                }
                else
                {
                    DirtyTransform.WorldTransform = DirtyTransform.LocalTransform;
                }

                DirtyTransform.CachedMatrix = DirtyTransform.WorldTransform.GetMatrix();

                // Push the new world transform down the subtree. A child that
                // was independently dirty also gets refreshed here; clearing
                // FNeedsTransformUpdate at the end handles the dedupe.
                TFunction<void(entt::entity)> UpdateChildrenRecursive;
                UpdateChildrenRecursive = [&](entt::entity ParentEntity)
                {
                    ForEachChild(Registry, ParentEntity, [&](entt::entity Child)
                    {
                        auto& ParentTransform = Registry.get<STransformComponent>(ParentEntity);
                        auto& ChildTransform  = Registry.get<STransformComponent>(Child);

                        glm::mat4 ParentWorld = ParentTransform.WorldTransform.GetMatrix();
                        glm::mat4 ChildLocal  = ChildTransform.LocalTransform.GetMatrix();

                        ChildTransform.WorldTransform = FTransform(ParentWorld * ChildLocal);
                        ChildTransform.CachedMatrix   = ChildTransform.WorldTransform.GetMatrix();

                        UpdateChildrenRecursive(Child);
                    });
                };

                UpdateChildrenRecursive(DirtyEntity);
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
        }

        if (SingleView.size_hint() < 1000)
        {
            SingleView.each([&](STransformComponent& Transform)
            {
                Transform.WorldTransform = Transform.LocalTransform;
                Transform.CachedMatrix   = Transform.WorldTransform.GetMatrix();
            });
        }
        else
        {
            auto WorkFunctor = [](STransformComponent& Transform)
            {
                Transform.WorldTransform = Transform.LocalTransform;
                Transform.CachedMatrix   = Transform.WorldTransform.GetMatrix();
            };

            auto Handle = SingleView.handle();
            Task::ParallelFor(Handle->size(), [&](uint32 Index)
            {
                entt::entity Entity = (*Handle)[Index];

                if (SingleView.contains(Entity))
                {
                    std::apply(WorkFunctor, SingleView.get(Entity));
                }
            });
        }

        Registry.clear<FNeedsTransformUpdate>();
    }

    glm::vec3 GetEntityLocation(FEntityRegistry& Registry, entt::entity Entity)
    {
        auto* Transform = Registry.try_get<STransformComponent>(Entity);
        return Transform ? Transform->GetWorldLocation() : glm::vec3{};    
    }

    glm::quat GetEntityRotation(FEntityRegistry& Registry, entt::entity Entity)
    {
        auto* Transform = Registry.try_get<STransformComponent>(Entity);
        return Transform ? Transform->GetWorldRotation() : glm::quat{};
    }

    glm::vec3 GetEntityScale(FEntityRegistry& Registry, entt::entity Entity)
    {
        auto* Transform = Registry.try_get<STransformComponent>(Entity);
        return Transform ? Transform->GetWorldScale() : glm::vec3{};
    }

    void SetEntityLocation(FEntityRegistry& Registry, entt::entity Entity, const glm::vec3& Location)
    {
        if (auto* Transform = Registry.try_get<STransformComponent>(Entity))
        {
            Transform->SetLocation(Location);
        }    
    }

    void SetEntityRotation(FEntityRegistry& Registry, entt::entity Entity, const glm::quat& Rotation)
    {
        if (auto* Transform = Registry.try_get<STransformComponent>(Entity))
        {
            Transform->SetRotation(Rotation);
        }
    }

    void SetEntityScale(FEntityRegistry& Registry, entt::entity Entity, const glm::vec3& Scale)
    {
        if (auto* Transform = Registry.try_get<STransformComponent>(Entity))
        {
            Transform->SetScale(Scale);
        }
    }

    bool IsEntityValid(FEntityRegistry& Registry, entt::entity Entity)
    {
        return Registry.valid(Entity);
    }

    glm::vec3 TranslateEntity(FEntityRegistry& Registry, entt::entity Entity, const glm::vec3& Translation)
    {
        auto* Transform = Registry.try_get<STransformComponent>(Entity);
        return Transform ? Transform->Translate(Translation) : glm::vec3{};
    }

    entt::entity DuplicateEntity(FEntityRegistry& Registry, entt::entity Entity)
    {
        auto DuplicateRecursive = [&](auto& Self, entt::entity Source, entt::entity NewParent) -> entt::entity
        {
            entt::entity To = Registry.create();

            for (auto&& [ID, Storage] : Registry.storage())
            {
                if (Storage.contains(Source) && !Storage.contains(To))
                {
                    // Scripts can't be bit-copied: their Lua refs point at the source's thread, the shared
                    // FScript would be shared, and self.Entity/self.Transform would resolve to the source.
                    // Re-emplace below with just the editable fields so on_construct re-runs SetupScriptComponent.
                    // SRigidBodyComponent is skipped too: bit-copying carries the source's live BodyID, and it
                    // must be re-emplaced (not copied) so on_construct builds a fresh Jolt body for the duplicate.
                    if (ID == entt::type_hash<FRelationshipComponent>::value()
                        || ID == entt::type_hash<SScriptComponent>::value()
                        || ID == entt::type_hash<SRigidBodyComponent>::value())
                    {
                        continue;
                    }

                    Storage.push(To, Storage.value(Source));
                }
            }

            // Bit-copying STransformComponent carries the source's self-references; rebind so MarkDirty
            // and ResolveIfDirty operate on the duplicate, not the original.
            if (STransformComponent* NewTransform = Registry.try_get<STransformComponent>(To))
            {
                NewTransform->Bind(Registry, To);
                Registry.emplace_or_replace<FNeedsTransformUpdate>(To);
            }

            // Re-emplace SRigidBodyComponent from the source. A collider on_construct hook may have
            // auto-emplaced a default rigid body (and built a default Jolt body) onto the duplicate, so
            // emplace_or_replace here would only fire on_update -- a no-op -- and the body would never be
            // rebuilt with the source's type/profile. Remove that default first so on_destroy tears down
            // any wrong Jolt body, then emplace fresh (with an invalid BodyID) so on_construct builds the body.
            if (const SRigidBodyComponent* SourceBody = Registry.try_get<SRigidBodyComponent>(Source))
            {
                SRigidBodyComponent NewBody = *SourceBody;
                NewBody.BodyID = 0xFFFFFFFF;

                Registry.remove<SRigidBodyComponent>(To);
                Registry.emplace<SRigidBodyComponent>(To, eastl::move(NewBody));
            }

            // Emplace SScriptComponent with the editable fields only; on_construct fires the canonical
            // attach flow which loads a unique FScript and binds fresh Lua refs to the new entity.
            if (const SScriptComponent* SourceScript = Registry.try_get<SScriptComponent>(Source))
            {
                SScriptComponent NewScript;
                NewScript.ScriptPath        = SourceScript->ScriptPath;
                NewScript.PropertyOverrides = SourceScript->PropertyOverrides;
                NewScript.UpdateStage       = SourceScript->UpdateStage;
                NewScript.TickRate          = SourceScript->TickRate;
                Registry.emplace<SScriptComponent>(To, eastl::move(NewScript));
            }

            if (NewParent != entt::null)
            {
                ECS::Utils::ReparentEntity(Registry, To, NewParent);
            }
            else if (FRelationshipComponent* Rel = Registry.try_get<FRelationshipComponent>(Source))
            {
                if (Rel->Parent != entt::null)
                {
                    ECS::Utils::ReparentEntity(Registry, To, Rel->Parent);
                }
            }

            // Recursively duplicate children parented to new duplicate
            ECS::Utils::ForEachChild(Registry, Source, [&](entt::entity Child)
            {
                Self(Self, Child, To);
            });

            return To;
        };

        return DuplicateRecursive(DuplicateRecursive, Entity, entt::null);
    }

    glm::vec3 GetDirectionVector(FEntityRegistry& Registry, entt::entity To, entt::entity From)
    {
        glm::vec3 ToLoc = GetEntityLocation(Registry, To);
        glm::vec3 FromLoc = GetEntityLocation(Registry, From);
        glm::vec3 Direction = glm::normalize(ToLoc - FromLoc);
        return Direction;
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

    entt::id_type GetTypeID(const Lua::FRef& Obj)
    {
        auto Ref = Obj["__type_id"];
        if (Ref.IsValid())
        {
            return Ref.As<entt::id_type>().value();
        }
        
        return entt::id_type{};
    }

    void SetEntityBodyType(FEntityRegistry& Registry, entt::entity Entity)
    {
        Registry.emplace_or_replace<FNeedsPhysicsBodyUpdate>(Entity);
    }

    void MarkTransformDirty(FEntityRegistry& Registry, entt::entity Entity)
    {
        FRecursiveScopeLock Lock(GetTransformResolveMutex());
        Registry.emplace_or_replace<FNeedsTransformUpdate>(Entity);

        if (Registry.any_of<SRigidBodyComponent, SCharacterPhysicsComponent>(Entity))
        {
            Registry.emplace_or_replace<FNeedsPhysicsBodyUpdate>(Entity);
        }
    }
}
