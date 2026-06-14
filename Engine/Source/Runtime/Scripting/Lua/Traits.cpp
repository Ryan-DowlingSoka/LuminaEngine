#include "pch.h"
#include "Traits.h"
#include "Core/Assertions/Assert.h"
#include "Core/Threading/Thread.h"
#include "Core/Object/Object.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "lua.h"
#include "lualib.h"

namespace Lumina::Lua
{
    uint32 FTypeIndex::GetOrCreate(const char* UniqueTypeName)
    {
        static FMutex Mutex;
        static THashMap<FString, uint32> Ids;
        static uint32 GNextId = 1;

        FScopeLock Lock(Mutex);
        FString Key(UniqueTypeName);
        if (auto It = Ids.find(Key); It != Ids.end())
        {
            return It->second;
        }

        Ids.emplace(eastl::move(Key), GNextId);
        return GNextId++;
    }

    namespace
    {
        const char GTypeMTsKey = 0;

        void PushTypeMTsTable(lua_State* State)
        {
            lua_pushlightuserdata(State, const_cast<char*>(&GTypeMTsKey));
            lua_rawget(State, LUA_REGISTRYINDEX);
            if (!lua_istable(State, -1))
            {
                lua_pop(State, 1);
                lua_newtable(State);
                lua_pushlightuserdata(State, const_cast<char*>(&GTypeMTsKey));
                lua_pushvalue(State, -2);
                lua_rawset(State, LUA_REGISTRYINDEX);
            }
        }
    }

    void RegisterBoundDestructors(lua_State* State)
    {
        lua_setuserdatadtor(State, BoundTag_CObject, [](lua_State*, void* UD)
        {
            static_cast<TObjectPtr<CObject>*>(UD)->~TObjectPtr<CObject>();
        });

        lua_setuserdatadtor(State, BoundTag_Value, [](lua_State*, void* UD)
        {
            auto Fn = *reinterpret_cast<void(* const*)(void*)>(UD);
            if (Fn)
            {
                Fn(static_cast<char*>(UD) + sizeof(void*));
            }
        });
    }

    void StoreTypeMetatable(lua_State* State, uint32 TypeId)
    {
        PushTypeMTsTable(State);
        lua_pushvalue(State, -2);
        lua_rawseti(State, -2, static_cast<int>(TypeId));
        lua_pop(State, 1);
    }

    void SetTypeMetatable(lua_State* State, uint32 TypeId)
    {
        PushTypeMTsTable(State);
        lua_rawgeti(State, -1, static_cast<int>(TypeId));
        if (!lua_istable(State, -1))
        {
            lua_pop(State, 1);
            lua_newtable(State);
            lua_pushvalue(State, -1);
            lua_rawseti(State, -3, static_cast<int>(TypeId));
        }
        lua_remove(State, -2);
        lua_setmetatable(State, -2);
    }

    bool HasTypeMetatable(lua_State* State, int Index, uint32 TypeId)
    {
        const int Abs = lua_absindex(State, Index);
        if (!lua_getmetatable(State, Abs))
        {
            return false;
        }
        PushTypeMTsTable(State);
        lua_rawgeti(State, -1, static_cast<int>(TypeId));
        const bool bEqual = lua_rawequal(State, -3, -1) != 0;
        lua_pop(State, 3);
        return bEqual;
    }

    void* NewBoundUserdata(lua_State* State, size_t Size, int Tag, uint32 TypeId)
    {
        void* Block = lua_newuserdatatagged(State, Size, Tag);
        SetTypeMetatable(State, TypeId);
        return Block;
    }

    void* GetBoundUserdata(lua_State* State, int Index, uint32 TypeId)
    {
        if (!lua_isuserdata(State, Index))
        {
            return nullptr;
        }
        if (!HasTypeMetatable(State, Index, TypeId))
        {
            return nullptr;
        }
        return lua_touserdata(State, Index);
    }
}
