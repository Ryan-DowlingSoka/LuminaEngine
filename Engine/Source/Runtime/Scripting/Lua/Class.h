#pragma once

#include "Invoker.h"
#include "Traits.h"
#include "Log/Log.h"
#include "Containers/String.h"

namespace Lumina::Lua
{
    struct FMethodEntry
    {
        int16           Atom   = -1;
        FStringView     Name   = {};
        lua_CFunction   Invoke = nullptr;
    };

    struct FPropertyEntry
    {
        int16           Atom    = -1;
        FStringView     Name    = {};
        lua_CFunction   Getter = nullptr;
        lua_CFunction   Setter = nullptr;
    };
    
    template <typename T, size_t NMethods = 0, size_t NMetaMethods = 0, size_t NProperties = 0>
    class TClass
    {
    public:
    
        using ClassT = T;
        
        TClass(lua_State* VM, FStringView InName)
            : L(VM)
            , Name(InName)
        {
            luaL_newmetatable(L, InName.data());
            MetaTableIdx = lua_gettop(L);
        }
    
        TClass(lua_State* InL, FStringView InName, int InMetaTableIdx,
               const TArray<FMethodEntry,   NMethods>&    InMethods,
               const TArray<FMethodEntry,   NMetaMethods>& InMetaMethods,
               const TArray<FPropertyEntry, NProperties>& InProperties)
            : L(InL)
            , Name(InName)
            , MetaTableIdx(InMetaTableIdx)
        {
            for (size_t i = 0; i < NMethods; ++i)
            {
                Methods[i]    = InMethods[i];
            }
            for (size_t i = 0; i < NMetaMethods; ++i)
            {
                MetaMethods[i] = InMetaMethods[i];
            }
            for (size_t i = 0; i < NProperties; ++i)
            {
                Properties[i] = InProperties[i];
            }
        }
        
        template <auto TFunc>
        auto AddFunction(FStringView FuncName)
        {
            FMethodEntry Entry;
            Entry.Name   = FuncName;
            Entry.Invoke = [](lua_State* State) -> int
            {
                return Invoker<TFunc>(State);
            };
    
            TArray<FMethodEntry, NMethods + 1> NewMethods;
            for (size_t i = 0; i < NMethods; ++i)
            {
                NewMethods[i] = Methods[i];
            }
            NewMethods[NMethods] = Entry;
    
            return TClass<T, NMethods + 1, NMetaMethods, NProperties>(L, Name, MetaTableIdx, NewMethods, MetaMethods, Properties);
        }
        
        template <auto TFunc>
        auto AddFunction(EMetaMethod Method)
        {
            FMethodEntry Entry;
            Entry.Name   = MetaMethodName(Method);
            Entry.Invoke = [](lua_State* State) -> int
            {
                return Invoker<TFunc>(State);
            };
    
            TArray<FMethodEntry, NMetaMethods + 1> NewMetaMethods;
            for (size_t i = 0; i < NMetaMethods; ++i)
            {
                NewMetaMethods[i] = MetaMethods[i];
            }
            NewMetaMethods[NMetaMethods] = Entry;
    
            return TClass<T, NMethods, NMetaMethods + 1, NProperties>(L, Name, MetaTableIdx, Methods, NewMetaMethods, Properties);
        }
        
        template <auto TMemberPtr>
        requires(eastl::is_member_object_pointer_v<decltype(TMemberPtr)>)
        auto AddProperty(FStringView PropName)
        {
            using MemberT = eastl::remove_reference_t<decltype(eastl::declval<T>().*TMemberPtr)>;

            FPropertyEntry Entry;
            Entry.Name   = PropName;
            Entry.Getter = [](lua_State* State) -> int
            {
                T& Self = TStack<T&>::Get(State, 1);
                TStack<MemberT>::Push(State, Self.*TMemberPtr);
                return 1;
            };
            Entry.Setter = [](lua_State* State) -> int
            {
                T& Self = TStack<T&>::Get(State, 1);
                MemberT Member = TStack<MemberT>::Get(State, 2);
                Self.*TMemberPtr = Member;
                return 0;
            };
    
            TArray<FPropertyEntry, NProperties + 1> NewProperties;
            for (size_t i = 0; i < NProperties; ++i)
            {
                NewProperties[i] = Properties[i];
            }
            NewProperties[NProperties] = Entry;
    
            return TClass<T, NMethods, NMetaMethods, NProperties + 1>(L, Name, MetaTableIdx, Methods, MetaMethods, NewProperties);
        }
        
        
        void Register()
        {
            for (auto& Entry : Methods)
            {
                Entry.Atom = Hash::FNV1a::GetHash16(Entry.Name.data());
            }
    
            for (auto& Entry : Properties)
            {
                Entry.Atom = Hash::FNV1a::GetHash16(Entry.Name.data());
            }
            
            eastl::sort(Methods.begin(), Methods.end(), [](const FMethodEntry& A, const FMethodEntry& B)
            {
                return A.Atom < B.Atom;
            });

            eastl::sort(Properties.begin(), Properties.end(), [](const FPropertyEntry& A, const FPropertyEntry& B)
            {
                return A.Atom < B.Atom;
            });
            
            if constexpr (NMethods > 0)
            {
                using TMethodStorage  = eastl::array<FMethodEntry, NMethods>;
    
                auto* Stored = static_cast<TMethodStorage*>(lua_newuserdata(L, sizeof(TMethodStorage)));
                new (Stored) TMethodStorage(Methods);
    
                lua_pushcclosure(L, [](lua_State* State) -> int
                {
                    int32 RawAtom = 0;
                    lua_namecallatom(State, &RawAtom);
                    int16 Atom = static_cast<int16>(RawAtom);
    
                    auto* BoundMethods = static_cast<TMethodStorage*>(lua_touserdata(State, lua_upvalueindex(1)));
                    
                    auto It = eastl::lower_bound(BoundMethods->begin(), BoundMethods->end(), Atom, [](const FMethodEntry& Entry, int16 Value)
                    {
                        return Entry.Atom < Value;
                    });

                    if (It != BoundMethods->end() && It->Atom == Atom)
                    {
                        return It->Invoke(State);
                    }
                    
                    return 0;
                }, "__namecall", 1);
    
                lua_setfield(L, MetaTableIdx, "__namecall");
            }
    
            if constexpr (NProperties > 0)
            {
                using TPropertyStorage = TArray<FPropertyEntry, NProperties>;
    
                auto* Stored = static_cast<TPropertyStorage*>(lua_newuserdata(L, sizeof(TPropertyStorage)));
                new (Stored) TPropertyStorage(Properties);
    
                lua_pushcclosure(L, [](lua_State* State) -> int
                {
                    int32 RawAtom = 0;
                    lua_tostringatom(State, 2, &RawAtom);
                    int16 Atom = static_cast<int16>(RawAtom);
    
                    auto* BoundProperties = static_cast<TPropertyStorage*>(lua_touserdata(State, lua_upvalueindex(1)));
                    
                    auto It = eastl::lower_bound(BoundProperties->begin(), BoundProperties->end(), Atom, [](const FPropertyEntry& Entry, int16 Value)
                    {
                        return Entry.Atom < Value;
                    });

                    if (It != BoundProperties->end() && It->Atom == Atom)
                    {
                        return It->Getter(State);
                    }
    
                    return 0;
                }, "__index", 1);
    
                lua_setfield(L, MetaTableIdx, "__index");
    
                Stored = static_cast<TPropertyStorage*>(lua_newuserdata(L, sizeof(TPropertyStorage)));
                new (Stored) TPropertyStorage(Properties);
    
                lua_pushcclosure(L, [](lua_State* State) -> int
                {
                    int32 RawAtom = 0;
                    lua_tostringatom(State, 2, &RawAtom);
                    int16 Atom = static_cast<int16>(RawAtom);
    
                    auto* BoundProperties = static_cast<TPropertyStorage*>(lua_touserdata(State, lua_upvalueindex(1)));
                    
                    auto It = eastl::lower_bound(BoundProperties->begin(), BoundProperties->end(), Atom, [](const FPropertyEntry& Entry, int16 Value)
                    {
                        return Entry.Atom < Value;
                    });

                    if (It != BoundProperties->end() && It->Atom == Atom)
                    {
                        return It->Setter(State);
                    }
    
                    return 0;
                }, "__newindex", 1);
    
                lua_setfield(L, MetaTableIdx, "__newindex");
            }
    
            for (auto& Entry : MetaMethods)
            {
                lua_pushcfunction(L, Entry.Invoke, Entry.Name.data());
                lua_setfield(L, MetaTableIdx, Entry.Name.data());
            }
            
            lua_newtable(L);
            lua_setglobal(L, Name.data());
            
            lua_setuserdatametatable(L, TClassTraits<ClassT>::Tag());
        }
    
    private:
    
        lua_State*                                  L             = nullptr;
        FStringView                                 Name          = {};
        int                                         MetaTableIdx  = -1;
        TArray<FMethodEntry,   NMethods>            Methods       = {};
        TArray<FMethodEntry,   NMetaMethods>        MetaMethods   = {};
        TArray<FPropertyEntry, NProperties>         Properties    = {};
    };
}
