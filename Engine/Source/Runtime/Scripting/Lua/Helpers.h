#pragma once
#include <lua.h>
#include "Stack.h"
#include "Containers/String.h"


namespace Lumina::Lua
{
    
    template<typename T>
    T GetGlobal(lua_State* L, FStringView Key)
    {
        lua_rawgetfield(L, LUA_GLOBALSINDEX, Key.data());
        
        auto Result = TStack<T>::Get(L, -1);
        
        lua_pop(L, 1);
        
        return Result;
    }
    
}
