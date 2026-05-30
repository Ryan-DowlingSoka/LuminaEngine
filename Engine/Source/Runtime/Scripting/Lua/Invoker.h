#pragma once

#include "lualib.h"
#include "Stack.h"
#include "Traits.h"

namespace Lumina::Lua
{
    template<typename TParam>
    static decltype(auto) GetArg(lua_State* L, int Index)
    {
        using BaseT = eastl::remove_cvref_t<TParam>;

        if constexpr (TLuaNativeType<BaseT>::value)
        {
            return TStack<BaseT>::Get(L, Index);
        }
        else if constexpr (eastl::is_pointer_v<BaseT>)
        {
            return TStack<BaseT>::Get(L, Index);
        }
        else if constexpr (eastl::is_enum_v<BaseT>)
        {
            return TStack<BaseT>::Get(L, Index);
        }
        else if constexpr (eastl::is_lvalue_reference_v<TParam>)
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

        if constexpr (TLuaNativeType<BaseT>::value)
        {
            TStack<BaseT>::Push(L, Param);
        }
        else if constexpr (eastl::is_pointer_v<BaseT>)
        {
            TStack<BaseT>::Push(L, Param);
        }
        else if constexpr (eastl::is_enum_v<BaseT>)
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
    
    template<auto TFunc>
    auto LightInvoker(lua_State* L)
    {
        using TraitsT = TFunctionTraits<decltype(TFunc)>;
        using ReturnT = TraitsT::ReturnType;
        using ArgsT   = TraitsT::ArgsTuple;
        
        auto Dispatch = [&]<size_t... Is>(eastl::index_sequence<Is...>)
        {
            if constexpr (eastl::is_member_function_pointer_v<decltype(TFunc)>)
            {
                using ClassT  = TraitsT::ClassType;
                
                ClassT* Self = static_cast<ClassT*>(lua_tolightuserdata(L, 1));
                if (Self == nullptr)
                {
                    luaL_errorL(L, "[%s]", "Cannot invoke lua function with incorrect usertype.");
                }
                
                if constexpr (eastl::is_void_v<ReturnT>)
                {
                    (Self->*TFunc)(ArgResolve::ResolveArg<ArgsT, Is>(L, 2)...);
                    return 0;
                }
                else
                {
                    decltype(auto) Ret = (Self->*TFunc)(ArgResolve::ResolveArg<ArgsT, Is>(L, 2)...);
                    PushArg(L, eastl::forward<decltype(Ret)>(Ret));
                    return 1;
                }
            }
            else
            {
                if constexpr (eastl::is_void_v<ReturnT>)
                {
                    TFunc(ArgResolve::ResolveArg<ArgsT, Is>(L, 1)...);
                    return 0;
                }
                else
                {
                    decltype(auto) Ret = TFunc(ArgResolve::ResolveArg<ArgsT, Is>(L, 1)...);
                    PushArg(L, eastl::forward<decltype(Ret)>(Ret));
                    return 1;
                }
            }
        };
        
        return Dispatch(eastl::make_index_sequence<TraitsT::ArgCount>{});
    }
    
    // Untagged self-read; metatable dispatch constrains type, header layout makes the cast sound.
    // Used by TClass<T>::AddFunction so inherited methods work on child userdata.
    template<typename SelfClassT, auto TFunc>
    auto InvokerSelfUntagged(lua_State* L)
    {
        using TraitsT = TFunctionTraits<decltype(TFunc)>;
        using ReturnT = TraitsT::ReturnType;
        using ArgsT   = TraitsT::ArgsTuple;

        auto* Header = static_cast<TUserdataHeader<SelfClassT>*>(lua_touserdata(L, 1));
        if (Header == nullptr)
        {
            luaL_errorL(L, "[%s]", "Cannot invoke lua function with incorrect usertype.");
        }

        SelfClassT* Self = Header->Underlying();
        if (Self == nullptr)
        {
            luaL_errorL(L, "[%s]", "Cannot invoke lua function with incorrect usertype.");
        }

        auto Dispatch = [&]<size_t... Is>(eastl::index_sequence<Is...>)
        {
            if constexpr (eastl::is_void_v<ReturnT>)
            {
                (Self->*TFunc)(ArgResolve::ResolveArg<ArgsT, Is>(L, 2)...);
                return 0;
            }
            else
            {
                decltype(auto) Ret = (Self->*TFunc)(ArgResolve::ResolveArg<ArgsT, Is>(L, 2)...);
                PushArg(L, eastl::forward<decltype(Ret)>(Ret));
                return 1;
            }
        };

        return Dispatch(eastl::make_index_sequence<TraitsT::ArgCount>{});
    }

    template<auto TFunc>
    auto Invoker(lua_State* L)
    {
        using TraitsT = TFunctionTraits<decltype(TFunc)>;
        using ReturnT = TraitsT::ReturnType;
        using ArgsT   = TraitsT::ArgsTuple;

        auto Dispatch = [&]<size_t... Is>(eastl::index_sequence<Is...>)
        {
            if constexpr (eastl::is_member_function_pointer_v<decltype(TFunc)>)
            {
                using ClassT  = TraitsT::ClassType;

                ClassT* Self = TStack<ClassT*>::Get(L, 1);
                if (Self == nullptr)
                {
                    luaL_errorL(L, "[%s]", "Cannot invoke lua function with incorrect usertype.");
                }
                
                if constexpr (eastl::is_void_v<ReturnT>)
                {
                    (Self->*TFunc)(ArgResolve::ResolveArg<ArgsT, Is>(L, 2)...);
                    return 0;
                }
                else
                {
                    decltype(auto) Ret = (Self->*TFunc)(ArgResolve::ResolveArg<ArgsT, Is>(L, 2)...);
                    PushArg(L, eastl::forward<decltype(Ret)>(Ret));
                    return 1;
                }
            }
            else
            {
                if constexpr (eastl::is_void_v<ReturnT>)
                {
                    TFunc(ArgResolve::ResolveArg<ArgsT, Is>(L, 1)...);
                    return 0;
                }
                else
                {
                    decltype(auto) Ret = TFunc(ArgResolve::ResolveArg<ArgsT, Is>(L, 1)...);
                    PushArg(L, eastl::forward<decltype(Ret)>(Ret));
                    return 1;
                }
            }
        };
        
        return Dispatch(eastl::make_index_sequence<TraitsT::ArgCount>{});
    }
    
    template<auto TFunc>
    auto InvokerWithInstance(lua_State* L)
    {
        using TraitsT = TFunctionTraits<decltype(TFunc)>;
        using ReturnT = TraitsT::ReturnType;
        using ArgsT   = TraitsT::ArgsTuple;
        
        auto Dispatch = [&]<size_t... Is>(eastl::index_sequence<Is...>)
        {
            if constexpr (eastl::is_member_function_pointer_v<decltype(TFunc)>)
            {
                using ClassT = TraitsT::ClassType;
                
                ClassT* Self = static_cast<ClassT*>(lua_tolightuserdatatagged(L, lua_upvalueindex(1), TClassTraits<ClassT>::Tag()));
                if (Self == nullptr)
                {
                    luaL_errorL(L, "[%s]", "Cannot invoke lua function with incorrect usertype.");
                }
                
                if constexpr (eastl::is_void_v<ReturnT>)
                {
                    (Self->*TFunc)(ArgResolve::ResolveArg<ArgsT, Is>(L, 1)...);
                    return 0;
                }
                else
                {
                    decltype(auto) Ret = (Self->*TFunc)(ArgResolve::ResolveArg<ArgsT, Is>(L, 1)...);
                    PushArg(L, eastl::forward<decltype(Ret)>(Ret));
                    return 1;
                }
            }
            else
            {
                if constexpr (eastl::is_void_v<ReturnT>)
                {
                    TFunc(ArgResolve::ResolveArg<ArgsT, Is>(L, 1)...);
                    return 0;
                }
                else
                {
                    decltype(auto) Ret = TFunc(ArgResolve::ResolveArg<ArgsT, Is>(L, 1)...);
                    PushArg(L, eastl::forward<decltype(Ret)>(Ret));
                    return 1;
                }
            }
        };
    
        return Dispatch(eastl::make_index_sequence<TraitsT::ArgCount>{});
    }
}
