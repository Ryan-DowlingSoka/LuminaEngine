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
           TStack<TParam>::Push(L, Param);
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
                    (Self->*TFunc)(GetArg<eastl::tuple_element_t<Is, ArgsT>>(L, Is + 2)...);
                    return 0;
                }
                else
                {
                    decltype(auto) Ret = (Self->*TFunc)(GetArg<eastl::tuple_element_t<Is, ArgsT>>(L, Is + 2)...);
                    PushArg(L, eastl::forward<decltype(Ret)>(Ret));
                    return 1;
                }
            }
            else
            {
                if constexpr (eastl::is_void_v<ReturnT>)
                {
                    TFunc(GetArg<eastl::tuple_element_t<Is, ArgsT>>(L, Is + 1)...);
                    return 0;
                }
                else
                {
                    decltype(auto) Ret = TFunc(GetArg<eastl::tuple_element_t<Is, ArgsT>>(L, Is + 1)...);
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
                (Self->*TFunc)(GetArg<eastl::tuple_element_t<Is, ArgsT>>(L, Is + 2)...);
                return 0;
            }
            else
            {
                decltype(auto) Ret = (Self->*TFunc)(GetArg<eastl::tuple_element_t<Is, ArgsT>>(L, Is + 2)...);
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
                    (Self->*TFunc)(GetArg<eastl::tuple_element_t<Is, ArgsT>>(L, Is + 2)...);
                    return 0;
                }
                else
                {
                    decltype(auto) Ret = (Self->*TFunc)(GetArg<eastl::tuple_element_t<Is, ArgsT>>(L, Is + 2)...);
                    PushArg(L, eastl::forward<decltype(Ret)>(Ret));
                    return 1;
                }
            }
            else
            {
                if constexpr (eastl::is_void_v<ReturnT>)
                {
                    TFunc(GetArg<eastl::tuple_element_t<Is, ArgsT>>(L, Is + 1)...);
                    return 0;
                }
                else
                {
                    decltype(auto) Ret = TFunc(GetArg<eastl::tuple_element_t<Is, ArgsT>>(L, Is + 1)...);
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
                    (Self->*TFunc)(GetArg<eastl::tuple_element_t<Is, ArgsT>>(L, Is + 1)...);
                    return 0;
                }
                else
                {
                    decltype(auto) Ret = (Self->*TFunc)(GetArg<eastl::tuple_element_t<Is, ArgsT>>(L, Is + 1)...);
                    PushArg(L, eastl::forward<decltype(Ret)>(Ret));
                    return 1;
                }
            }
            else
            {
                if constexpr (eastl::is_void_v<ReturnT>)
                {
                    TFunc(GetArg<eastl::tuple_element_t<Is, ArgsT>>(L, Is + 1)...);
                    return 0;
                }
                else
                {
                    decltype(auto) Ret = TFunc(GetArg<eastl::tuple_element_t<Is, ArgsT>>(L, Is + 1)...);
                    PushArg(L, eastl::forward<decltype(Ret)>(Ret));
                    return 1;
                }
            }
        };
    
        return Dispatch(eastl::make_index_sequence<TraitsT::ArgCount>{});
    }
}
