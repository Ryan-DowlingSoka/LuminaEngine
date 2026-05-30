#pragma once

#include "Invoker.h"
#include "Traits.h"
#include "Containers/Array.h"
#include "Containers/String.h"

// Editor-only: the type-definition metadata (auto-derived signatures, doc comments) is built for the
// script editor's analyzer and compiled out of shipping/game runtime entirely.
#if WITH_EDITOR
#include "LuaTypeName.h"
#include "ScriptTypeRegistry.h"
#endif

#include <EASTL/algorithm.h>
#include <EASTL/type_traits.h>

namespace Lumina
{
    class CObject;
    class CClass;
}

namespace Lumina::Lua
{
    // Codegen `new` ctor for reflected structs; by-value return routes through TStack<T>::Push.
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
        // Header + flexible array via lua_newuserdata; lifetime tied to the metatable.
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

        // Shared dispatcher; per-type data lives in the closure upvalue so code size stays constant.
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

    // Lets PushCObjectAsActualType wrap with the runtime class's metatable, not the static type's.
    struct FUserdataLayout
    {
        uint16  Tag         = 0;
        size_t  Size        = 0;
        void  (*Initialize) (void* Block)             = nullptr;
        void  (*SetExternal)(void* Block, void* Ptr)  = nullptr;
    };

    RUNTIME_API void RegisterCObjectLayout(const CClass* Class, const FUserdataLayout& Layout);
    RUNTIME_API const FUserdataLayout* FindCObjectLayout(const CClass* Class);

    // Pushes Object using its runtime class metatable, walking the super chain. Nil if no ancestor bound.
    RUNTIME_API void PushCObjectAsActualType(lua_State* L, CObject* Object);

    // Type-erased registration; bulk of Register is emitted once for the whole program.
    class RUNTIME_API FClassBuilder
    {
    public:

        FClassBuilder(lua_State* InL, FStringView InName);

        FClassBuilder& SetSuperClass(FStringView InParentName);
        FClassBuilder& EnableTypeId();

        FClassBuilder& AddMethod(FStringView FuncName, lua_CFunction Func);
        FClassBuilder& AddProperty(FStringView PropName, lua_CFunction Getter, lua_CFunction Setter);
        FClassBuilder& AddMetamethod(FStringView MetaName, lua_CFunction Func);

        // Merges parent entries (child wins), sorts, stamps metatable + global. Stack-balanced.
        FClassBuilder& Register(int UserdataTag);

        // Must be called after Register.
        FClassBuilder& AddStaticFunction(FStringView FuncName, lua_CFunction Func);

        // Accumulates an auto-derived Luau field declaration ("Name: (self: T, ...) -> Ret"); Register
        // hands the set to FScriptTypeRegistry as this class's editor type. Editor-only no-op otherwise.
        void RecordTypeMember(FStringView MemberName, FString Decl)
        {
#if WITH_EDITOR
            TypeMembers.push_back(FScriptTypeMember{ FString(MemberName.data(), MemberName.size()), eastl::move(Decl), FString() });
#else
            (void)MemberName; (void)Decl;
#endif
        }

        // Attaches a doc comment to the most recently recorded member -- backs the fluent
        // `.AddFunction<...>("X").AddComment("...")`. Editor-only; a no-op in shipping builds.
        void SetLastMemberComment(FStringView Comment)
        {
#if WITH_EDITOR
            if (!TypeMembers.empty())
            {
                TypeMembers.back().Comment.assign(Comment.data(), Comment.size());
            }
#else
            (void)Comment;
#endif
        }

    protected:

        virtual void InstallUserdataDestructor(int /*Tag*/) {}

        lua_State*                  L           = nullptr;
        FStringView                 Name        = {};
        FStringView                 ParentName  = {};
        bool                        bHasTypeId  = false;
        TVector<FMethodEntry>       Methods;
        TVector<FPropertyEntry>     Properties;
#if WITH_EDITOR
        TVector<FScriptTypeMember>  TypeMembers;
#endif
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
                // Untagged self: metatable dispatch already constrains type, strict-tag would reject children.
                FClassBuilder::AddMethod(FuncName, [](lua_State* State) -> int
                {
                    return InvokerSelfUntagged<T, TFunc>(State);
                });
#if WITH_EDITOR
                // Auto-derive the editor type signature from the bound C++ method (self + its args).
                RecordTypeMember(FuncName, BuildLuauMethodDecl<TFunc>(FuncName, Name));
#endif
            }
            else
            {
                // Non-member bound as a method: no self model to derive; the type's open indexer covers it.
                FClassBuilder::AddMethod(FuncName, [](lua_State* State) -> int { return Invoker<TFunc>(State); });
            }
            return *this;
        }

        TClass& AddRawFunction(FStringView FuncName, lua_CFunction Func)
        {
            FClassBuilder::AddMethod(FuncName, Func);
            return *this;
        }

        // Goes straight on the metatable; metamethods aren't called by name.
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

            // Untagged self for inherited getters; PushArg routes primitives as Lua values not userdata.
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

#if WITH_EDITOR
            FString PropDecl(PropName.data(), PropName.size());
            PropDecl += ": ";
            PropDecl += LuaTypeNameOf<MemberT>();
            RecordTypeMember(PropName, eastl::move(PropDecl));
#endif
            return *this;
        }

        // Attaches a doc comment to the last method/property added -- the fluent metadata hook:
        //   .AddFunction<&T::Foo>("Foo").AddComment("What Foo does.")
        // Chains like the rest of the builder; editor-only, no shipping-runtime cost.
        TClass& AddComment(FStringView Comment) { SetLastMemberComment(Comment); return *this; }

        TClass& AddRawProperty(FStringView PropName, lua_CFunction Getter, lua_CFunction Setter = nullptr)
        {
            FClassBuilder::AddProperty(PropName, Getter, Setter);
            return *this;
        }

        TClass& Register()
        {
            FClassBuilder::Register(TClassTraits<ClassT>::Tag());

            // CObject pushes embed an owning TObjectPtr<ClassT> (one strong GC ref); the userdata
            // destructor above runs ~TObjectPtr to release it. Pointer-sized, ClassT* at offset 0.
            if constexpr (eastl::is_base_of_v<CObject, ClassT>)
            {
                FUserdataLayout Layout;
                Layout.Tag         = TClassTraits<ClassT>::Tag();
                Layout.Size        = sizeof(TObjectPtr<ClassT>);
                Layout.Initialize  = +[](void*) {};
                Layout.SetExternal = +[](void* Block, void* Ptr)
                {
                    new (Block) TObjectPtr<ClassT>(static_cast<ClassT*>(Ptr));
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
            if constexpr (eastl::is_base_of_v<CObject, ClassT>)
            {
                // CObject userdata embeds a TObjectPtr<ClassT>; running its destructor releases the
                // strong GC ref it took on construction -- same lifetime path as any owning handle.
                lua_setuserdatadtor(L, Tag, [](lua_State*, void* UD)
                {
                    static_cast<TObjectPtr<ClassT>*>(UD)->~TObjectPtr<ClassT>();
                });
            }
            else if constexpr (!eastl::is_trivially_destructible_v<ClassT>)
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
