#include "pch.h"
#include "EntityUtils.h"
#include "Components/DirtyComponent.h"
#include "Components/EditorComponent.h"
#include "components/entitytags.h"
#include "Components/RelationshipComponent.h"
#include "components/tagcomponent.h"
#include "Components/TransformComponent.h"
#include "Core/Object/Class.h"
#include "Scripting/Lua/ScriptTypes.h"

using namespace entt::literals; 

namespace Lumina::ECS::Utils
{
    bool SerializeEntity(FArchive& Ar, FEntityRegistry& Registry, entt::entity& Entity)
    {
        using namespace entt::literals;
        
        if (Ar.IsWriting())
        {
            Ar << Entity;

            bool bHasRelationship = false;
            int64 SizeBeforeRelationshipFlag = Ar.Tell();
            Ar << bHasRelationship;

            if (FRelationshipComponent* RelationshipComponent = Registry.try_get<FRelationshipComponent>(Entity))
            {
                bHasRelationship = true;
                
                int64 SizeBefore = Ar.Tell();
                Ar.Seek(SizeBeforeRelationshipFlag);
                Ar << bHasRelationship;
                Ar.Seek(SizeBefore);
                
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
            uint32 EntityID = (uint32)Entity;
            Ar << EntityID;
            Entity = (entt::entity)EntityID;

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

            for (int32 i = 0; i < NumEntitiesSerialized; ++i)
            {
                int64 EntitySaveSize = 0;
                Ar << EntitySaveSize;
        
                int64 PreEntityPos = Ar.Tell();

                entt::entity NewEntity = entt::null;
                bool bSuccess = ECS::Utils::SerializeEntity(Ar, Registry, NewEntity);
                
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
        #if LE_DEBUG
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

        for (auto It = ToDestroy.rbegin(); It != ToDestroy.rend(); ++It)
        {
            if (Registry.valid(*It) && Registry.any_of<FRelationshipComponent>(*It))
            {
                RemoveFromParent(Registry, *It);
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

    void ResolveTransformChain(FEntityRegistry& Registry, entt::entity Entity)
    {
        LUMINA_PROFILE_SCOPE();
        
        TFixedVector<entt::entity, 64> AncestorChain;
        
        entt::entity Current = Entity;
        while (Current != entt::null)
        {
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
            
            if (!Registry.all_of<FNeedsTransformUpdate>(Parent))
            {
                break;
            }
    
            Current = Parent;
        }
    
        for (int32 i = (int32)AncestorChain.size() - 1; i >= 0; --i)
        {
            entt::entity Ancestor = AncestorChain[i];
            auto& Transform = Registry.get<STransformComponent>(Ancestor);
    
            if (Registry.all_of<FRelationshipComponent>(Ancestor))
            {
                entt::entity Parent = Registry.get<FRelationshipComponent>(Ancestor).Parent;
    
                if (Parent != entt::null && Registry.valid(Parent))
                {
                    glm::mat4 ParentWorld = Registry.get<STransformComponent>(Parent).CachedMatrix;
                    glm::mat4 Local       = Transform.LocalTransform.GetMatrix();
                    Transform.WorldTransform = FTransform(ParentWorld * Local);
                }
                else
                {
                    Transform.WorldTransform = Transform.LocalTransform;
                }
            }
            else
            {
                Transform.WorldTransform = Transform.LocalTransform;
            }
    
            Transform.CachedMatrix = Transform.WorldTransform.GetMatrix();
            Registry.erase<FNeedsTransformUpdate>(Ancestor);
        }
        
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
    
        UpdateChildrenRecursive(Entity);
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
            return Ref.Get<entt::id_type>();
        }
        
        return entt::id_type{};
    }

    void SetEntityBodyType(FEntityRegistry& Registry, entt::entity Entity)
    {
        Registry.emplace_or_replace<FNeedsPhysicsBodyUpdate>(Entity);
    }
}
