#include "pch.h"
#include "EntityUtils.h"
#include "Components/CharacterComponent.h"
#include "Components/DirtyComponent.h"
#include "Components/EditorComponent.h"
#include "components/entitytags.h"
#include "Components/PhysicsComponent.h"
#include "Components/NameComponent.h"
#include "Components/RelationshipComponent.h"
#include "Components/ScriptComponent.h"
#include "components/tagcomponent.h"
#include "Components/TransformComponent.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetTypes/EntityComponent/EntityComponentType.h"
#include "Core/Object/Class.h"
#include "Core/Object/ObjectArray.h"
#include "Core/Object/Package/Package.h"
#include "Core/Reflection/Type/LuminaTypes.h"
#include "Core/Reflection/Type/Properties/ArrayProperty.h"
#include "Core/Reflection/Type/Properties/StructProperty.h"
#include "RuntimeComponent.h"
#include "Scripting/Lua/ScriptTypes.h"
#include "Scripting/Lua/Scripting.h"
#include "World/World.h"

using namespace entt::literals; 

namespace Lumina::ECS::Utils
{
    // '@' prefix ensures no collision with reflected CStruct names. Lazy to avoid static-init order issues.
    static const FName& RuntimeComponentTypeName()
    {
        static const FName Name("@RuntimeComponent");
        return Name;
    }

    namespace
    {
        void OnComponentTypePackageDeleted(FName Path)
        {
            const FAssetData* Data = FAssetRegistry::Get().GetAssetByPath(Path.c_str());
            if (Data == nullptr)
            {
                return;
            }

            const FGuid DeletedGuid = Data->AssetGUID;
            const uint32 StorageId = static_cast<uint32>(DeletedGuid.Hash());

            UnregisterRuntimeComponentTypeGlobal(Lua::FScriptingContext::Get().GetVM(), DeletedGuid);

            GObjectArray.ForEachObject([&](CObjectBase* Object, int32)
            {
                if (Object == nullptr || !Object->IsA<CWorld>())
                {
                    return;
                }

                CWorld* World = static_cast<CWorld*>(Object);
                FRuntimeComponentStorage* Storage = FindRuntimeStorageById(World->GetEntityRegistry(), StorageId);
                if (Storage != nullptr && Storage->GetSchemaGuid() == DeletedGuid)
                {
                    Storage->Invalidate();
                    if (World->GetPackage() != nullptr)
                    {
                        World->GetPackage()->MarkDirty();
                    }
                }
            });
        }
    }

    FRuntimeComponentStorage& GetOrCreateRuntimeStorage(FEntityRegistry& Registry, CEntityComponentType* Type)
    {
        static const FDelegateHandle DeleteHook = CPackage::OnPackageDestroyed.AddStatic(&OnComponentTypePackageDeleted);
        (void)DeleteHook;

        FRuntimeComponentStorage& Storage = Registry.storage<FDynamicComponentTag>(Type->GetStorageId());
        if (!Storage.IsBound())
        {
            Storage.BindLayout(Type);
        }
        else
        {
            Storage.RefreshSchema();
        }
        return Storage;
    }

    FRuntimeComponentStorage* FindRuntimeStorage(FEntityRegistry& Registry, CEntityComponentType* Type)
    {
        return FindRuntimeStorageById(Registry, Type->GetStorageId());
    }

    FRuntimeComponentStorage* FindRuntimeStorageById(FEntityRegistry& Registry, uint32 StorageId)
    {
        if (auto* Set = Registry.storage(StorageId))
        {
            if (FRuntimeComponentStorage::IsRuntimeStorage(*Set))
            {
                return static_cast<FRuntimeComponentStorage*>(Set);
            }
        }
        return nullptr;
    }

    void* AddRuntimeComponent(FEntityRegistry& Registry, entt::entity Entity, CEntityComponentType* Type)
    {
        FRuntimeComponentStorage& Storage = GetOrCreateRuntimeStorage(Registry, Type);
        if (!Storage.contains(Entity))
        {
            Storage.push(Entity);
        }
        return Storage.value(Entity);
    }

    bool RemoveRuntimeComponent(FEntityRegistry& Registry, entt::entity Entity, CEntityComponentType* Type)
    {
        if (FRuntimeComponentStorage* Storage = FindRuntimeStorage(Registry, Type))
        {
            return Storage->remove(Entity);
        }
        return false;
    }

    void* GetRuntimeComponent(FEntityRegistry& Registry, entt::entity Entity, CEntityComponentType* Type)
    {
        FRuntimeComponentStorage* Storage = FindRuntimeStorage(Registry, Type);
        return (Storage != nullptr && Storage->contains(Entity)) ? Storage->value(Entity) : nullptr;
    }

    bool HasRuntimeComponent(FEntityRegistry& Registry, entt::entity Entity, CEntityComponentType* Type)
    {
        FRuntimeComponentStorage* Storage = FindRuntimeStorage(Registry, Type);
        return Storage != nullptr && Storage->contains(Entity);
    }

    void RefreshRuntimeComponentSchemas(FEntityRegistry& Registry)
    {
        for (auto&& [Id, Set] : Registry.storage())
        {
            if (FRuntimeComponentStorage::IsRuntimeStorage(Set))
            {
                static_cast<FRuntimeComponentStorage&>(Set).RefreshSchema();
            }
        }
    }

    void RefreshAllWorldsRuntimeComponentSchemas()
    {
        GObjectArray.ForEachObject([](CObjectBase* Object, int32)
        {
            if (Object != nullptr && Object->IsA<CWorld>())
            {
                RefreshRuntimeComponentSchemas(static_cast<CWorld*>(Object)->GetEntityRegistry());
            }
        });
    }

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

                if (FRuntimeComponentStorage::IsRuntimeStorage(Set))
                {
                    FRuntimeComponentStorage& RuntimeStorage = static_cast<FRuntimeComponentStorage&>(Set);
                    CEntityComponentType* Type = RuntimeStorage.GetSchemaType();
                    if (Type == nullptr)
                    {
                        continue;
                    }

                    FName Name = RuntimeComponentTypeName();
                    Ar << Name;

                    int64 ComponentStart = Ar.Tell();
                    int64 ComponentSize = 0;
                    Ar << ComponentSize;
                    int64 StartOfComponentData = Ar.Tell();

                    CObject* SchemaObject = Type;
                    Ar << SchemaObject;
                    if (CStruct* Layout = RuntimeStorage.GetLayout())
                    {
                        Layout->SerializeTaggedProperties(Ar, RuntimeStorage.value(Entity));
                    }

                    int64 EndOfComponentData = Ar.Tell();
                    ComponentSize = EndOfComponentData - StartOfComponentData;
                    Ar.Seek(ComponentStart);
                    Ar << ComponentSize;
                    Ar.Seek(EndOfComponentData);

                    NumComponents++;
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

                if (TypeName == RuntimeComponentTypeName())
                {
                    CObject* SchemaObject = nullptr;
                    Ar << SchemaObject;

                    CEntityComponentType* Type = (SchemaObject != nullptr && SchemaObject->IsA<CEntityComponentType>())
                        ? static_cast<CEntityComponentType*>(SchemaObject) : nullptr;

                    if (Type != nullptr)
                    {
                        FRuntimeComponentStorage& Storage = GetOrCreateRuntimeStorage(Registry, Type);
                        if (!Storage.contains(Entity))
                        {
                            Storage.push(Entity);
                        }
                        if (CStruct* Layout = Storage.GetLayout())
                        {
                            Layout->SerializeTaggedProperties(Ar, Storage.value(Entity));
                        }
                    }
                }
                else if (CStruct* Struct = FindObject<CStruct>(TypeName))
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

        FMatrix4 ChildWorldMatrix = ChildTransform.GetWorldMatrix();
        FMatrix4 ParentWorldMatrix = FMatrix4(1.0f);
        
        if (Parent != entt::null)
        {
            ParentWorldMatrix = Registry.get<STransformComponent>(Parent).GetWorldMatrix();
        }

        FMatrix4 NewLocalMatrix = Math::Inverse(ParentWorldMatrix) * ChildWorldMatrix;

        FVector3 Translation, Scale, Skew;
        FQuat Rotation;
        FVector4 Perspective;
        Math::Decompose(NewLocalMatrix, Scale, Rotation, Translation, Skew, Perspective);

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

    entt::entity GetParent(FEntityRegistry& Registry, entt::entity Entity)
    {
        FRelationshipComponent* Relationship = Registry.try_get<FRelationshipComponent>(Entity);
        return Relationship ? Relationship->Parent : entt::null;
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

    void CollectAncestors(FEntityRegistry& Registry, entt::entity Entity, TVector<entt::entity>& OutAncestors)
    {
        ForEachAncestor(Registry, Entity, [&](entt::entity Ancestor)
        {
            OutAncestors.push_back(Ancestor);
        });
    }

    entt::entity FindChildByName(FEntityRegistry& Registry, entt::entity Parent, const FName& Name)
    {
        entt::entity Found = entt::null;
        ForEachChild(Registry, Parent, [&](entt::entity Child)
        {
            if (Found != entt::null)
            {
                return;
            }

            if (const SNameComponent* NameComponent = Registry.try_get<SNameComponent>(Child))
            {
                if (NameComponent->Name == Name)
                {
                    Found = Child;
                }
            }
        });

        return Found;
    }

    entt::entity FindDescendantByName(FEntityRegistry& Registry, entt::entity Parent, const FName& Name)
    {
        entt::entity Found = entt::null;
        ForEachDescendant(Registry, Parent, [&](entt::entity Descendant)
        {
            if (Found != entt::null)
            {
                return;
            }

            if (const SNameComponent* NameComponent = Registry.try_get<SNameComponent>(Descendant))
            {
                if (NameComponent->Name == Name)
                {
                    Found = Descendant;
                }
            }
        });

        return Found;
    }

    entt::entity GetNextSibling(FEntityRegistry& Registry, entt::entity Entity)
    {
        FRelationshipComponent* Relationship = Registry.try_get<FRelationshipComponent>(Entity);
        return Relationship ? Relationship->Next : entt::null;
    }

    entt::entity GetPrevSibling(FEntityRegistry& Registry, entt::entity Entity)
    {
        FRelationshipComponent* Relationship = Registry.try_get<FRelationshipComponent>(Entity);
        return Relationship ? Relationship->Prev : entt::null;
    }

    void CollectSiblings(FEntityRegistry& Registry, entt::entity Entity, TVector<entt::entity>& OutSiblings)
    {
        FRelationshipComponent* Relationship = Registry.try_get<FRelationshipComponent>(Entity);
        if (!Relationship || Relationship->Parent == entt::null)
        {
            return;
        }

        ForEachChild(Registry, Relationship->Parent, [&](entt::entity Sibling)
        {
            if (Sibling != Entity)
            {
                OutSiblings.push_back(Sibling);
            }
        });
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

        // Recursive mutex: concurrent MarkDirty + GetWorldMatrix would corrupt the sparse set; resolver may recurse.
        FRecursiveScopeLock Lock(GetTransformResolveMutex());

        // Chase dirty bit up to root; a dirty ancestor invalidates descendants even when they are clean.
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

        for (int32 i = TopmostDirtyIndex; i >= 0; --i)
        {
            entt::entity Ancestor = AncestorChain[i];
            auto& Transform = Registry.get<STransformComponent>(Ancestor);

            FRelationshipComponent* Rel = Registry.try_get<FRelationshipComponent>(Ancestor);
            if (Rel && Rel->Parent != entt::null && Registry.valid(Rel->Parent))
            {
                FMatrix4 ParentWorld = Registry.get<STransformComponent>(Rel->Parent).CachedMatrix;
                FMatrix4 Local       = Transform.LocalTransform.GetMatrix();
                Transform.WorldTransform = FTransform(ParentWorld * Local);
            }
            else
            {
                Transform.WorldTransform = Transform.LocalTransform;
            }

            Transform.CachedMatrix = Transform.WorldTransform.GetMatrix();
            Registry.remove<FNeedsTransformUpdate>(Ancestor);
        }

        // Propagate down the full subtree: sibling branches also referenced the ancestor's matrix.
        // Every entity owns a transform, so resolve the storage once and index it directly rather
        // than paying a pool-map lookup per node via Registry.get.
        auto& TransformStorage = Registry.storage<STransformComponent>();
        TFunction<void(entt::entity)> UpdateChildrenRecursive;
        UpdateChildrenRecursive = [&](entt::entity ParentEntity)
        {
            const FMatrix4 ParentMatrix = TransformStorage.get(ParentEntity).CachedMatrix;
            ForEachChild(Registry, ParentEntity, [&](entt::entity Child)
            {
                auto& ChildTransform = TransformStorage.get(Child);

                ChildTransform.WorldTransform = FTransform(ParentMatrix * ChildTransform.LocalTransform.GetMatrix());
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

        auto SingleView        = Registry.view<FNeedsTransformUpdate, STransformComponent>(entt::exclude<FRelationshipComponent>);
        auto RelationshipGroup = Registry.group<FNeedsTransformUpdate, FRelationshipComponent>(entt::get<STransformComponent>);

        // Every entity owns a transform; resolve the pool once so the parallel walk below indexes it
        // directly instead of re-resolving via Registry.get (which would also lazily assure under threads).
        auto& TransformStorage = Registry.storage<STransformComponent>();

        if (!RelationshipGroup.empty())
        {
            // Snapshot before iterating: removing members during iteration is unsafe.
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
                    FMatrix4 ParentWorld = TransformStorage.get(DirtyRelationship.Parent).WorldTransform.GetMatrix();
                    FMatrix4 LocalMat    = DirtyTransform.LocalTransform.GetMatrix();
                    DirtyTransform.WorldTransform = FTransform(ParentWorld * LocalMat);
                }
                else
                {
                    DirtyTransform.WorldTransform = DirtyTransform.LocalTransform;
                }

                DirtyTransform.CachedMatrix = DirtyTransform.WorldTransform.GetMatrix();

                TFunction<void(entt::entity)> UpdateChildrenRecursive;
                UpdateChildrenRecursive = [&](entt::entity ParentEntity)
                {
                    const FMatrix4 ParentWorld = TransformStorage.get(ParentEntity).WorldTransform.GetMatrix();
                    ForEachChild(Registry, ParentEntity, [&](entt::entity Child)
                    {
                        auto& ChildTransform = TransformStorage.get(Child);

                        FMatrix4 ChildLocal = ChildTransform.LocalTransform.GetMatrix();

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

    FVector3 GetEntityLocation(FEntityRegistry& Registry, entt::entity Entity)
    {
        auto* Transform = Registry.try_get<STransformComponent>(Entity);
        return Transform ? Transform->GetWorldLocation() : FVector3{};    
    }

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

    void SetEntityLocation(FEntityRegistry& Registry, entt::entity Entity, const FVector3& Location)
    {
        if (auto* Transform = Registry.try_get<STransformComponent>(Entity))
        {
            Transform->SetLocation(Location);
        }    
    }

    void SetEntityRotation(FEntityRegistry& Registry, entt::entity Entity, const FQuat& Rotation)
    {
        if (auto* Transform = Registry.try_get<STransformComponent>(Entity))
        {
            Transform->SetRotation(Rotation);
        }
    }

    void SetEntityScale(FEntityRegistry& Registry, entt::entity Entity, const FVector3& Scale)
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

    FVector3 TranslateEntity(FEntityRegistry& Registry, entt::entity Entity, const FVector3& Translation)
    {
        auto* Transform = Registry.try_get<STransformComponent>(Entity);
        return Transform ? Transform->Translate(Translation) : FVector3{};
    }

    entt::entity DuplicateEntity(FEntityRegistry& Registry, entt::entity Entity)
    {
        THashMap<entt::entity, entt::entity> SourceToDuplicate;

        auto DuplicateRecursive = [&](auto& Self, entt::entity Source, entt::entity NewParent) -> entt::entity
        {
            entt::entity To = Registry.create();
            SourceToDuplicate[Source] = To;

            for (auto&& [ID, Storage] : Registry.storage())
            {
                if (Storage.contains(Source) && !Storage.contains(To))
                {
                        // Scripts/rigid bodies can't be bit-copied; re-emplaced below so on_construct fires fresh.
                    if (ID == entt::type_hash<FRelationshipComponent>::value()
                        || ID == entt::type_hash<SScriptComponent>::value()
                        || ID == entt::type_hash<SRigidBodyComponent>::value())
                    {
                        continue;
                    }

                    Storage.push(To, Storage.value(Source));
                }
            }

            // Rebind: bit-copy carries source's self-references (Entity/Registry ptr).
            if (STransformComponent* NewTransform = Registry.try_get<STransformComponent>(To))
            {
                NewTransform->Bind(Registry, To);
                Registry.emplace_or_replace<FNeedsTransformUpdate>(To);
            }

            // Remove auto-emplaced default first; emplace_or_replace would fire on_update (no-op), not on_construct.
            if (const SRigidBodyComponent* SourceBody = Registry.try_get<SRigidBodyComponent>(Source))
            {
                SRigidBodyComponent NewBody = *SourceBody;
                NewBody.BodyID = 0xFFFFFFFF;

                Registry.remove<SRigidBodyComponent>(To);
                Registry.emplace<SRigidBodyComponent>(To, eastl::move(NewBody));
            }

            // Emplace editable fields only; on_construct loads a unique FScript for the duplicate.
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

        entt::entity Root = DuplicateRecursive(DuplicateRecursive, Entity, entt::null);

        // Fix up entity-handle properties so references between duplicated entities point at the
        // new copies; references to entities outside the duplicated set are left untouched.
        for (auto& [Source, Dup] : SourceToDuplicate)
        {
            RemapEntityReferences(Registry, Dup, SourceToDuplicate, /*bClearUnmapped*/ false);
        }

        return Root;
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

    FVector3 GetDirectionVector(FEntityRegistry& Registry, entt::entity To, entt::entity From)
    {
        FVector3 ToLoc = GetEntityLocation(Registry, To);
        FVector3 FromLoc = GetEntityLocation(Registry, From);
        FVector3 Direction = Math::Normalize(ToLoc - FromLoc);
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
        // Component handles are tables carrying __type_id. Guard against a nil/non-table ref (e.g. an
        // unregistered global) -- indexing a nil value would raise a Lua error and longjmp through the
        // C++ binding frame (the luaL_tolstring crash). A non-handle ref simply has no type id.
        if (!Obj.IsTable())
        {
            return entt::id_type{};
        }
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
