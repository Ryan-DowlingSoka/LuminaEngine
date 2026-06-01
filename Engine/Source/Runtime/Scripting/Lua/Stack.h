#pragma once
#include "lua.h"
#include "lualib.h"
#include "Traits.h"
#include <memory>
#include <entt/entt.hpp>
#include "Core/Templates/Optional.h"
#include "Core/Object/Object.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "UserDataHeader.h"
#include "Containers/Array.h"
#include "Containers/Function.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "Memory/Memcpy.h"
#include "Memory/SmartPtr.h"
#include "Log/Log.h"


namespace Lumina::Lua
{
    class FRef;
    class FVariadicArgs;

    template<typename T>
    struct TLuaNativeType : eastl::false_type {};
    
    struct FNil {};

    template<typename T>
    struct TLuaNativeType<TOptional<T>>                 : eastl::true_type {};
    
    template<> struct TLuaNativeType<FNil>              : eastl::true_type {};
    template<> struct TLuaNativeType<int>               : eastl::true_type {};
    template<> struct TLuaNativeType<float>             : eastl::true_type {};
    template<> struct TLuaNativeType<double>            : eastl::true_type {};
    template<> struct TLuaNativeType<int8>              : eastl::true_type {};
    template<> struct TLuaNativeType<uint8>             : eastl::true_type {};
    template<> struct TLuaNativeType<uint32>            : eastl::true_type {};
    template<> struct TLuaNativeType<int64>             : eastl::true_type {};
    template<> struct TLuaNativeType<uint64>            : eastl::true_type {};
    template<> struct TLuaNativeType<FVariadicArgs>     : eastl::true_type {};
    template<> struct TLuaNativeType<FRef>              : eastl::true_type {};
    template<> struct TLuaNativeType<bool>              : eastl::true_type {};
    template<> struct TLuaNativeType<FString>           : eastl::true_type {};
    template<> struct TLuaNativeType<std::string>       : eastl::true_type {};
    template<> struct TLuaNativeType<FStringView>       : eastl::true_type {};
    template<> struct TLuaNativeType<FName>             : eastl::true_type {};
    template<> struct TLuaNativeType<const char*>       : eastl::true_type {};
    template<> struct TLuaNativeType<FVector2>         : eastl::true_type {};
    template<> struct TLuaNativeType<FVector3>         : eastl::true_type {};
    template<> struct TLuaNativeType<FVector4>         : eastl::true_type {};

    // Marks a parameter as supplied by the execution context (lua_State* or thread data), not read
    // positionally; the invoker fills it without advancing the stack index. Specialize with static T Get(lua_State*).
    template<typename T>
    struct TLuaContext : eastl::false_type {};

    template<>
    struct TLuaContext<lua_State*> : eastl::true_type
    {
        static lua_State* Get(lua_State* State) { return State; }
    };

    template<typename T>
    struct TStack {};
    
    //@ TODO Consolidate with concepts.
    
    template<>
    struct TStack<FNil>
    {
        static FStringView TypeName(lua_State* State)       { return lua_typename(State, LUA_TNIL); }
        static void Push(lua_State* State, FNil)            { lua_pushnil(State); }
        static FNil  Get(lua_State* State, int Index)       { return {}; }
        static TOptional<FNil>  TryGet(lua_State* State, int Index)       { return eastl::nullopt; }

        static bool Check(lua_State* State, int Index)      { return lua_isnil(State, Index); }
    };
    
    template<typename T>
    struct TStack<TOptional<T>>
    {
        static FStringView TypeName(lua_State* State)
        {
            return "optional";
        }

        static void Push(lua_State* State, TOptional<T> Value)
        {
            if (Value.has_value())
            {
                TStack<T>::Push(State, Value.value());
            }
            else
            {
                lua_pushnil(State);
            }
        }

        static TOptional<T> Get(lua_State* State, int Index)
        {
            // None (argument omitted) as well as nil resolve to an empty optional, so a trailing
            // optional parameter can simply be left off at the call site.
            if (lua_isnoneornil(State, Index))
            {
                return eastl::nullopt;
            }
            return TOptional<T>(TStack<T>::Get(State, Index));
        }

        static bool Check(lua_State* State, int Index)
        {
            return lua_isnoneornil(State, Index) || TStack<T>::Check(State, Index);
        }
    };
    
    template<>
    struct TStack<int8>
    {
        static FStringView TypeName(lua_State* State)       { return lua_typename(State, LUA_TNUMBER); }
        static void Push(lua_State* State, int Value)       { lua_pushinteger(State, Value); }
        static int  Get(lua_State* State, int Index)        { return luaL_checkinteger(State, Index); }
        static bool Check(lua_State* State, int Index)      { return lua_isnumber(State, Index); }
    };
    
    template<>
    struct TStack<uint8>
    {
        static FStringView TypeName(lua_State* State)       { return lua_typename(State, LUA_TNUMBER); }
        static void Push(lua_State* State, int Value)       { lua_pushunsigned(State, Value); }
        static int  Get(lua_State* State, int Index)        { return luaL_checkunsigned(State, Index); }
        static bool Check(lua_State* State, int Index)      { return lua_isnumber(State, Index); }
    };
    
    template<>
    struct TStack<int16>
    {
        static FStringView TypeName(lua_State* State)       { return lua_typename(State, LUA_TNUMBER); }
        static void Push(lua_State* State, int Value)       { lua_pushinteger(State, Value); }
        static int  Get(lua_State* State, int Index)        { return luaL_checkinteger(State, Index); }
        static bool Check(lua_State* State, int Index)      { return lua_isnumber(State, Index); }
    };
    
    template<>
    struct TStack<uint16>
    {
        static FStringView TypeName(lua_State* State)       { return lua_typename(State, LUA_TNUMBER); }
        static void Push(lua_State* State, int Value)       { lua_pushunsigned(State, Value); }
        static int  Get(lua_State* State, int Index)        { return luaL_checkunsigned(State, Index); }
        static bool Check(lua_State* State, int Index)      { return lua_isnumber(State, Index); }
    };
    
    template<>
    struct TStack<int32>
    {
        static FStringView TypeName(lua_State* State)       { return lua_typename(State, LUA_TNUMBER); }
        static void Push(lua_State* State, int Value)       { lua_pushinteger(State, Value); }
        static int  Get(lua_State* State, int Index)        { return luaL_checkinteger(State, Index); }
        static bool Check(lua_State* State, int Index)      { return lua_isnumber(State, Index); }
    };
    
    template<>
    struct TStack<uint32>
    {
        static FStringView TypeName(lua_State* State)           { return lua_typename(State, LUA_TNUMBER); }
        static void Push(lua_State* State, unsigned Value)      { lua_pushunsigned(State, Value); }
        static unsigned  Get(lua_State* State, int Index)       { return luaL_checkunsigned(State, Index); }
        static bool Check(lua_State* State, int Index)          { return lua_isnumber(State, Index); }
    };
    
    template<>
    struct TStack<int64>
    {
        static FStringView TypeName(lua_State* State)       { return lua_typename(State, LUA_TNUMBER); }
        static void Push(lua_State* State, int Value)       { lua_pushinteger(State, Value); }
        static int  Get(lua_State* State, int Index)        { return luaL_checkinteger(State, Index); }
        static bool Check(lua_State* State, int Index)      { return lua_isnumber(State, Index); }
    };
    
    template<>
    struct TStack<uint64>
    {
        static FStringView TypeName(lua_State* State)       { return lua_typename(State, LUA_TNUMBER); }
        static void Push(lua_State* State, int Value)       { lua_pushunsigned(State, Value); }
        static int  Get(lua_State* State, int Index)        { return luaL_checkunsigned(State, Index); }
        static bool Check(lua_State* State, int Index)      { return lua_isnumber(State, Index); }
    };

    template<>
    struct TStack<float>
    {
        static FStringView TypeName(lua_State* State)       { return lua_typename(State, LUA_TNUMBER); }
        static void Push(lua_State* State, float Value)     { lua_pushnumber(State, Value); }
        static float Get(lua_State* State, int Index)       { return (float)luaL_checknumber(State, Index); }
        static bool Check(lua_State* State, int Index)      { return lua_isnumber(State, Index); }
    };
    
    template<>
    struct TStack<double>
    {
        static FStringView TypeName(lua_State* State)        { return lua_typename(State, LUA_TNUMBER); }
        static void Push(lua_State* State, double Value)     { lua_pushnumber(State, Value); }
        static double Get(lua_State* State, int Index)       { return luaL_checknumber(State, Index); }
        static bool Check(lua_State* State, int Index)       { return lua_isnumber(State, Index); }
    };
    
    template<>
    struct TStack<bool>
    {
        static FStringView TypeName(lua_State* State)   { return lua_typename(State, LUA_TBOOLEAN); }
        static void Push(lua_State* State, bool Value)  { lua_pushboolean(State, Value); }
        static bool Get(lua_State* State, int Index)    { return luaL_checkboolean(State, Index); }
        static bool Check(lua_State* State, int Index)  { return lua_isboolean(State, Index); }
    };

    template<>
    struct TStack<const char*>
    {
        static FStringView TypeName(lua_State* State)         { return lua_typename(State, LUA_TSTRING); }
        static void Push(lua_State* State, const char* Value) { lua_pushstring(State, Value); }
        static const char* Get(lua_State* State, int Index)   { return luaL_checkstring(State, Index); }
        static bool Check(lua_State* State, int Index)        { return lua_type(State, Index) == LUA_TSTRING; }
    };
    
    template<>
    struct TStack<FStringView>
    {
        static FStringView TypeName(lua_State* State)         { return lua_typename(State, LUA_TSTRING); }
        static void Push(lua_State* State, FStringView Value) { lua_pushlstring(State, Value.data(), Value.size()); }
        static FStringView Get(lua_State* State, int Index)
        {
            size_t Len = 0;
            const char* Str = luaL_checklstring(State, Index, &Len);
            return FStringView(Str, Len);
        }
        static bool Check(lua_State* State, int Index)        { return lua_type(State, Index) == LUA_TSTRING; }
    };

    template<>
    struct TStack<FString>
    {
        static FStringView TypeName(lua_State* State)               { return lua_typename(State, LUA_TSTRING); }
        static void Push(lua_State* State, const FString& Value)    { lua_pushlstring(State, Value.data(), Value.size()); }
        static FString Get(lua_State* State, int Index)
        {
            size_t Len = 0;
            const char* Str = luaL_checklstring(State, Index, &Len);
            return FString(Str, Len);
        }
        static bool Check(lua_State* State, int Index)              { return lua_type(State, Index) == LUA_TSTRING; }
    };

    template<>
    struct TStack<std::string>
    {
        static FStringView TypeName(lua_State* State)                   { return lua_typename(State, LUA_TSTRING); }
        static void Push(lua_State* State, const std::string& Value)    { lua_pushlstring(State, Value.data(), Value.size()); }
        static std::string Get(lua_State* State, int Index)
        {
            size_t Len = 0;
            const char* Str = luaL_checklstring(State, Index, &Len);
            return std::string(Str, Len);
        }
        static bool Check(lua_State* State, int Index)                  { return lua_type(State, Index) == LUA_TSTRING; }
    };


    template<>
    struct TStack<FFixedString>
    {
        static FStringView TypeName(lua_State* State)                   { return lua_typename(State, LUA_TSTRING); }
        static void Push(lua_State* State, const FFixedString& Value)   { lua_pushlstring(State, Value.data(), Value.length()); }
        static FFixedString Get(lua_State* State, int Index)
        {
            size_t Len = 0;
            const char* Str = luaL_checklstring(State, Index, &Len);
            return FFixedString(Str, Len);
        }
        static bool Check(lua_State* State, int Index)                  { return lua_type(State, Index) == LUA_TSTRING; }
    };

    template<>
    struct TStack<FName>
    {
        static FStringView TypeName(lua_State* State)               { return lua_typename(State, LUA_TSTRING); }
        // FName has no stored length, Length() does its own strlen, so just let
        // lua_pushstring do the single strlen instead of paying it twice.
        static void Push(lua_State* State, const FName& Value)      { lua_pushstring(State, Value.c_str()); }
        static FName Get(lua_State* State, int Index)
        {
            size_t Len = 0;
            const char* Str = luaL_checklstring(State, Index, &Len);
            return FName(Str, Len);
        }
        static bool Check(lua_State* State, int Index)              { return lua_type(State, Index) == LUA_TSTRING; }
    };
    
    template<>
    struct TStack<entt::entity>
    {
        static FStringView TypeName(lua_State* State)                   { return lua_typename(State, LUA_TNUMBER); }
        static void Push(lua_State* State, const entt::entity& Value)   { lua_pushinteger(State, (int)entt::to_integral(Value)); }
        static entt::entity Get(lua_State* State, int Index)            { return static_cast<entt::entity>(luaL_checkinteger(State, Index)); }
        static bool Check(lua_State* State, int Index)                  { return lua_isnumber(State, Index); }
    };
    
    template<>
    struct TStack<FVector2>
    {
        static FStringView TypeName(lua_State* State) { return lua_typename(State, LUA_TVECTOR); }

        static void Push(lua_State* State, const FVector2& Value)
        {
            lua_pushvector(State, Value.x, Value.y, 0.0f, 0.0f);
        }
    
        static FVector2 Get(lua_State* State, int Index)
        {
            const float* V = luaL_checkvector(State, Index);
            return { V[0], V[1] };
        }
    
        static bool Check(lua_State* State, int Index)
        {
            return lua_isvector(State, Index);
        }
    };
    
    template<>
    struct TStack<FVector3>
    {
        static FStringView TypeName(lua_State* State) { return lua_typename(State, LUA_TVECTOR); }
        static void Push(lua_State* State, const FVector3& Value)
        {
            lua_pushvector(State, Value.x, Value.y, Value.z, 0.0f);
        }
    
        static FVector3 Get(lua_State* State, int Index)
        {
            const float* V = luaL_checkvector(State, Index);
            return { V[0], V[1], V[2] };
        }
    
        static bool Check(lua_State* State, int Index)
        {
            return lua_isvector(State, Index);
        }
    };
    
    template<>
    struct TStack<FVector4>
    {
        static FStringView TypeName(lua_State* State) { return lua_typename(State, LUA_TVECTOR); }
        static void Push(lua_State* State, const FVector4& Value)
        {
            lua_pushvector(State, Value.x, Value.y, Value.z, Value.w);
        }
    
        static FVector4 Get(lua_State* State, int Index)
        {
            const float* V = luaL_checkvector(State, Index);
            return { V[0], V[1], V[2], V[3]};
        }
    
        static bool Check(lua_State* State, int Index)
        {
            return lua_isvector(State, Index);
        }
    };
    
    template<typename... T>
    struct TStack<TTuple<T...>>
    {
        static FStringView TypeName(lua_State* State)
        {
            return lua_typename(State, LUA_TTABLE);
        }

        static void Push(lua_State* State, const TTuple<T...>& Value)
        {
            lua_createtable(State, sizeof...(T), 0);

            auto Dispatch = [&]<size_t... Is>(eastl::index_sequence<Is...>)
            {
                ((
                    TStack<eastl::tuple_element_t<Is, TTuple<T...>>>::Push(State, eastl::get<Is>(Value)),
                    lua_rawseti(State, -2, static_cast<lua_Integer>(Is + 1))
                ), ...);
            };

            Dispatch(eastl::make_index_sequence<sizeof...(T)>{});
        }

        static TTuple<T...> Get(lua_State* State, int Index)
        {
            luaL_checktype(State, Index, LUA_TTABLE);

            return GetImpl(State, Index, eastl::make_index_sequence<sizeof...(T)>{});
        }

    private:
        template<size_t... Is>
        static TTuple<T...> GetImpl(lua_State* State, int Index, eastl::index_sequence<Is...>)
        {
            return TTuple<T...>{(GetElement<Is>(State, Index))...};
        }

        template<size_t I>
        static auto GetElement(lua_State* State, int Index)
        {
            lua_rawgeti(State, Index, static_cast<lua_Integer>(I + 1));
            using ElemT = eastl::tuple_element_t<I, TTuple<T...>>;
            auto value = TStack<ElemT>::Get(State, -1);
            lua_pop(State, 1);
            return value;
        }

        static bool Check(lua_State* State, int Index)
        {
            return lua_istable(State, Index);
        }
    };
    
    template<typename T>
    requires(!eastl::is_arithmetic_v<T>)
    struct TStack<TVector<T>>
    {
        static FStringView TypeName(lua_State* State) { return lua_typename(State, LUA_TTABLE); }
        
        static void Push(lua_State* State, const TVector<T>& Value)
        {
            lua_createtable(State, static_cast<int>(Value.size()), 0);
            for (std::size_t i = 0; i < Value.size(); ++i)
            {
                TStack<T>::Push(State, Value[i]);
                lua_rawseti(State, -2, static_cast<lua_Integer>(i + 1));
            }
        }
        
        static TVector<T> Get(lua_State* State, int Index)
        {
            if (!lua_istable(State, Index))
            {
                luaL_error(State, "#%d Argument must be a table", Index);
            }

            const int ABSIndex  = lua_absindex(State, Index);
            const int Count     = lua_objlen(State, ABSIndex);

            TVector<T> Vector;
            Vector.reserve(static_cast<std::size_t>(Count));
            
            for (int i = 1; i <= Count; ++i)
            {
                lua_rawgeti(State, ABSIndex, i);
                Vector.emplace_back(TStack<T>::Get(State, -1));
                lua_pop(State, 1);
            }
            return Vector;
        }
        
        static bool Check(lua_State* State, int Index)
        {
            return lua_istable(State, Index);
        }
    };
    
    template<typename T>
    struct TStack<TVector<T>&> : TStack<TVector<T>> {};

    template<typename T>
    requires(eastl::is_arithmetic_v<T>)
    struct TStack<TVector<T>>
    {
        static FStringView TypeName(lua_State* State) { return lua_typename(State, LUA_TBUFFER); }
        
        static void Push(lua_State* State, const TVector<T>& Value)
        {
            const size_t Bytes = Value.size() * sizeof(T);
            void* Buffer = lua_newbuffer(State, Bytes);
            Memory::Memcpy(Buffer, Value.data(), Bytes);
        }

        static TVector<T> Get(lua_State* State, int Index)
        {
            size_t Size = 0;
            void* Buffer = luaL_checkbuffer(State, Index, &Size);

            TVector<T> Result(Size / sizeof(T));
            Memory::Memcpy(Result.data(), Buffer, Size);
            return Result;
        }
        
        static bool Check(lua_State* State, int Index)
        {
            return lua_isbuffer(State, Index);
        }
    };
    
    template<typename T>
    requires(!eastl::is_enum_v<T> && !eastl::is_pointer_v<T> && !eastl::is_reference_v<T>)
    struct TStack<T>
    {
        static FStringView TypeName(lua_State* State) { return lua_typename(State, LUA_TUSERDATA); }

        using RawT = eastl::remove_pointer_t<eastl::decay_t<T>>;
        using StorageT = TUserdataHeader<RawT>;

        template<typename... TArgs>
        requires(eastl::is_constructible_v<RawT, TArgs...>)
        static void Push(lua_State* State, TArgs&&... Args)
        {
            void* Block = lua_newuserdatataggedwithmetatable(State, sizeof(StorageT), TClassTraits<RawT>::Tag());
            auto* Header = new (Block) StorageT{};
            Header->External = nullptr;
            Header->Emplace(eastl::forward<TArgs>(Args)...);
        }

        static RawT& Get(lua_State* State, int Index)
        {
            auto* Userdata = static_cast<TUserdataHeader<RawT>*>(lua_touserdatatagged(State, Index, TClassTraits<RawT>::Tag()));
            DEBUG_ASSERT(Userdata, "Tagged userdata not found!");
            return *Userdata->Underlying();
        }

        static bool Check(lua_State* State, int Index)
        {
            return lua_userdatatag(State, Index) == TClassTraits<RawT>::Tag();
        }
    };
    
    template<typename T>
    struct TStack<T*>
    {
        static FStringView TypeName(lua_State* State) { return lua_typename(State, LUA_TUSERDATA); }

        using RawT = eastl::remove_pointer_t<eastl::decay_t<T>>;
        using StorageT = TUserdataHeader<RawT*>;

        static void Push(lua_State* State, T* Ptr)
        {
            if (Ptr == nullptr)
            {
                lua_pushnil(State);
                return;
            }

            if constexpr (eastl::is_base_of_v<CObject, RawT>)
            {
                // CObjects are owned by an embedded TObjectPtr (ctor takes a strong GC ref, the userdata
                // destructor releases it). It's a single RawT* at offset 0, so raw-pointer readers recover it.
                static_assert(sizeof(TObjectPtr<RawT>) == sizeof(RawT*), "TObjectPtr must be pointer-sized for offset-0 recovery");
                void* Block = lua_newuserdatataggedwithmetatable(State, sizeof(TObjectPtr<RawT>), TClassTraits<RawT>::Tag());
                new (Block) TObjectPtr<RawT>(Ptr);
            }
            else
            {
                // Non-owning raw pointer (engine-managed lifetime, e.g. component refs / subsystems).
                void* Block = lua_newuserdatataggedwithmetatable(State, sizeof(RawT*), TClassTraits<RawT>::Tag());
                *static_cast<RawT**>(Block) = Ptr;
            }
        }

        static RawT* Get(lua_State* State, int Index)
        {
            auto* Header = static_cast<StorageT*>(lua_touserdatatagged(State, Index, TClassTraits<RawT>::Tag()));
            if (!ALERT_IF_NOT(Header, "Type is not registered as a userdata for Luau"))
            {
                return nullptr;
            }

            return Header->Underlying();
        }

        static bool Check(lua_State* State, int Index)
        {
            return lua_userdatatag(State, Index) == TClassTraits<RawT>::Tag();
        }
    };
    
    // A strong handle marshals exactly like the object pointer it owns: the pushed userdata embeds
    // its own TObjectPtr (one ref), and a C++ side reading it back gets a fresh owning handle.
    template<typename T>
    struct TStack<TObjectPtr<T>>
    {
        static FStringView TypeName(lua_State* State) { return lua_typename(State, LUA_TUSERDATA); }

        static void Push(lua_State* State, const TObjectPtr<T>& Handle) { TStack<T*>::Push(State, Handle.Get()); }
        static TObjectPtr<T> Get(lua_State* State, int Index)           { return TObjectPtr<T>(TStack<T*>::Get(State, Index)); }
        static bool Check(lua_State* State, int Index)                  { return TStack<T*>::Check(State, Index); }
    };

    template<>
    struct TStack<void*>
    {
        static FStringView TypeName(lua_State* State) { return lua_typename(State, LUA_TLIGHTUSERDATA); }

        static void Push(lua_State* State, void* Ptr)
        {
            lua_pushlightuserdata(State, Ptr);
        }
    
        static void* Get(lua_State* State, int Index)
        {
            return lua_tolightuserdata(State, Index);
        }
    
        static bool Check(lua_State* State, int Index)
        {
            return lua_islightuserdata(State, Index);
        }
    };
    
    template<>
    struct TStack<std::nullptr_t>
    {
        static FStringView TypeName(lua_State* State)       { return lua_typename(State, LUA_TNIL); }
        static void Push(lua_State* State, std::nullptr_t)  { lua_pushnil(State); }
        static bool Check(lua_State* State, int Index)      { return lua_isnil(State, Index); }
    };
    
    template<typename T>
    struct TStack<T&>
    {
        static FStringView TypeName(lua_State* State) { return lua_typename(State, LUA_TUSERDATA); }

        using BaseT = std::remove_const_t<T>;
        
        static void Push(lua_State* State, T& Ref)
        {
            BaseT* Ptr = const_cast<BaseT*>(&Ref);
            TStack<BaseT*>::Push(State, Ptr);
        }

        static T& Get(lua_State* State, int Index)
        {
            return *TStack<BaseT*>::Get(State, Index);
        }

        static bool Check(lua_State* State, int Index)
        {
            return TStack<BaseT*>::Check(State, Index);
        }
    };
    
    template<typename T>
    struct TStack<eastl::reference_wrapper<T>>
    {
        using BaseT = eastl::remove_const_t<T>;

        static void Push(lua_State* State, eastl::reference_wrapper<T> Ref)
        {
            TStack<BaseT*>::Push(State, &Ref.get());
        }

        static T& Get(lua_State* State, int Index)
        {
            return *TStack<BaseT*>::Get(State, Index);
        }

        static bool Check(lua_State* State, int Index)
        {
            return TStack<BaseT*>::Check(State, Index);
        }
    };
    
    template<typename T>
    requires(eastl::is_enum_v<T>)
    struct TStack<T>
    {
        static FStringView TypeName(lua_State* State)   { return lua_typename(State, LUA_TNUMBER); }
        static void Push(lua_State* State, T Value)     { lua_pushinteger(State, (int)Value); }
        static T  Get(lua_State* State, int Index)      { return (T)luaL_checkinteger(State, Index); }
        static bool Check(lua_State* State, int Index)  { return lua_isnumber(State, Index); }
    };

    template<typename TParam>
    static decltype(auto) GetArg(lua_State* L, int Index);

    template<typename TParam>
    static void PushArg(lua_State* L, TParam&& Param);

    template<typename R, typename... Args>
    struct TLuaNativeType<TFunction<R(Args...)>> : eastl::true_type {};

    namespace FunctionStack_Private
    {
        struct FLuaCallbackRef
        {
            lua_State*  State   = nullptr;
            int         Ref     = LUA_NOREF;

            FLuaCallbackRef(lua_State* L, int Index)
                : State(L)
            {
                lua_pushvalue(L, Index);
                Ref = lua_ref(L, -1);
                lua_pop(L, 1);
            }

            ~FLuaCallbackRef()
            {
                if (State && Ref != LUA_NOREF && Ref != LUA_REFNIL)
                {
                    lua_unref(State, Ref);
                }
            }

            FLuaCallbackRef(const FLuaCallbackRef&) = delete;
            FLuaCallbackRef& operator=(const FLuaCallbackRef&) = delete;
        };
    }

    template<typename R, typename... Args>
    requires(eastl::is_void_v<R> || eastl::is_default_constructible_v<R>)
    struct TStack<TFunction<R(Args...)>>
    {
        using FuncT = TFunction<R(Args...)>;

        static FStringView TypeName(lua_State* State) { return lua_typename(State, LUA_TFUNCTION); }

        static int InvokeThunk(lua_State* L)
        {
            auto* Stored = static_cast<FuncT*>(lua_touserdata(L, lua_upvalueindex(1)));
            if (Stored == nullptr || !(*Stored))
            {
                luaL_errorL(L, "%s", "Attempted to invoke empty TFunction.");
            }

            auto Dispatch = [&]<size_t... Is>(eastl::index_sequence<Is...>) -> int
            {
                if constexpr (eastl::is_void_v<R>)
                {
                    (*Stored)(GetArg<eastl::tuple_element_t<Is, TTuple<Args...>>>(L, static_cast<int>(Is) + 1)...);
                    return 0;
                }
                else
                {
                    decltype(auto) Ret = (*Stored)(GetArg<eastl::tuple_element_t<Is, TTuple<Args...>>>(L, static_cast<int>(Is) + 1)...);
                    PushArg(L, eastl::forward<decltype(Ret)>(Ret));
                    return 1;
                }
            };

            return Dispatch(eastl::make_index_sequence<sizeof...(Args)>{});
        }

        static void Push(lua_State* State, const FuncT& Value)
        {
            PushImpl(State, FuncT(Value));
        }

        static void Push(lua_State* State, FuncT&& Value)
        {
            PushImpl(State, eastl::move(Value));
        }

        // Adapts a Lua function into a TFunction; copies share one pinned Lua ref.
        static FuncT Get(lua_State* State, int Index)
        {
            if (lua_isnil(State, Index))
            {
                return {};
            }

            if (!lua_isfunction(State, Index))
            {
                luaL_typeerrorL(State, Index, "function");
                return {};
            }

            auto Callback = MakeShared<FunctionStack_Private::FLuaCallbackRef>(State, Index);

            return [Callback](Args... args) -> R
            {
                lua_State* L = Callback->State;
                lua_getref(L, Callback->Ref);

                (TStack<eastl::decay_t<Args>>::Push(L, eastl::forward<Args>(args)), ...);

                constexpr int NumResults = eastl::is_void_v<R> ? 0 : 1;
                int Status = lua_pcall(L, static_cast<int>(sizeof...(Args)), NumResults, 0);
                if (Status != LUA_OK)
                {
                    const char* ErrMsg = lua_tostring(L, -1);
                    LOG_ERROR("[Lua] - TFunction invocation failed: {}", ErrMsg ? ErrMsg : "<unknown>");
                    lua_pop(L, 1);

                    if constexpr (!eastl::is_void_v<R>)
                    {
                        return R{};
                    }
                    else
                    {
                        return;
                    }
                }

                if constexpr (!eastl::is_void_v<R>)
                {
                    R Result = TStack<R>::Get(L, -1);
                    lua_pop(L, 1);
                    return Result;
                }
            };
        }

        static bool Check(lua_State* State, int Index)
        {
            return lua_isfunction(State, Index) || lua_isnil(State, Index);
        }

    private:

        static void PushImpl(lua_State* State, FuncT Value)
        {
            if (!Value)
            {
                lua_pushnil(State);
                return;
            }

            void* Block = lua_newuserdatadtor(State, sizeof(FuncT), [](void* UD)
            {
                static_cast<FuncT*>(UD)->~FuncT();
            });

            new (Block) FuncT(eastl::move(Value));

            lua_pushcclosure(State, &InvokeThunk, "TFunction", 1);
        }
    };

    // Raw fn-ptr values wrap into a TFunction so users can return `R(*)(Args...)` from bindings.
    template<typename R, typename... Args>
    struct TLuaNativeType<R(*)(Args...)> : eastl::true_type {};

    template<typename R, typename... Args>
    struct TStack<R(*)(Args...)>
    {
        using FnPtrT = R(*)(Args...);
        using FuncT  = TFunction<R(Args...)>;

        static FStringView TypeName(lua_State* State) { return lua_typename(State, LUA_TFUNCTION); }

        static void Push(lua_State* State, FnPtrT Ptr)
        {
            if (Ptr == nullptr)
            {
                lua_pushnil(State);
                return;
            }
            TStack<FuncT>::Push(State, FuncT(Ptr));
        }

        static FnPtrT Get(lua_State* State, int Index)
        {
            luaL_errorL(State, "%s", "Cannot convert a Lua function into a raw C++ function pointer (use TFunction instead).");
            return nullptr;
        }

        static bool Check(lua_State* State, int Index)
        {
            return lua_isfunction(State, Index) || lua_isnil(State, Index);
        }
    };
}
