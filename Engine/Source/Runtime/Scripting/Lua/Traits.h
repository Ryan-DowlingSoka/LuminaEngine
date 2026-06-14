#pragma once

#include "Core/Math/Hash/Hash.h"
#include "Containers/String.h"
#include "Containers/Tuple.h"
#include "Platform/GenericPlatform.h"

struct lua_State;

namespace Lumina::Lua
{
    enum EBoundTag : int
    {
        BoundTag_Plain   = 1,
        BoundTag_CObject = 2,
        BoundTag_Value   = 3,
    };

    enum ELightTag : int
    {
        LightTag_Self = 1,
    };

    template<typename T>
    struct TFunctionTraits;
    
    template<typename T>
    struct TFunctionTraits : TFunctionTraits<decltype(&T::operator())> {};
    
    template<typename TReturn, typename ... TArgs>
    struct TFunctionTraits<TReturn(*)(TArgs...)>
    {
        using ReturnType                    = TReturn;
        using ArgsTuple                     = TTuple<TArgs...>;
        static constexpr size_t ArgCount    = sizeof...(TArgs);
    };
    
    template<typename TReturn, typename TClass, typename... TArgs>
    struct TFunctionTraits<TReturn(TClass::*)(TArgs...)>
    {
        using ReturnType                 = TReturn;
        using ClassType                  = TClass;
        using ArgsTuple                  = TTuple<TArgs...>;
        static constexpr size_t ArgCount = sizeof...(TArgs);
    };

    template<typename TReturn, typename TClass, typename... TArgs>
    struct TFunctionTraits<TReturn(TClass::*)(TArgs...) const>
    {
        using ReturnType                 = TReturn;
        using ClassType                  = TClass;
        using ArgsTuple                  = TTuple<TArgs...>;
        static constexpr size_t ArgCount = sizeof...(TArgs);
    };
    
    struct FTypeIndex final
    {
        RUNTIME_API static uint32 GetOrCreate(const char* UniqueTypeName);
    };

    template<typename T>
    struct TClassTraits
    {
        static uint32 TypeId()
        {
            #if defined(_MSC_VER)
            static const uint32 SId = FTypeIndex::GetOrCreate(__FUNCSIG__);
            #else
            static const uint32 SId = FTypeIndex::GetOrCreate(__PRETTY_FUNCTION__);
            #endif
            return SId;
        }
    };

    RUNTIME_API void  RegisterBoundDestructors(lua_State* State);
    RUNTIME_API void  StoreTypeMetatable(lua_State* State, uint32 TypeId);
    RUNTIME_API void  SetTypeMetatable(lua_State* State, uint32 TypeId);
    RUNTIME_API bool  HasTypeMetatable(lua_State* State, int Index, uint32 TypeId);
    RUNTIME_API void* NewBoundUserdata(lua_State* State, size_t Size, int Tag, uint32 TypeId);
    RUNTIME_API void* GetBoundUserdata(lua_State* State, int Index, uint32 TypeId);
    
    enum class EMetaMethod : uint8
    {
        Index,          // __index
        NewIndex,       // __newindex
        ToString,       // __tostring
        Add,            // __add
        Sub,            // __sub
        Mul,            // __mul
        Div,            // __div
        Mod,            // __mod
        UnaryMinus,     // __unm
        Eq,             // __eq
        Lt,             // __lt
        Le,             // __le
        Len,            // __len
        Iter,           // __iter
        Call,           // __call
        NameCall,       // __namecall
    };
    
    constexpr FStringView MetaMethodName(EMetaMethod M) noexcept
    {
        switch (M)
        {
            case EMetaMethod::Index:        return "__index";
            case EMetaMethod::NewIndex:     return "__newindex";
            case EMetaMethod::ToString:     return "__tostring";
            case EMetaMethod::Add:          return "__add";
            case EMetaMethod::Sub:          return "__sub";
            case EMetaMethod::Mul:          return "__mul";
            case EMetaMethod::Div:          return "__div";
            case EMetaMethod::Mod:          return "__mod";
            case EMetaMethod::UnaryMinus:   return "__unm";
            case EMetaMethod::Eq:           return "__eq";
            case EMetaMethod::Lt:           return "__lt";
            case EMetaMethod::Le:           return "__le";
            case EMetaMethod::Len:          return "__len";
            case EMetaMethod::Iter:         return "__iter";
            case EMetaMethod::Call:         return "__call";
            case EMetaMethod::NameCall:     return "__namecall";
            default:                        return "__unknown";
        }
    }
    
}
