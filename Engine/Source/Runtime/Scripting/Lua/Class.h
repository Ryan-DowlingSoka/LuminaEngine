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
    
    template <typename T, size_t NMethods = 0>
    class TClass
    {
    public:
    
        using ClassT = T;
        
        TClass(lua_State* VM, FStringView InName)
            : L(VM)
            , Name(InName)
        {
            luaL_newmetatable(L, InName.data()); // [MT]

            // Stamp the metatable with its own registered name so the editor
            // duck-typer (and anyone debugging) can recover it in O(1).
            lua_pushstring(L, InName.data());
            lua_rawsetfield(L, -2, "__typename");

            lua_newtable(L); // [MT] | [__index]
            lua_newtable(L); // [MT] | [__index] | [__newindex]
            
            lua_newtable(L);      // [MT] | [__index] | [__newindex] | [[GlobalTable]
            lua_setglobal(L, Name.data()); // [MT] | [__index] | [__newindex]
        }
    
        TClass(lua_State* InL, FStringView InName,
               const TArray<FMethodEntry,   NMethods>&    InMethods)
            : L(InL)
            , Name(InName)
        {
            for (size_t i = 0; i < NMethods; ++i)
            {
                Methods[i]    = InMethods[i];
            }
        }
        
        template<auto TFunc>
        auto AddConstructor()
        {
            lua_getglobal(L, Name.data());
            
            lua_pushcfunction(L, [](lua_State* State)
            {
                return Invoker<TFunc>(State);
            }, "new");
            
            lua_rawsetfield(L, -2, "new");
            lua_pop(L, 1);
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

            return TClass<T, NMethods + 1>(L, Name, NewMethods);
        }

        /**
         * Register a raw lua_CFunction as a method. Use this for methods that need
         * direct access to lua_State* (e.g. functions that yield via lua_yield),
         * since the templated AddFunction path can't represent those.
         *
         * The raw function receives the standard __namecall stack: [self, args...].
         */
        auto AddRawFunction(FStringView FuncName, lua_CFunction Func)
        {
            FMethodEntry Entry;
            Entry.Name   = FuncName;
            Entry.Invoke = Func;

            TArray<FMethodEntry, NMethods + 1> NewMethods;
            for (size_t i = 0; i < NMethods; ++i)
            {
                NewMethods[i] = Methods[i];
            }
            NewMethods[NMethods] = Entry;

            return TClass<T, NMethods + 1>(L, Name, NewMethods);
        }
        
        template <auto TFunc>
        auto AddFunction(EMetaMethod Method)
        {
            FStringView MethodName = MetaMethodName(Method);
            lua_pushcfunction(L, &Invoker<TFunc>, MethodName.data()); // [MT] | [__index] | [__newindex] | [[func]]
            lua_rawsetfield(L, -4, MethodName.data());  // [MT] | [__index] | [__newindex]

            return *this;
        }
        
        template <auto TMemberPtr>
        requires(eastl::is_member_object_pointer_v<decltype(TMemberPtr)>)
        auto AddProperty(FStringView PropName)
        {
            using MemberRefT = decltype(eastl::declval<T&>().*TMemberPtr);
            using MemberT = eastl::remove_cvref_t<MemberRefT>;
            
            lua_pushcfunction(L, [](lua_State* State) -> int
            {
                T& Self = TStack<T&>::Get(State, 1);
                TStack<MemberRefT>::Push(State, Self.*TMemberPtr);
                return 1;
            }, PropName.data());   // [MT] | [__index] | [__newindex] | [func]
            lua_rawsetfield(L, -3, PropName.data());   // [MT] | [__index] | [__newindex]
            
            if constexpr (eastl::is_assignable_v<MemberRefT, MemberT&&>)
            {
                lua_pushcfunction(L, [](lua_State* State) -> int
                {
                    T& Self = TStack<T&>::Get(State, 1);
                    MemberT Member = TStack<MemberT>::Get(State, 2);
                    Self.*TMemberPtr = Move(Member);
                    return 0;
                }, PropName.data());   // [MT] | [__index] | [__newindex] | [func]
                lua_rawsetfield(L, -2, PropName.data());   // [MT] | [__index] | [__newindex]
            }
            
            return *this;
        }
        
        void Register()
        {
            for (auto& Entry : Methods)
            {
                Entry.Atom = Hash::FNV1a::GetHash16(Entry.Name.data());
            }
            
            eastl::sort(Methods.begin(), Methods.end(), [](const FMethodEntry& A, const FMethodEntry& B)
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
                }, "__namecall", 1);   // [MT] | [__index] | [__newindex] | [Func]
    
                lua_rawsetfield(L, -4, "__namecall");    // [MT] | [__index] | [__newindex]
            }
            
            lua_rawsetfield(L, -3, "__newindex");    // [MT] | [__index]
            lua_rawsetfield(L, -2, "__index");    // [MT]

            lua_newtable(L);     // [MT] | [Table]
            lua_setglobal(L, Name.data()); // [MT]
            
            if constexpr (!eastl::is_trivially_destructible_v<ClassT>)
            {
                lua_setuserdatadtor(L, TClassTraits<ClassT>::Tag(), [](lua_State*, void* UD)
                {
                    auto* TypedData = static_cast<TUserdataHeader<ClassT>*>(UD);
                    TypedData->InvokeDtor();
                });
            }
            
            lua_setuserdatametatable(L, TClassTraits<ClassT>::Tag()); // Clear
        }
    
    private:
    
        lua_State*                                  L             = nullptr;
        FStringView                                 Name          = {};
        TArray<FMethodEntry,   NMethods>            Methods       = {};
    };
}
