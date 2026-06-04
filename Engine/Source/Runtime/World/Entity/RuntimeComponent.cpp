#include "pch.h"
#include "RuntimeComponent.h"

#include <algorithm>

#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetTypes/EntityComponent/EntityComponentType.h"
#include "Containers/Array.h"
#include "Core/Object/Class.h"
#include "Core/Object/ObjectArray.h"
#include "Core/Object/ObjectCore.h"
#include "Core/Reflection/PropertyBag/PropertyBag.h"
#include "Core/Reflection/ReflectedTypeAccessors.h"
#include "Core/Reflection/Type/Properties/ObjectProperty.h"
#include "Core/Reflection/Type/Properties/StructProperty.h"
#include "Memory/Memory.h"
#include "Scripting/Lua/Class.h"
#include "Scripting/Lua/Scripting.h"
#include "Scripting/Lua/Stack.h"
#include "Core/Math/Math.h"

namespace Lumina
{
    static int GRuntimeComponentMetatableRef = LUA_NOREF;

    FRuntimeComponentStorage::FRuntimeComponentStorage(const allocator_type& Allocator)
        : base_type(entt::type_id<FDynamicComponentTag>(), entt::deletion_policy::swap_and_pop, Allocator)
    {
    }

    FRuntimeComponentStorage::~FRuntimeComponentStorage()
    {
        if (LayoutBag != nullptr)
        {
            for (std::size_t i = 0, n = base_type::size(); i < n; ++i)
            {
                DestructSlot(SlotAt(i));
            }
        }
        if (Packed != nullptr)
        {
            Memory::Free((void*&)Packed);
            Packed = nullptr;
        }
        if (LayoutBag != nullptr)
        {
            Memory::Delete(LayoutBag);
            LayoutBag = nullptr;
        }
        if (SchemaType != nullptr)
        {
            GObjectArray.ReleaseStrongRef(SchemaType);
            SchemaType = nullptr;
        }
    }

    void FRuntimeComponentStorage::BindLayout(CEntityComponentType* Type)
    {
        SchemaType = Type;
        SchemaGuid = Type->GetGUID();
        GObjectArray.AddStrongRef(Type);

        LayoutBag = Memory::New<FPropertyBag>();
        LayoutBag->SetSchema(Type->GetFields());
        Stride        = LayoutBag->GetBufferSize();
        ElemAlign     = LayoutBag->GetBufferAlignment();
        BoundRevision = Type->GetSchemaRevision();
    }

    void FRuntimeComponentStorage::RefreshSchema()
    {
        if (SchemaType != nullptr && BoundRevision != SchemaType->GetSchemaRevision())
        {
            MigrateTo(SchemaType);
        }
    }

    void FRuntimeComponentStorage::Invalidate()
    {
        base_type::clear();

        if (Packed != nullptr)
        {
            Memory::Free((void*&)Packed);
            Packed = nullptr;
        }
        if (LayoutBag != nullptr)
        {
            Memory::Delete(LayoutBag);
            LayoutBag = nullptr;
        }
        if (SchemaType != nullptr)
        {
            GObjectArray.ReleaseStrongRef(SchemaType);
            SchemaType = nullptr;
        }

        Capacity      = 0;
        Stride        = 0;
        ElemAlign     = 1;
        BoundRevision = 0;
    }

    CStruct* FRuntimeComponentStorage::GetLayout() const
    {
        return LayoutBag ? LayoutBag->GetLayout() : nullptr;
    }

    bool FRuntimeComponentStorage::IsRuntimeStorage(const base_type& Set)
    {
        return Set.info() == entt::type_id<FDynamicComponentTag>();
    }

    const void* FRuntimeComponentStorage::get_at(const std::size_t Pos) const
    {
        return SlotAt(Pos);
    }

    void FRuntimeComponentStorage::EnsureCapacity(uint32 Count)
    {
        if (Stride == 0 || Count <= Capacity)
        {
            return;
        }

        uint32 NewCap = (Capacity == 0) ? 8u : Capacity * 2u;
        NewCap = std::max(NewCap, Count);

        uint8* NewBuf = static_cast<uint8*>(Memory::Malloc((size_t)NewCap * Stride, ElemAlign));

        CStruct* Layout = LayoutBag->GetLayout();
        const TVector<FPropertyBagField>& Fields = LayoutBag->GetSchema();
        for (std::size_t i = 0, Live = base_type::size(); i < Live; ++i)
        {
            uint8* Dst = NewBuf + i * Stride;
            uint8* Src = Packed + i * Stride;
            FPropertyBag::ConstructValueInto(Layout, Fields, Dst);
            FPropertyBag::CopyValueInto(Layout, Fields, Dst, Src);
            FPropertyBag::DestructValueIn(Layout, Fields, Src);
        }

        if (Packed != nullptr)
        {
            Memory::Free((void*&)Packed);
        }
        Packed   = NewBuf;
        Capacity = NewCap;
    }

    void FRuntimeComponentStorage::ConstructFromDefaults(uint8* Slot)
    {
        if (Slot == nullptr || LayoutBag == nullptr)
        {
            return;
        }
        CStruct* Layout = LayoutBag->GetLayout();
        const TVector<FPropertyBagField>& Fields = LayoutBag->GetSchema();
        FPropertyBag::ConstructValueInto(Layout, Fields, Slot);

        const void* Defaults = SchemaType ? SchemaType->GetDefaults() : nullptr;
        if (Defaults != nullptr)
        {
            FPropertyBag::CopyValueInto(Layout, Fields, Slot, Defaults);
        }
    }

    void FRuntimeComponentStorage::DestructSlot(uint8* Slot)
    {
        if (Slot != nullptr && LayoutBag != nullptr)
        {
            FPropertyBag::DestructValueIn(LayoutBag->GetLayout(), LayoutBag->GetSchema(), Slot);
        }
    }

    FRuntimeComponentStorage::base_type::basic_iterator
    FRuntimeComponentStorage::try_emplace(const entt::entity Entity, const bool ForceBack, const void* Value)
    {
        const std::size_t Pos = base_type::size();

        // Value may alias our buffer (e.g. entity duplicate); rebase after EnsureCapacity realloc.
        intptr_t AliasIndex = -1;
        if (Value != nullptr && Stride != 0 && Packed != nullptr)
        {
            const uint8* AsBytes = static_cast<const uint8*>(Value);
            if (AsBytes >= Packed && AsBytes < Packed + (size_t)Capacity * Stride)
            {
                AliasIndex = (AsBytes - Packed) / Stride;
            }
        }

        EnsureCapacity((uint32)Pos + 1u);

        if (AliasIndex >= 0)
        {
            Value = Packed + (size_t)AliasIndex * Stride;
        }

        base_type::basic_iterator It = base_type::try_emplace(Entity, ForceBack, nullptr);

        uint8* Slot = SlotAt(Pos);
        ConstructFromDefaults(Slot);
        if (Value != nullptr && Slot != nullptr)
        {
            FPropertyBag::CopyValueInto(LayoutBag->GetLayout(), LayoutBag->GetSchema(), Slot, Value);
        }
        return It;
    }

    void FRuntimeComponentStorage::pop(base_type::basic_iterator First, base_type::basic_iterator Last)
    {
        const bool bHasElements = (Stride != 0 && LayoutBag != nullptr);

        for (; First != Last; ++First)
        {
            const std::size_t ErasePos = base_type::index(*First);
            const std::size_t LastPos  = base_type::size() - 1u;

            if (bHasElements)
            {
                CStruct* Layout = LayoutBag->GetLayout();
                const TVector<FPropertyBagField>& Fields = LayoutBag->GetSchema();
                uint8* EraseSlot = SlotAt(ErasePos);
                uint8* LastSlot  = SlotAt(LastPos);
                if (ErasePos != LastPos)
                {
                    FPropertyBag::CopyValueInto(Layout, Fields, EraseSlot, LastSlot);
                }
                FPropertyBag::DestructValueIn(Layout, Fields, LastSlot);
            }

            base_type::swap_and_pop(First);
        }
    }

    void FRuntimeComponentStorage::pop_all()
    {
        if (Stride != 0 && LayoutBag != nullptr)
        {
            CStruct* Layout = LayoutBag->GetLayout();
            const TVector<FPropertyBagField>& Fields = LayoutBag->GetSchema();
            for (std::size_t i = 0, n = base_type::size(); i < n; ++i)
            {
                FPropertyBag::DestructValueIn(Layout, Fields, SlotAt(i));
            }
        }
        base_type::pop_all();
    }

    void FRuntimeComponentStorage::swap_or_move(const std::size_t From, const std::size_t To)
    {
        if (Stride == 0 || From == To)
        {
            return;
        }

        CStruct* Layout = LayoutBag->GetLayout();
        const TVector<FPropertyBagField>& Fields = LayoutBag->GetSchema();

        uint8* Tmp = static_cast<uint8*>(Memory::Malloc(Stride, ElemAlign));
        FPropertyBag::ConstructValueInto(Layout, Fields, Tmp);

        uint8* A = SlotAt(From);
        uint8* B = SlotAt(To);
        FPropertyBag::CopyValueInto(Layout, Fields, Tmp, A);
        FPropertyBag::CopyValueInto(Layout, Fields, A, B);
        FPropertyBag::CopyValueInto(Layout, Fields, B, Tmp);

        FPropertyBag::DestructValueIn(Layout, Fields, Tmp);
        Memory::Free((void*&)Tmp);
    }

    void FRuntimeComponentStorage::reserve(const size_type InCapacity)
    {
        base_type::reserve(InCapacity);
        EnsureCapacity((uint32)InCapacity);
    }

    void FRuntimeComponentStorage::MigrateTo(CEntityComponentType* Type)
    {
        FPropertyBag* OldBag    = LayoutBag;
        uint8*        OldPacked = Packed;
        const uint32  OldStride = Stride;

        FPropertyBag* NewBag = Memory::New<FPropertyBag>();
        NewBag->SetSchema(Type->GetFields());
        const uint32 NewStride = NewBag->GetBufferSize();
        const uint32 NewAlign  = NewBag->GetBufferAlignment();

        const std::size_t Count = base_type::size();

        uint8* NewPacked = nullptr;
        uint32 NewCap    = 0;
        if (NewStride > 0 && Count > 0)
        {
            NewCap    = (uint32)Count;
            NewPacked = static_cast<uint8*>(Memory::Malloc((size_t)NewCap * NewStride, NewAlign));
        }

        CStruct* NewLayout = NewBag->GetLayout();
        const TVector<FPropertyBagField>& NewFields = NewBag->GetSchema();
        CStruct* OldLayout = OldBag ? OldBag->GetLayout() : nullptr;
        const TVector<FPropertyBagField>* OldFields = OldBag ? &OldBag->GetSchema() : nullptr;
        const void* Defaults = Type->GetDefaults();

        for (std::size_t i = 0; i < Count; ++i)
        {
            uint8* NewSlot = (NewStride > 0) ? (NewPacked + i * NewStride) : nullptr;
            uint8* OldSlot = (OldStride > 0 && OldPacked) ? (OldPacked + i * OldStride) : nullptr;

            if (NewSlot != nullptr)
            {
                FPropertyBag::ConstructValueInto(NewLayout, NewFields, NewSlot);
                if (Defaults != nullptr)
                {
                    FPropertyBag::CopyValueInto(NewLayout, NewFields, NewSlot, Defaults);
                }

                if (OldLayout != nullptr && OldSlot != nullptr && OldFields != nullptr)
                {
                    for (const FPropertyBagField& NewField : NewFields)
                    {
                        const FPropertyBagField* OldField = nullptr;
                        for (const FPropertyBagField& Cand : *OldFields)
                        {
                            if (Cand.Name == NewField.Name) { OldField = &Cand; break; }
                        }
                        if (OldField == nullptr || OldField->Type != NewField.Type || OldField->TypeName != NewField.TypeName)
                        {
                            continue;
                        }
                        FProperty* NewProp = NewLayout->GetProperty(NewField.Name);
                        FProperty* OldProp = OldLayout->GetProperty(NewField.Name);
                        if (NewProp != nullptr && OldProp != nullptr)
                        {
                            FPropertyBag::CopyFieldValue(NewField.Type, NewProp,
                                NewSlot + NewProp->Offset, OldSlot + OldProp->Offset);
                        }
                    }
                }
            }

            if (OldSlot != nullptr && OldLayout != nullptr && OldFields != nullptr)
            {
                FPropertyBag::DestructValueIn(OldLayout, *OldFields, OldSlot);
            }
        }

        if (OldPacked != nullptr)
        {
            Memory::Free((void*&)OldPacked);
        }
        if (OldBag != nullptr)
        {
            Memory::Delete(OldBag);
        }

        LayoutBag     = NewBag;
        Packed        = NewPacked;
        Capacity      = NewCap;
        Stride        = NewStride;
        ElemAlign     = NewAlign;
        BoundRevision = Type->GetSchemaRevision();
    }

    namespace
    {
        struct FRuntimeComponentRef
        {
            entt::registry* Registry     = nullptr;
            entt::entity    Entity       = entt::null;
            uint32          StorageId    = 0;
            uint32          ByteOffset   = 0;
            CStruct*        StructLayout = nullptr;  // null = top-level component, else nested struct
        };

        THashMap<uint32, CEntityComponentType*>& RuntimeTypeMap()
        {
            static THashMap<uint32, CEntityComponentType*> Map;
            return Map;
        }

        uint16 RuntimeComponentTag()
        {
            return Lua::TClassTraits<FDynamicComponentTag>::Tag();
        }

        FRuntimeComponentStorage* ResolveRefStorage(const FRuntimeComponentRef& Ref, void*& OutData, CStruct*& OutLayout)
        {
            OutData = nullptr;
            OutLayout = nullptr;
            if (Ref.Registry == nullptr)
            {
                return nullptr;
            }
            auto* Set = Ref.Registry->storage(Ref.StorageId);
            if (Set == nullptr || !FRuntimeComponentStorage::IsRuntimeStorage(*Set))
            {
                return nullptr;
            }
            auto* Storage = static_cast<FRuntimeComponentStorage*>(Set);
            if (!Storage->contains(Ref.Entity))
            {
                return nullptr;
            }
            uint8* Base = static_cast<uint8*>(Storage->value(Ref.Entity));
            if (Base == nullptr)
            {
                return nullptr;
            }
            OutData   = Base + Ref.ByteOffset;
            OutLayout = Ref.StructLayout ? Ref.StructLayout : Storage->GetLayout();
            return Storage;
        }

        void PushProxy(lua_State* L, entt::registry* Registry, entt::entity Entity, uint32 StorageId, uint32 ByteOffset, CStruct* StructLayout)
        {
            void* Block = lua_newuserdatatagged(L, sizeof(FRuntimeComponentRef), RuntimeComponentTag());
            new (Block) FRuntimeComponentRef{ Registry, Entity, StorageId, ByteOffset, StructLayout };
            if (GRuntimeComponentMetatableRef != LUA_NOREF)
            {
                lua_getref(L, GRuntimeComponentMetatableRef);
                lua_setmetatable(L, -2);
            }
        }

        int PushPropertyValue(lua_State* L, const FRuntimeComponentRef& ParentRef, void* Data, FProperty* Prop)
        {
            void* V = Prop->GetValuePtr<void>(Data);

            if (Prop->IsA(EPropertyTypeFlags::Bool))   { Lua::TStack<bool>::Push(L, *static_cast<bool*>(V)); return 1; }
            if (Prop->IsA(EPropertyTypeFlags::Int32))  { Lua::TStack<int32>::Push(L, *static_cast<int32*>(V)); return 1; }
            if (Prop->IsA(EPropertyTypeFlags::Int64))  { Lua::TStack<int64>::Push(L, *static_cast<int64*>(V)); return 1; }
            if (Prop->IsA(EPropertyTypeFlags::Float))  { Lua::TStack<float>::Push(L, *static_cast<float*>(V)); return 1; }
            if (Prop->IsA(EPropertyTypeFlags::Double)) { Lua::TStack<double>::Push(L, *static_cast<double*>(V)); return 1; }
            if (Prop->IsA(EPropertyTypeFlags::Name))   { Lua::TStack<FName>::Push(L, *static_cast<FName*>(V)); return 1; }
            if (Prop->IsA(EPropertyTypeFlags::String)) { Lua::TStack<FString>::Push(L, *static_cast<FString*>(V)); return 1; }
            if (Prop->IsA(EPropertyTypeFlags::Enum))   { Lua::TStack<int64>::Push(L, *static_cast<int64*>(V)); return 1; }
            if (Prop->IsA(EPropertyTypeFlags::Object)) { Lua::PushCObjectAsActualType(L, static_cast<TObjectPtr<CObject>*>(V)->Get()); return 1; }
            if (Prop->IsA(EPropertyTypeFlags::Struct))
            {
                CStruct* S = static_cast<FStructProperty*>(Prop)->GetStruct();
                if (S == TBaseStructure<FVector2>::Get()) { Lua::TStack<FVector2>::Push(L, *static_cast<FVector2*>(V)); return 1; }
                if (S == TBaseStructure<FVector3>::Get()) { Lua::TStack<FVector3>::Push(L, *static_cast<FVector3*>(V)); return 1; }
                if (S == TBaseStructure<FVector4>::Get()) { Lua::TStack<FVector4>::Push(L, *static_cast<FVector4*>(V)); return 1; }
                if (S != nullptr)
                {
                    PushProxy(L, ParentRef.Registry, ParentRef.Entity, ParentRef.StorageId, ParentRef.ByteOffset + Prop->Offset, S);
                    return 1;
                }
            }

            lua_pushnil(L);
            return 1;
        }

        void SetPropertyValue(lua_State* L, void* Data, FProperty* Prop, int ValueIndex)
        {
            void* V = Prop->GetValuePtr<void>(Data);

            if (Prop->IsA(EPropertyTypeFlags::Bool))   { *static_cast<bool*>(V)   = Lua::TStack<bool>::Get(L, ValueIndex); return; }
            if (Prop->IsA(EPropertyTypeFlags::Int32))  { *static_cast<int32*>(V)  = Lua::TStack<int32>::Get(L, ValueIndex); return; }
            if (Prop->IsA(EPropertyTypeFlags::Int64))  { *static_cast<int64*>(V)  = Lua::TStack<int64>::Get(L, ValueIndex); return; }
            if (Prop->IsA(EPropertyTypeFlags::Float))  { *static_cast<float*>(V)  = Lua::TStack<float>::Get(L, ValueIndex); return; }
            if (Prop->IsA(EPropertyTypeFlags::Double)) { *static_cast<double*>(V) = Lua::TStack<double>::Get(L, ValueIndex); return; }
            if (Prop->IsA(EPropertyTypeFlags::Name))   { *static_cast<FName*>(V)  = Lua::TStack<FName>::Get(L, ValueIndex); return; }
            if (Prop->IsA(EPropertyTypeFlags::String)) { *static_cast<FString*>(V) = Lua::TStack<FString>::Get(L, ValueIndex); return; }
            if (Prop->IsA(EPropertyTypeFlags::Enum))   { *static_cast<int64*>(V)  = Lua::TStack<int64>::Get(L, ValueIndex); return; }
            if (Prop->IsA(EPropertyTypeFlags::Object)) { *static_cast<TObjectPtr<CObject>*>(V) = Lua::TStack<CObject*>::Get(L, ValueIndex); return; }
            if (Prop->IsA(EPropertyTypeFlags::Struct))
            {
                CStruct* S = static_cast<FStructProperty*>(Prop)->GetStruct();
                if (S == TBaseStructure<FVector2>::Get()) { *static_cast<FVector2*>(V) = Lua::TStack<FVector2>::Get(L, ValueIndex); return; }
                if (S == TBaseStructure<FVector3>::Get()) { *static_cast<FVector3*>(V) = Lua::TStack<FVector3>::Get(L, ValueIndex); return; }
                if (S == TBaseStructure<FVector4>::Get()) { *static_cast<FVector4*>(V) = Lua::TStack<FVector4>::Get(L, ValueIndex); return; }

                if (S != nullptr)
                {
                    if (auto* Src = static_cast<FRuntimeComponentRef*>(lua_touserdatatagged(L, ValueIndex, RuntimeComponentTag())))
                    {
                        void* SrcData = nullptr;
                        CStruct* SrcLayout = nullptr;
                        if (ResolveRefStorage(*Src, SrcData, SrcLayout) && SrcLayout == S && SrcData != nullptr)
                        {
                            if (FStructOps* Ops = S->GetStructOps(); Ops != nullptr && Ops->HasCopy())
                            {
                                Ops->Copy(V, SrcData);
                            }
                            else
                            {
                                memcpy(V, SrcData, S->GetSize());
                            }
                        }
                    }
                }
            }
        }

        int RuntimeComponent_Index(lua_State* L)
        {
            auto* Ref = static_cast<FRuntimeComponentRef*>(lua_touserdatatagged(L, 1, RuntimeComponentTag()));
            const char* Key = lua_tostring(L, 2);
            void* Data = nullptr;
            CStruct* Layout = nullptr;
            if (Ref == nullptr || Key == nullptr || !ResolveRefStorage(*Ref, Data, Layout) || Layout == nullptr)
            {
                lua_pushnil(L);
                return 1;
            }
            if (FProperty* Prop = Layout->GetProperty(FName(Key)))
            {
                return PushPropertyValue(L, *Ref, Data, Prop);
            }
            lua_pushnil(L);
            return 1;
        }

        int RuntimeComponent_Newindex(lua_State* L)
        {
            auto* Ref = static_cast<FRuntimeComponentRef*>(lua_touserdatatagged(L, 1, RuntimeComponentTag()));
            const char* Key = lua_tostring(L, 2);
            void* Data = nullptr;
            CStruct* Layout = nullptr;
            if (Ref == nullptr || Key == nullptr || !ResolveRefStorage(*Ref, Data, Layout) || Layout == nullptr)
            {
                return 0;
            }
            if (FProperty* Prop = Layout->GetProperty(FName(Key)))
            {
                SetPropertyValue(L, Data, Prop, 3);
            }
            return 0;
        }
    }

    namespace
    {
        int RuntimeComponent_ToString(lua_State* L)
        {
            auto* Ref = static_cast<FRuntimeComponentRef*>(lua_touserdatatagged(L, 1, RuntimeComponentTag()));
            if (Ref != nullptr)
            {
                const char* Name = Ref->StructLayout ? Ref->StructLayout->GetName().c_str() : nullptr;
                if (Name == nullptr)
                {
                    if (CEntityComponentType* Type = ResolveRuntimeComponentType(Ref->StorageId))
                    {
                        Name = Type->GetName().c_str();
                    }
                }
                const FString Label = FString("RuntimeComponent<") + (Name ? Name : "?") + ">";
                lua_pushlstring(L, Label.c_str(), Label.size());
                return 1;
            }
            lua_pushstring(L, "RuntimeComponent");
            return 1;
        }
    }

    void RegisterRuntimeComponentMetatable(lua_State* L)
    {
        if (L == nullptr)
        {
            return;
        }
        lua_newtable(L);
        lua_pushcfunction(L, &RuntimeComponent_Index, "__index");
        lua_rawsetfield(L, -2, "__index");
        lua_pushcfunction(L, &RuntimeComponent_Newindex, "__newindex");
        lua_rawsetfield(L, -2, "__newindex");
        lua_pushcfunction(L, &RuntimeComponent_ToString, "__tostring");
        lua_rawsetfield(L, -2, "__tostring");

        if (GRuntimeComponentMetatableRef != LUA_NOREF)
        {
            lua_unref(L, GRuntimeComponentMetatableRef);
        }
        GRuntimeComponentMetatableRef = lua_ref(L, -1);
        lua_pop(L, 1);
    }

    void PushRuntimeComponent(lua_State* L, entt::registry* Registry, entt::entity Entity, uint32 StorageId)
    {
        PushProxy(L, Registry, Entity, StorageId, 0, nullptr);
    }

    void RegisterRuntimeComponentTypeGlobal(lua_State* L, CEntityComponentType* Type)
    {
        if (L == nullptr || Type == nullptr)
        {
            return;
        }
        const uint32 Id = Type->GetStorageId();
        RuntimeTypeMap()[Id] = Type;

        lua_newtable(L);
        lua_pushunsigned(L, Id);
        lua_rawsetfield(L, -2, "__type_id");
        lua_pushstring(L, Type->GetName().c_str());
        lua_rawsetfield(L, -2, "__typename");
        lua_pushboolean(L, 1);
        lua_rawsetfield(L, -2, "__runtime_component");
        lua_setglobal(L, Type->GetName().c_str());

        Lua::FScriptingContext::Get().OnGlobalsChanged.Broadcast();
    }

    void UnregisterRuntimeComponentTypeGlobal(lua_State* L, const FGuid& TypeGuid)
    {
        const uint32 Id = static_cast<uint32>(TypeGuid.Hash());
        auto It = RuntimeTypeMap().find(Id);
        if (It == RuntimeTypeMap().end())
        {
            return;
        }
        if (L != nullptr && It->second != nullptr)
        {
            lua_pushnil(L);
            lua_setglobal(L, It->second->GetName().c_str());
        }
        RuntimeTypeMap().erase(It);

        if (L != nullptr)
        {
            Lua::FScriptingContext::Get().OnGlobalsChanged.Broadcast();
        }
    }

    void RegisterAllRuntimeComponentTypeGlobals(lua_State* L)
    {
        if (L == nullptr)
        {
            return;
        }
        TVector<FAssetData*> Types = FAssetRegistry::Get().FindByPredicate([](const FAssetData& Data)
        {
            CClass* DataClass = FindObject<CClass>(Data.AssetClass);
            return DataClass != nullptr && DataClass->IsChildOf(CEntityComponentType::StaticClass());
        });

        for (const FAssetData* Data : Types)
        {
            if (CObject* Obj = LoadObject<CObject>(Data->AssetGUID))
            {
                if (Obj->IsA<CEntityComponentType>())
                {
                    RegisterRuntimeComponentTypeGlobal(L, static_cast<CEntityComponentType*>(Obj));
                }
            }
        }
    }

    CEntityComponentType* ResolveRuntimeComponentType(uint32 StorageId)
    {
        auto It = RuntimeTypeMap().find(StorageId);
        return (It != RuntimeTypeMap().end()) ? It->second : nullptr;
    }
}
