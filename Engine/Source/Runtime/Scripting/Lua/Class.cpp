#include "pch.h"
#include "Class.h"
#include "Core/Math/Hash/Hash.h"
#include "Core/Object/Class.h"
#include "Core/Object/Object.h"
#include "lua.h"
#include "lualib.h"

namespace Lumina::Lua
{
    FClassBuilder::FClassBuilder(lua_State* InL, FStringView InName)
        : L(InL)
        , Name(InName)
    {
        luaL_newmetatable(L, InName.data()); // [MT]

        lua_pushstring(L, InName.data());
        lua_rawsetfield(L, -2, "__typename");
    }

    FClassBuilder& FClassBuilder::SetSuperClass(FStringView InParentName)
    {
        ParentName = InParentName;
        return *this;
    }

    FClassBuilder& FClassBuilder::EnableTypeId()
    {
        bHasTypeId = true;
        return *this;
    }

    FClassBuilder& FClassBuilder::AddMethod(FStringView FuncName, lua_CFunction Func)
    {
        FMethodEntry Entry;
        Entry.Name   = FuncName;
        Entry.Invoke = Func;
        Methods.push_back(Entry);
        return *this;
    }

    FClassBuilder& FClassBuilder::AddProperty(FStringView PropName, lua_CFunction Getter, lua_CFunction Setter)
    {
        FPropertyEntry Entry;
        Entry.Name   = PropName;
        Entry.Getter = Getter;
        Entry.Setter = Setter;
        Properties.push_back(Entry);
        return *this;
    }

    FClassBuilder& FClassBuilder::AddMetamethod(FStringView MetaName, lua_CFunction Func)
    {
        // Stack on entry: [MT]
        lua_pushcfunction(L, Func, MetaName.data()); // [MT, func]
        lua_rawsetfield(L, -2, MetaName.data());     // [MT]
        return *this;
    }

    FClassBuilder& FClassBuilder::Register(int UserdataTag)
    {
        // Stack on entry: [MT]

        const uint32 TypeIdHash = bHasTypeId ? Hash::FNV1a::GetHash32(Name.data()) : 0u;

        for (auto& Method : Methods)
        {
            Method.Atom = static_cast<int16>(Hash::FNV1a::GetHash16(Method.Name.data()));
        }
        for (auto& Prop   : Properties)
        {
            Prop.Hash   = Hash::FNV1a::GetHash32(Prop.Name.data());
        }

        // Single hop captures the full ancestry; parent already merged its own.
        TVector<FMethodEntry>   MergedMethods = Methods;
        TVector<FPropertyEntry> MergedProps   = Properties;

        if (!ParentName.empty())
        {
            luaL_getmetatable(L, ParentName.data()); // [MT, ParentMT|nil]
            if (lua_istable(L, -1))
            {
                lua_rawgetfield(L, -1, "__lumina_methods"); // [MT, ParentMT, ParentMethods|nil]
                if (lua_isuserdata(L, -1))
                {
                    const auto* ParentTable = static_cast<const Internal::FEntryTable<FMethodEntry>*>(lua_touserdata(L, -1));
                    const FMethodEntry* B = ParentTable->Entries();
                    const FMethodEntry* E = B + ParentTable->Count;
                    for (const FMethodEntry* It = B; It != E; ++It)
                    {
                        const bool bExists = eastl::any_of(MergedMethods.begin(), MergedMethods.end(),[&](const FMethodEntry& Existing)
                        {
                            return Existing.Name == It->Name;
                        });
                        
                        if (!bExists)
                        {
                            MergedMethods.push_back(*It);
                        }
                    }
                }
                lua_pop(L, 1); // [MT, ParentMT]

                lua_rawgetfield(L, -1, "__lumina_properties"); // [MT, ParentMT, ParentProps|nil]
                if (lua_isuserdata(L, -1))
                {
                    const auto* ParentTable = static_cast<const Internal::FEntryTable<FPropertyEntry>*>(lua_touserdata(L, -1));
                    const FPropertyEntry* B = ParentTable->Entries();
                    const FPropertyEntry* E = B + ParentTable->Count;
                    for (const FPropertyEntry* It = B; It != E; ++It)
                    {
                        const bool bExists = eastl::any_of(MergedProps.begin(), MergedProps.end(), [&](const FPropertyEntry& Existing)
                        {
                            return Existing.Name == It->Name;
                        });
                        
                        if (!bExists)
                        {
                            MergedProps.push_back(*It);
                        }
                    }
                }
                lua_pop(L, 1); // [MT, ParentMT]
            }
            lua_pop(L, 1); // [MT]
        }

        if (bHasTypeId)
        {
            // Synthetic __type_id reads from MT at access time; keeps the dispatcher upvalue free.
            FPropertyEntry TypeIdProp;
            TypeIdProp.Name   = FStringView("__type_id");
            TypeIdProp.Hash   = Hash::FNV1a::GetHash32("__type_id");
            TypeIdProp.Getter = +[](lua_State* State) -> int
            {
                if (!lua_getmetatable(State, 1))
                {
                    lua_pushnil(State); return 1;
                }
                
                lua_rawgetfield(State, -1, "__type_id");
                lua_remove(State, -2);
                return 1;
            };
            MergedProps.erase(
                eastl::remove_if(MergedProps.begin(), MergedProps.end(), [](const FPropertyEntry& E)
                {
                    return E.Name == FStringView("__type_id");
                }), MergedProps.end());
            
            
            MergedProps.push_back(TypeIdProp);
        }

        eastl::sort(MergedMethods.begin(), MergedMethods.end(), [](const FMethodEntry& A, const FMethodEntry& B)
        {
            return A.Atom < B.Atom;
        });
        
        eastl::sort(MergedProps.begin(), MergedProps.end(), [](const FPropertyEntry& A, const FPropertyEntry& B)
        {
            return A.Hash < B.Hash;
        });

        if (!MergedMethods.empty())
        {
            auto* Stored = Internal::FEntryTable<FMethodEntry>::Allocate(L, static_cast<uint32>(MergedMethods.size()));
            for (uint32 i = 0; i < MergedMethods.size(); ++i)
            {
                Stored->Entries()[i] = MergedMethods[i];
            }

            lua_pushvalue(L, -1); // [MT, MethodsUD, MethodsUD]
            lua_rawsetfield(L, -3, "__lumina_methods"); // [MT, MethodsUD]

            lua_pushcclosure(L, &Internal::GenericNamecall, "__namecall", 1); // [MT, NamecallClosure]
            lua_rawsetfield(L, -2, "__namecall"); // [MT]
        }

        if (bHasTypeId || !MergedProps.empty())
        {
            auto* PropsUD = Internal::FEntryTable<FPropertyEntry>::Allocate(L, static_cast<uint32>(MergedProps.size())); // [MT, PropsUD]
            for (uint32 i = 0; i < MergedProps.size(); ++i)
            {
                PropsUD->Entries()[i] = MergedProps[i];
            }

            lua_pushvalue(L, -1); // [MT, PropsUD, PropsUD]
            lua_rawsetfield(L, -3, "__lumina_properties"); // [MT, PropsUD]

            lua_pushvalue(L, -1); // [MT, PropsUD, PropsUD]
            lua_pushcclosure(L, &Internal::GenericIndex, "__index", 1); // [MT, PropsUD, IndexClosure]
            lua_rawsetfield(L, -3, "__index"); // [MT, PropsUD]

            lua_pushcclosure(L, &Internal::GenericNewindex, "__newindex", 1); // [MT, NewindexClosure]
            lua_rawsetfield(L, -2, "__newindex"); // [MT]
        }

        // Editor introspection table: name -> "method"|"property".
        lua_newtable(L); // [MT, MembersTable]
        for (const auto& M : MergedMethods)
        {
            lua_pushlstring(L, "method", 6);
            lua_rawsetfield(L, -2, M.Name.data());
        }
        for (const auto& P : MergedProps)
        {
            lua_pushlstring(L, "property", 8);
            lua_rawsetfield(L, -2, P.Name.data());
        }
        if (!ParentName.empty())
        {
            lua_pushlstring(L, ParentName.data(), ParentName.size());
            lua_rawsetfield(L, -2, "__parentname");
        }
        lua_rawsetfield(L, -2, "__lumina_members"); // [MT]

        if (bHasTypeId)
        {
            lua_pushunsigned(L, TypeIdHash);
            lua_rawsetfield(L, -2, "__type_id"); // [MT]
        }

        InstallUserdataDestructor(UserdataTag);

        lua_pushvalue(L, -1); // [MT, MTcopy]
        lua_setuserdatametatable(L, UserdataTag); // [MT]

        lua_newtable(L); // [MT, GlobalTable]
        if (bHasTypeId)
        {
            lua_pushunsigned(L, TypeIdHash);
            lua_rawsetfield(L, -2, "__type_id");
        }
        lua_pushstring(L, Name.data());
        lua_rawsetfield(L, -2, "__typename");
        lua_setglobal(L, Name.data()); // [MT]

        lua_pop(L, 1); // []

#if WITH_EDITOR
        // Hand the auto-derived signatures to the editor type registry as this class's Luau type.
        if (!TypeMembers.empty())
        {
            FScriptTypeRegistry::Get().RegisterClassType(Name, TypeMembers);
        }
#endif

        return *this;
    }

    FClassBuilder& FClassBuilder::AddStaticFunction(FStringView FuncName, lua_CFunction Func)
    {
        lua_getglobal(L, Name.data());
        lua_pushcfunction(L, Func, FuncName.data());
        lua_rawsetfield(L, -2, FuncName.data());
        lua_pop(L, 1);
        return *this;
    }

    namespace
    {
        // Keyed by CClass*; lifetime is process-wide (CClass is leaked by design).
        THashMap<const CClass*, FUserdataLayout>& GetCObjectLayoutRegistry()
        {
            static THashMap<const CClass*, FUserdataLayout> Registry;
            return Registry;
        }

        // Reverse map: runtime userdata tag -> the CObject class registered for it. Lets us recognize a
        // CObject userdata on the Lua stack when the concrete C++ type isn't known at compile time.
        THashMap<uint16, const CClass*>& GetCObjectTagRegistry()
        {
            static THashMap<uint16, const CClass*> Registry;
            return Registry;
        }
    }

    void RegisterCObjectLayout(const CClass* Class, const FUserdataLayout& Layout)
    {
        if (Class == nullptr)
        {
            return;
        }
        GetCObjectLayoutRegistry()[Class] = Layout;
        GetCObjectTagRegistry()[Layout.Tag] = Class;
    }

    bool IsCObjectUserdata(lua_State* L, int Index)
    {
        if (!lua_isuserdata(L, Index))
        {
            return false;
        }
        const int Tag = lua_userdatatag(L, Index);
        if (Tag < 0)
        {
            return false;
        }
        const auto& Registry = GetCObjectTagRegistry();
        return Registry.find(static_cast<uint16>(Tag)) != Registry.end();
    }

    CObject* ToCObject(lua_State* L, int Index)
    {
        if (!IsCObjectUserdata(L, Index))
        {
            return nullptr;
        }
        // The block is an in-place TObjectPtr<ClassT> (pointer-sized, object pointer at offset 0).
        void* Block = lua_touserdata(L, Index);
        return Block != nullptr ? *static_cast<CObject* const*>(Block) : nullptr;
    }

    const FUserdataLayout* FindCObjectLayout(const CClass* Class)
    {
        if (Class == nullptr)
        {
            return nullptr;
        }
        const auto& Registry = GetCObjectLayoutRegistry();
        const auto It = Registry.find(Class);
        return It != Registry.end() ? &It->second : nullptr;
    }

    void PushCObjectAsActualType(lua_State* L, CObject* Object)
    {
        if (Object == nullptr)
        {
            lua_pushnil(L);
            return;
        }

        // Walk up to nearest bound ancestor so unbound subclasses still get a metatable.
        const FUserdataLayout* Layout = nullptr;
        for (const CClass* Class = Object->GetClass(); Class != nullptr; Class = Class->GetSuperClass())
        {
            Layout = FindCObjectLayout(Class);
            if (Layout != nullptr)
            {
                break;
            }
        }

        if (Layout == nullptr)
        {
            lua_pushnil(L);
            return;
        }

        void* Block = lua_newuserdatataggedwithmetatable(L, Layout->Size, Layout->Tag);
        Layout->Initialize(Block);
        // SetExternal constructs the userdata's owning TObjectPtr<ClassT> (takes a strong GC ref);
        // the tag's destructor (TClass::Register) runs ~TObjectPtr to release it.
        Layout->SetExternal(Block, Object);
    }
}
