#include "pch.h"
#include "Reference.h"


namespace Lumina::Lua
{
    FRef::FRef(FNil)
    { }

    FRef::FRef(lua_State* L, int Index)
        : State(L)
    {
        Ref = lua_ref(L, Index);
        lua_pop(L, 1);
    }

    FRef::~FRef()
    {
        Reset();
    }

    FRef::FRef(const FRef& Other)
        : State(Other.State)
    {
        if (State && Other.Ref != LUA_NOREF)
        {
            lua_getref(State, Other.Ref);
            Ref = lua_ref(State, -1);
            lua_pop(State, 1);
        }
    }

    FRef& FRef::operator=(const FRef& Other)
    {
        if (this == &Other)
        {
            return *this;
        }
            
        Reset();
        State = Other.State;
        if (State && Other.Ref != LUA_NOREF)
        {
            lua_getref(State, Other.Ref);
            Ref = lua_ref(State, -1);
            lua_pop(State, 1);
        }
            
        return *this;
    }

    FRef::FRef(FRef&& Other) noexcept
    : State(Other.State)
    , Ref(Other.Ref)
    {
        Other.State = nullptr;
        Other.Ref   = LUA_NOREF;
    }

    FRef& FRef::operator=(FRef&& Other) noexcept
    {
        if (this == &Other)
        {
            return *this;
        }
            
        Reset();
        
        State   = Other.State;
        Ref     = Other.Ref;
            
        Other.State = nullptr;
        Other.Ref   = LUA_NOREF;
            
        return *this;
    }

    void FRef::Reset()
    {
        if (State && Ref != LUA_NOREF)
        {
            lua_unref(State, Ref);
            Ref     = LUA_NOREF;
            State   = nullptr;
        }
    }

    FString FRef::ToString() const
    {
        Push();

        size_t Length = 0;
        FString View = luaL_tolstring(State, -1, &Length);
        lua_pop(State, 2);
        return View;
    }

    EType FRef::GetType() const
    {
        Push();
        
        int Type = lua_type(State, -1);
        
        lua_pop(State, 1);
        return (EType)Type;
    }

    void FRef::Push() const
    {
        DEBUG_ASSERT(IsValid());
        lua_getref(State, Ref);
    }

    FRef FRef::NewTable(FStringView Key) const
    {
        Push();
        lua_newtable(State);
        lua_pushvalue(State, -1);
        lua_rawsetfield(State, -3, Key.data());
        lua_remove(State, -2);
        return FRef(State, -1);
    }

    bool FRef::IsValid() const
    {
        return State != nullptr && Ref != LUA_NOREF && Ref != LUA_REFNIL;
    }

    FRef FRef::GetField(FStringView Key) const
    {
        Push();
        lua_getfield(State, -1, Key.data());
        lua_remove(State, -2);
        return FRef(State, -1);
    }

    FRef FRef::GetIndex(int Index) const
    {
        Push();
        lua_rawgeti(State, -1, Index);
        lua_remove(State, -2);
        return FRef(State, -1);
    }

    FRef FRef::RawGetField(FStringView Key) const
    {
        Push();
        lua_rawgetfield(State, -1, Key.data());
        lua_remove(State, -2);
        return FRef(State, -1);
    }

    bool FRef::IsInvokable() const
    {
        Push();

        bool Result = lua_isfunction(State, -1);
        lua_pop(State, 1);
        return Result;
    }

    bool FRef::IsTable() const
    {
        Push();
        
        bool Result = lua_istable(State, -1);
        lua_pop(State, 1);
        return Result;
    }

    bool FRef::IsUserdata(int Tag) const
    {
        Push();
        
        bool bResult = lua_userdatatag(State, -1) == Tag;
        lua_pop(State, 1);
        return bResult;
    }
}
