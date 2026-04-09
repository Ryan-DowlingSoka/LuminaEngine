#include "pch.h"
#include "Reference.h"


namespace Lumina::Lua
{
    FRef::FRef(FNil)
        : State(nullptr)
        , Ref(LUA_NOREF)
    {
    }

    FRef::FRef(lua_State* L, int Index)
    : State(L)
    {
        Ref = lua_ref(L, Index);
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
        if (!Push())
        {
            return {};
        }
        
        size_t Length = 0;
        FString View = luaL_tolstring(State, -1, &Length);
        lua_pop(State, 2);
        return View;
    }

    EType FRef::GetType() const
    {
        if (!Push())
        {
            return {};
        }
        
        int Type = lua_type(State, -1);
        
        lua_pop(State, 1);
        return (EType)Type;
    }

    bool FRef::Push() const
    {
        if (!IsValid())
        {
            LOG_ERROR("[Lua] - Attempted to push an invalid lua reference");
            return false;
        }
            
        lua_getref(State, Ref);
        return true;
    }

    FRef FRef::NewTable(FStringView Key) const
    {
        if (!Push())
        {
            return {};
        }
        
        lua_newtable(State);
        
        lua_pushvalue(State, -1);
        lua_setfield(State, -3, Key.data());
        
        FRef Result(State, -1);
        lua_pop(State, 2);
        return Result;
    }

    bool FRef::IsValid() const
    {
        return State != nullptr && Ref != LUA_NOREF;
    }

    FRef FRef::GetField(FStringView Key) const
    {
        if (!Push())
        {
            return FNil{};
        }
        
        int Type = lua_getfield(State, -1, Key.data());
        if (Type <= LUA_TNIL)
        {
            lua_pop(State, 2);
            return FNil{};
        }
        
        FRef Result(State, -1);
        lua_pop(State, 2); // Pop value and table.
        return Result;
    }

    bool FRef::IsInvokable() const
    {
        if (!Push())
        {
            return false;
        }
        
        bool Result = lua_isfunction(State, -1);
        lua_pop(State, 1);
        return Result;
    }

    bool FRef::IsTable() const
    {
        if (!Push())
        {
            return false;
        }
        
        bool Result = lua_istable(State, -1);
        lua_pop(State, 1);
        return Result;
    }

    bool FRef::IsUserdata(int Tag) const
    {
        if (!Push())
        {
            return false;
        }
        
        bool bResult = lua_userdatatag(State, -1) == Tag;
        lua_pop(State, 1);
        return bResult;
    }
}
