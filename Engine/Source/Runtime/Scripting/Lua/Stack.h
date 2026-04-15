#pragma once
#include "lua.h"
#include "Traits.h"
#include <entt/entt.hpp>
#include "Core/Templates/Optional.h"
#include "UserDataHeader.h"
#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "Memory/Memcpy.h"


namespace Lumina::Lua
{
    class FRef;
    class FVariadicArgs;

    template<typename T>
    struct TLuaNativeType : eastl::false_type {};
    
    struct FNil {};

    // Specialize for types that have native Lua representations
    
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
    template<> struct TLuaNativeType<glm::vec2>         : eastl::true_type {};
    template<> struct TLuaNativeType<glm::vec3>         : eastl::true_type {};
    template<> struct TLuaNativeType<glm::vec4>         : eastl::true_type {};

    template<typename T>
    struct TStack {};
    
    //@ TODO Consolidate with concepts.
    
    template<>
    struct TStack<FNil>
    {
        static FStringView TypeName(lua_State* State)       { return lua_typename(State, LUA_TNIL); }
        static void Push(lua_State* State, FNil)            { lua_pushnil(State); }
        static FNil  Get(lua_State* State, int Index)       { return {}; }
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
            if (lua_isnil(State, Index))
            {
                return eastl::nullopt;
            }
            return TOptional<T>(TStack<T>::Get(State, Index));
        }

        static bool Check(lua_State* State, int Index)
        {
            return lua_isnil(State, Index) || TStack<T>::Check(State, Index);
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
        static double Get(lua_State* State, int Index)       { return (float)luaL_checknumber(State, Index); }
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
        static void Push(lua_State* State, FStringView Value) { lua_pushstring(State, Value.data()); }
        static FStringView Get(lua_State* State, int Index)   { return luaL_checkstring(State, Index); }
        static bool Check(lua_State* State, int Index)        { return lua_type(State, Index) == LUA_TSTRING; }
    };
    
    template<>
    struct TStack<FString>
    {
        static FStringView TypeName(lua_State* State)               { return lua_typename(State, LUA_TSTRING); }
        static void Push(lua_State* State, const FString& Value)    { lua_pushstring(State, Value.c_str()); }
        static FString Get(lua_State* State, int Index)             { return luaL_checkstring(State, Index); }
        static bool Check(lua_State* State, int Index)              { return lua_type(State, Index) == LUA_TSTRING; }
    };
    
    template<>
    struct TStack<std::string>
    {
        static FStringView TypeName(lua_State* State)                   { return lua_typename(State, LUA_TSTRING); }
        static void Push(lua_State* State, const std::string& Value)    { lua_pushstring(State, Value.c_str()); }
        static std::string Get(lua_State* State, int Index)             { return luaL_checkstring(State, Index); }
        static bool Check(lua_State* State, int Index)                  { return lua_type(State, Index) == LUA_TSTRING; }
    };
    
    
    template<>
    struct TStack<FFixedString>
    {
        static FStringView TypeName(lua_State* State)                   { return lua_typename(State, LUA_TSTRING); }
        static void Push(lua_State* State, const FFixedString& Value)   { lua_pushstring(State, Value.c_str()); }
        static FFixedString Get(lua_State* State, int Index)            { return luaL_checkstring(State, Index); }
        static bool Check(lua_State* State, int Index)                  { return lua_type(State, Index) == LUA_TSTRING; }
    };
    
    template<>
    struct TStack<FName>
    {
        static FStringView TypeName(lua_State* State)               { return lua_typename(State, LUA_TSTRING); }
        static void Push(lua_State* State, const FName& Value)      { lua_pushstring(State, Value.c_str()); }
        static FName Get(lua_State* State, int Index)               { return luaL_checkstring(State, Index); }
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
    struct TStack<glm::vec2>
    {
        static FStringView TypeName(lua_State* State) { return lua_typename(State, LUA_TVECTOR); }

        static void Push(lua_State* State, const glm::vec2& Value)
        {
            lua_pushvector(State, Value.x, Value.y, 0.0f, 0.0f);
        }
    
        static glm::vec2 Get(lua_State* State, int Index)
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
    struct TStack<glm::vec3>
    {
        static FStringView TypeName(lua_State* State) { return lua_typename(State, LUA_TVECTOR); }
        static void Push(lua_State* State, const glm::vec3& Value)
        {
            lua_pushvector(State, Value.x, Value.y, Value.z, 0.0f);
        }
    
        static glm::vec3 Get(lua_State* State, int Index)
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
    struct TStack<glm::vec4>
    {
        static FStringView TypeName(lua_State* State) { return lua_typename(State, LUA_TVECTOR); }
        static void Push(lua_State* State, const glm::vec4& Value)
        {
            lua_pushvector(State, Value.x, Value.y, Value.z, Value.w);
        }
    
        static glm::vec4 Get(lua_State* State, int Index)
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

            TVector<T> Vector;
            Vector.reserve(static_cast<std::size_t>(lua_objlen(State, Index)));

            int const ABSIndex = lua_absindex(State, Index);
            lua_pushnil(State);
            while (lua_next(State, ABSIndex) != 0)
            {
                Vector.push_back(TStack<T>::Get(State, -1));
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
    requires(eastl::is_arithmetic_v<T>)
    struct TStack<TVector<T>>
    {
        static FStringView TypeName(lua_State* State) { return lua_typename(State, LUA_TBUFFER); }
        
        static void Push(lua_State* State, const TVector<T>& Value)
        {
            void* Buffer = lua_newbuffer(State, Value.size());
            Memory::Memcpy(Buffer, Value.data(), Value.size() * sizeof(T));
        }
        
        static TVector<T> Get(lua_State* State, int Index)
        {
            size_t Size = 0;
            void* Buffer = luaL_checkbuffer(State, Index, &Size);
        
            TVector<T> Result(Size / sizeof(T));
            Memory::Memcpy(Result.data(), Buffer, Size * sizeof(T));
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
            void* Block = lua_newuserdatataggedwithmetatable(State, sizeof(StorageT), TClassTraits<RawT>::Tag());
            auto* Header = new (Block) StorageT{};
            Header->SetExternal(Ptr);
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
}
