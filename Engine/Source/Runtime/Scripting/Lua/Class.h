#pragma once

#include "Invoker.h"
#include "Traits.h"
#include "Containers/Array.h"
#include "Containers/String.h"

#include <EASTL/algorithm.h>
#include <EASTL/type_traits.h>

namespace Lumina
{
    class CObject;
    class CClass;
}

namespace Lumina::Lua
{
    // Default factory used by codegen as the `new` constructor for reflected
    // structs. Returning T by value lets TStack<T>::Push wrap it in the
    // matching tagged userdata, so we don't have to duplicate that plumbing.
    template <typename T>
    T DefaultConstruct() { return T{}; }

    struct FMethodEntry
    {
        int16           Atom    = -1;
        FStringView     Name    = {};
        lua_CFunction   Invoke  = nullptr;
    };

    struct FPropertyEntry
    {
        uint32          Hash    = 0;
        FStringView     Name    = {};
        lua_CFunction   Getter  = nullptr;
        lua_CFunction   Setter  = nullptr;
    };

    namespace Internal
    {
        // Heap header followed by a flexible run of TEntry. Lifetime is owned
        // by Lua: we allocate via lua_newuserdata and root the userdata as a
        // metatable field, so the array lives as long as the metatable does.
        template <typename TEntry>
        struct alignas(TEntry) FEntryTable
        {
            uint32  Count = 0;

            TEntry*       Entries()       { return reinterpret_cast<TEntry*>(this + 1); }
            const TEntry* Entries() const { return reinterpret_cast<const TEntry*>(this + 1); }

            static FEntryTable* Allocate(lua_State* L, uint32 N)
            {
                const size_t Bytes = sizeof(FEntryTable) + N * sizeof(TEntry);
                void* Block = lua_newuserdata(L, Bytes);
                auto* Header = static_cast<FEntryTable*>(Block);
                Header->Count = N;
                for (uint32 i = 0; i < N; ++i)
                {
                    new (Header->Entries() + i) TEntry{};
                }
                return Header;
            }
        };

        // One generic dispatcher reused across every TClass<T> registration.
        // The per-type data lives entirely in the closure upvalue, so binary
        // sizes don't grow with the number of registered types.
        inline int GenericNamecall(lua_State* L)
        {
            int RawAtom = 0;
            lua_namecallatom(L, &RawAtom);
            const int16 Atom = static_cast<int16>(RawAtom);

            const auto* Methods = static_cast<const FEntryTable<FMethodEntry>*>(lua_touserdata(L, lua_upvalueindex(1)));
            if (!Methods)
            {
                return 0;
            }

            const FMethodEntry* Begin = Methods->Entries();
            const FMethodEntry* End   = Begin + Methods->Count;
            const auto It = eastl::lower_bound(Begin, End, Atom, [](const FMethodEntry& Entry, int16 Value)
            {
                return Entry.Atom < Value;
            });

            if (It != End && It->Atom == Atom && It->Invoke)
            {
                return It->Invoke(L);
            }
            return 0;
        }

        inline int GenericIndex(lua_State* L)
        {
            const char* Key = lua_tostring(L, 2);
            if (!Key)
            {
                return 0;
            }

            const uint32 Hash = Hash::FNV1a::GetHash32(Key);

            const auto* Props = static_cast<const FEntryTable<FPropertyEntry>*>(lua_touserdata(L, lua_upvalueindex(1)));
            if (!Props)
            {
                return 0;
            }

            const FPropertyEntry* Begin = Props->Entries();
            const FPropertyEntry* End   = Begin + Props->Count;
            const auto It = eastl::lower_bound(Begin, End, Hash, [](const FPropertyEntry& Entry, uint32 Value)
            {
                return Entry.Hash < Value;
            });

            if (It != End && It->Hash == Hash && It->Getter)
            {
                return It->Getter(L);
            }
            return 0;
        }

        inline int GenericNewindex(lua_State* L)
        {
            const char* Key = lua_tostring(L, 2);
            if (!Key)
            {
                return 0;
            }

            const uint32 Hash = Hash::FNV1a::GetHash32(Key);

            const auto* Props = static_cast<const FEntryTable<FPropertyEntry>*>(lua_touserdata(L, lua_upvalueindex(1)));
            if (!Props)
            {
                return 0;
            }

            const FPropertyEntry* Begin = Props->Entries();
            const FPropertyEntry* End   = Begin + Props->Count;
            const auto It = eastl::lower_bound(Begin, End, Hash, [](const FPropertyEntry& Entry, uint32 Value)
            {
                return Entry.Hash < Value;
            });

            if (It != End && It->Hash == Hash && It->Setter)
            {
                return It->Setter(L);
            }
            return 0;
        }
    }

    // Per-class metadata captured at registration time so we can push a
    // CObject* using its actual runtime type's metatable rather than the
    // static type the caller happened to hold. Without this, loading an
    // object via `Engine.LoadObject(path)` (which returns CObject*) would
    // wrap the result with CObject's metatable even when the underlying
    // instance is a subclass with its own bound methods.
    struct FUserdataLayout
    {
        uint16  Tag         = 0;
        size_t  Size        = 0;
        void  (*Initialize) (void* Block)             = nullptr;
        void  (*SetExternal)(void* Block, void* Ptr)  = nullptr;
    };

    RUNTIME_API void RegisterCObjectLayout(const CClass* Class, const FUserdataLayout& Layout);
    RUNTIME_API const FUserdataLayout* FindCObjectLayout(const CClass* Class);

    // Pushes Object as a Lua userdata wrapped with the metatable of its
    // actual runtime class (walking up Object->GetClass()->GetSuperClass() if
    // the leaf class isn't bound to Lua). Pushes nil if Object is null or no
    // ancestor is bound. Use this anywhere a polymorphic CObject* needs to
    // cross the C++/Lua boundary, direct loaders, return values from
    // reflected functions, etc.
    RUNTIME_API void PushCObjectAsActualType(lua_State* L, CObject* Object);

    // Type-erased registration helper. Per-type code is confined to the small
    // template glue (Invoker<TFunc>, AddProperty's getter/setter lambdas, and
    // the destructor lambda for non-trivially-destructible types); the bulk
    // of Register lives in this base class so it's emitted once for the whole
    // program rather than once per registered type.
    class RUNTIME_API FClassBuilder
    {
    public:

        FClassBuilder(lua_State* InL, FStringView InName);

        FClassBuilder& SetSuperClass(FStringView InParentName);
        FClassBuilder& EnableTypeId();

        // Records an entry. Allocates only during registration; dispatch is
        // unaffected because the final array is stored as a Lua userdata.
        FClassBuilder& AddMethod(FStringView FuncName, lua_CFunction Func);
        FClassBuilder& AddProperty(FStringView PropName, lua_CFunction Getter, lua_CFunction Setter);
        FClassBuilder& AddMetamethod(FStringView MetaName, lua_CFunction Func);

        // Walks the parent chain (if any), merges entries (child wins on name
        // collisions), sorts, allocates the dispatch tables on the Lua heap,
        // and stamps the metatable + global. Stack-balanced.
        FClassBuilder& Register(int UserdataTag);

        // Static helpers attached to the public global (e.g. constructors,
        // class loaders). Must be called after Register.
        FClassBuilder& AddStaticFunction(FStringView FuncName, lua_CFunction Func);

    protected:

        // Subclasses (TClass<T>) install a userdata destructor by tag — keeps
        // the type-specific destruction lambda out of the type-erased path.
        virtual void InstallUserdataDestructor(int /*Tag*/) {}

        lua_State*                  L           = nullptr;
        FStringView                 Name        = {};
        FStringView                 ParentName  = {};
        bool                        bHasTypeId  = false;
        TVector<FMethodEntry>       Methods;
        TVector<FPropertyEntry>     Properties;
    };

    template <typename T>
    class TClass : public FClassBuilder
    {
    public:

        using ClassT = T;

        TClass(lua_State* VM, FStringView InName)
            : FClassBuilder(VM, InName)
        {}

        TClass& SetSuperClass(FStringView InParentName) { FClassBuilder::SetSuperClass(InParentName); return *this; }
        TClass& EnableTypeId()                          { FClassBuilder::EnableTypeId(); return *this; }

        template <auto TFunc>
        TClass& AddFunction(FStringView FuncName)
        {
            using TraitsT = TFunctionTraits<decltype(TFunc)>;

            if constexpr (eastl::is_member_function_pointer_v<decltype(TFunc)>)
            {
                // Self extraction is untagged because the metatable dispatch
                // already constrains the userdata to a compatible type. With a
                // strict `lua_touserdatatagged` we'd reject child userdata when
                // the method comes from a parent type via inheritance — see
                // TUserdataHeader's layout note for why this cast is sound.
                FClassBuilder::AddMethod(FuncName, [](lua_State* State) -> int
                {
                    return InvokerSelfUntagged<T, TFunc>(State);
                });
            }
            else
            {
                FClassBuilder::AddMethod(FuncName, [](lua_State* State) -> int { return Invoker<TFunc>(State); });
            }
            return *this;
        }

        TClass& AddRawFunction(FStringView FuncName, lua_CFunction Func)
        {
            FClassBuilder::AddMethod(FuncName, Func);
            return *this;
        }

        // Sets a metamethod (e.g. __add, __eq) directly on the metatable.
        // Bypasses the dispatch tables since metamethods aren't called by name.
        template <auto TFunc>
        TClass& AddFunction(EMetaMethod Method)
        {
            FClassBuilder::AddMetamethod(MetaMethodName(Method), &Invoker<TFunc>);
            return *this;
        }

        template <auto TMemberPtr>
        requires (eastl::is_member_object_pointer_v<decltype(TMemberPtr)>)
        TClass& AddProperty(FStringView PropName)
        {
            using MemberRefT = decltype(eastl::declval<T&>().*TMemberPtr);
            using MemberT    = eastl::remove_cvref_t<MemberRefT>;

            // Untagged self access: when this getter is inherited by a child
            // type, the userdata's tag is the child's, not T's. The
            // metatable lookup that got us here already proved type
            // compatibility, so we can safely reinterpret as TUserdataHeader<T>
            // and access fields T owns (single-inheritance layout guarantee).
            //
            // PushArg (not TStack<MemberRefT>::Push) routes the result so
            // primitives like bool/int/float push as Lua values; only
            // non-native member types get wrapped as userdata refs. Without
            // this, `obj.bFlag` returns a userdata wrapping the bool rather
            // than the boolean itself.
            lua_CFunction Getter = [](lua_State* State) -> int
            {
                auto* Header = static_cast<TUserdataHeader<T>*>(lua_touserdata(State, 1));
                if (Header == nullptr) return 0;
                T* Self = Header->Underlying();
                if (Self == nullptr) return 0;
                PushArg(State, Self->*TMemberPtr);
                return 1;
            };

            lua_CFunction Setter = nullptr;
            if constexpr (eastl::is_assignable_v<MemberRefT, MemberT&&>)
            {
                Setter = [](lua_State* State) -> int
                {
                    auto* Header = static_cast<TUserdataHeader<T>*>(lua_touserdata(State, 1));
                    if (Header == nullptr) return 0;
                    T* Self = Header->Underlying();
                    if (Self == nullptr) return 0;
                    MemberT Member = TStack<MemberT>::Get(State, 3);
                    Self->*TMemberPtr = std::move(Member);
                    return 0;
                };
            }

            FClassBuilder::AddProperty(PropName, Getter, Setter);
            return *this;
        }

        TClass& AddRawProperty(FStringView PropName, lua_CFunction Getter, lua_CFunction Setter = nullptr)
        {
            FClassBuilder::AddProperty(PropName, Getter, Setter);
            return *this;
        }

        TClass& Register()
        {
            FClassBuilder::Register(TClassTraits<ClassT>::Tag());

            // Register the layout for polymorphic pushes. Only CObject-derived
            // types participate, POD structs (CStruct) don't have runtime
            // type identity, so pushing them by static type is correct.
            if constexpr (eastl::is_base_of_v<CObject, ClassT>)
            {
                // Polymorphic CObject push is always external — the object
                // outlives the userdata and we only need the External slot at
                // offset 0. Skip the inline Buffer (sizeof(ClassT) of dead
                // weight per pushed CObject) and skip Initialize entirely;
                // SetExternal writes the pointer straight in.
                FUserdataLayout Layout;
                Layout.Tag         = TClassTraits<ClassT>::Tag();
                Layout.Size        = sizeof(ClassT*);
                Layout.Initialize  = +[](void*) {};
                Layout.SetExternal = +[](void* Block, void* Ptr)
                {
                    *static_cast<ClassT**>(Block) = static_cast<ClassT*>(Ptr);
                };
                RegisterCObjectLayout(ClassT::StaticClass(), Layout);
            }

            return *this;
        }

        template <auto TFunc>
        TClass& AddConstructor()
        {
            FClassBuilder::AddStaticFunction("new",
                [](lua_State* State) -> int { return Invoker<TFunc>(State); });
            return *this;
        }

        TClass& AddStaticRawFunction(FStringView FuncName, lua_CFunction Func)
        {
            FClassBuilder::AddStaticFunction(FuncName, Func);
            return *this;
        }

    protected:

        void InstallUserdataDestructor(int Tag) override
        {
            if constexpr (!eastl::is_trivially_destructible_v<ClassT>)
            {
                lua_setuserdatadtor(L, Tag, [](lua_State*, void* UD)
                {
                    auto* TypedData = static_cast<TUserdataHeader<ClassT>*>(UD);
                    TypedData->InvokeDtor();
                });
            }
        }
    };
}
