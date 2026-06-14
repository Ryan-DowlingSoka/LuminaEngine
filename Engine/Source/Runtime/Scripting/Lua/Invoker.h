#pragma once

#include "lualib.h"
#include "Stack.h"
#include "Traits.h"

namespace Lumina::Lua
{
    template<typename T>
    concept CMemberFunction = eastl::is_member_function_pointer_v<T>;

    template<typename TParam>
    static decltype(auto) GetArg(lua_State* L, int Index)
    {
        using BaseT = eastl::remove_cvref_t<TParam>;

        if constexpr (eastl::is_lvalue_reference_v<TParam> && !TLuaNativeType<BaseT>::value
                      && !eastl::is_pointer_v<BaseT> && !eastl::is_enum_v<BaseT>)
        {
            return TStack<BaseT&>::Get(L, Index);
        }
        else
        {
            return TStack<BaseT>::Get(L, Index);
        }
    }

    template<typename TParam>
    static void PushArg(lua_State* L, TParam&& Param)
    {
        using BaseT = eastl::decay_t<TParam>;

        if constexpr (TLuaNativeType<BaseT>::value || eastl::is_pointer_v<BaseT> || eastl::is_enum_v<BaseT>)
        {
            TStack<BaseT>::Push(L, Param);
        }
        else
        {
            TStack<TParam>::Push(L, eastl::forward<TParam>(Param));
        }
    }

    namespace ArgResolve
    {
        // Count of params before index I read from the arg stack (not TLuaContext-injected); maps a
        // param index to its absolute slot so injected params don't shift positional reads after them.
        template<typename ArgsT, size_t... J>
        constexpr int CountStackArgsImpl(eastl::index_sequence<J...>)
        {
            return (0 + ... + (TLuaContext<eastl::remove_cvref_t<eastl::tuple_element_t<J, ArgsT>>>::value ? 0 : 1));
        }

        template<typename ArgsT, size_t I>
        constexpr int CountStackArgsBefore()
        {
            return CountStackArgsImpl<ArgsT>(eastl::make_index_sequence<I>{});
        }

        // Resolve param I: context params come from the execution context (no slot); the rest read
        // positionally from StackBase + (# stack params before I), so eval order is irrelevant.
        template<typename ArgsT, size_t I>
        decltype(auto) ResolveArg(lua_State* L, int StackBase)
        {
            using P     = eastl::tuple_element_t<I, ArgsT>;
            using BaseP = eastl::remove_cvref_t<P>;
            if constexpr (TLuaContext<BaseP>::value)
            {
                return TLuaContext<BaseP>::Get(L);
            }
            else
            {
                return GetArg<P>(L, StackBase + CountStackArgsBefore<ArgsT, I>());
            }
        }
    }

    namespace Invoker_Private
    {
        FORCEINLINE void CheckSelf(lua_State* L, const void* Self)
        {
            if (Self == nullptr)
            {
                luaL_errorL(L, "[%s]", "Cannot invoke lua function with incorrect usertype.");
            }
        }

        // Calls TFunc with its arguments resolved positionally from ArgBase, pushes the return value
        // (if any), and returns the Lua result count. Self is unused for free functions.
        template<auto TFunc, int ArgBase, typename SelfT>
        FORCEINLINE int CallAndPush(lua_State* L, [[maybe_unused]] SelfT Self)
        {
            using TraitsT = TFunctionTraits<decltype(TFunc)>;
            using ReturnT = TraitsT::ReturnType;
            using ArgsT   = TraitsT::ArgsTuple;

            auto Invoke = [&]<size_t... Is>(eastl::index_sequence<Is...>) -> decltype(auto)
            {
                if constexpr (CMemberFunction<decltype(TFunc)>)
                {
                    return (Self->*TFunc)(ArgResolve::ResolveArg<ArgsT, Is>(L, ArgBase)...);
                }
                else
                {
                    return TFunc(ArgResolve::ResolveArg<ArgsT, Is>(L, ArgBase)...);
                }
            };

            constexpr auto Indices = eastl::make_index_sequence<TraitsT::ArgCount>{};
            if constexpr (eastl::is_void_v<ReturnT>)
            {
                Invoke(Indices);
                return 0;
            }
            else
            {
                decltype(auto) Ret = Invoke(Indices);
                PushArg(L, eastl::forward<decltype(Ret)>(Ret));
                return 1;
            }
        }
        
        // Light userdata at slot 1, untagged.
        template<typename SelfClassT>
        struct FSelfLightUserdata
        {
            static constexpr int StackSlots = 1;
            static SelfClassT* Get(lua_State* L)
            {
                auto* Self = static_cast<SelfClassT*>(lua_tolightuserdata(L, 1));
                CheckSelf(L, Self);
                return Self;
            }
        };

        // Full userdata header at slot 1, untagged so metatable-dispatched calls work on child userdata.
        template<typename SelfClassT>
        struct FSelfUserdataHeader
        {
            static constexpr int StackSlots = 1;
            static SelfClassT* Get(lua_State* L)
            {
                auto* Header = static_cast<TUserdataHeader<SelfClassT>*>(lua_touserdata(L, 1));
                CheckSelf(L, Header);
                SelfClassT* Self = Header->Underlying();
                CheckSelf(L, Self);
                return Self;
            }
        };

        // Tagged userdata at slot 1 (strict type check via TStack).
        template<typename SelfClassT>
        struct FSelfTaggedUserdata
        {
            static constexpr int StackSlots = 1;
            static SelfClassT* Get(lua_State* L)
            {
                SelfClassT* Self = TStack<SelfClassT*>::Get(L, 1);
                CheckSelf(L, Self);
                return Self;
            }
        };

        // Tagged light userdata bound as the closure's first upvalue; self is off the arg stack.
        template<typename SelfClassT>
        struct FSelfUpvalue
        {
            static constexpr int StackSlots = 0;
            static SelfClassT* Get(lua_State* L)
            {
                auto* Self = static_cast<SelfClassT*>(lua_tolightuserdatatagged(L, lua_upvalueindex(1), LightTag_Self));
                CheckSelf(L, Self);
                return Self;
            }
        };

        // Acquires self via SelfSource then dispatches; positional args start right after self's slots.
        template<auto TFunc, template<typename> class SelfSource, typename SelfClassT = TFunctionTraits<decltype(TFunc)>::ClassType>
        FORCEINLINE int InvokeMember(lua_State* L)
        {
            SelfClassT* Self = SelfSource<SelfClassT>::Get(L);
            return CallAndPush<TFunc, SelfSource<SelfClassT>::StackSlots + 1>(L, Self);
        }
    }

    template<auto TFunc>
    int LightInvoker(lua_State* L)
    {
        if constexpr (CMemberFunction<decltype(TFunc)>)
        {
            return Invoker_Private::InvokeMember<TFunc, Invoker_Private::FSelfLightUserdata>(L);
        }
        else
        {
            return Invoker_Private::CallAndPush<TFunc, 1>(L, nullptr);
        }
    }
    
    template<typename SelfClassT, auto TFunc>
    int InvokerSelfUntagged(lua_State* L)
    {
        return Invoker_Private::InvokeMember<TFunc, Invoker_Private::FSelfUserdataHeader, SelfClassT>(L);
    }

    template<auto TFunc>
    int Invoker(lua_State* L)
    {
        if constexpr (CMemberFunction<decltype(TFunc)>)
        {
            return Invoker_Private::InvokeMember<TFunc, Invoker_Private::FSelfTaggedUserdata>(L);
        }
        else
        {
            return Invoker_Private::CallAndPush<TFunc, 1>(L, nullptr);
        }
    }

    template<auto TFunc>
    int InvokerWithInstance(lua_State* L)
    {
        if constexpr (CMemberFunction<decltype(TFunc)>)
        {
            return Invoker_Private::InvokeMember<TFunc, Invoker_Private::FSelfUpvalue>(L);
        }
        else
        {
            return Invoker_Private::CallAndPush<TFunc, 1>(L, nullptr);
        }
    }
}
