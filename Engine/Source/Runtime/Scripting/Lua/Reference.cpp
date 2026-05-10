#include "pch.h"
#include "Reference.h"


namespace Lumina::Lua
{
    FRef::FRef(FNil)
    { }

    FRef::FRef(lua_State* L, int Index)
        : State(lua_mainthread(L))
    {
        CachedType = (EType)lua_type(L, Index);
        Ref = lua_ref(L, Index);
        lua_pop(L, 1);
    }

    FRef::~FRef()
    {
        Reset();
    }

    FRef::FRef(const FRef& Other)
        : State(Other.State)
        , CachedType(Other.CachedType)
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
        CachedType = Other.CachedType;
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
    , CachedType(Other.CachedType)
    {
        Other.State = nullptr;
        Other.Ref   = LUA_NOREF;
        Other.CachedType = EType::Nil;
    }

    FRef& FRef::operator=(FRef&& Other) noexcept
    {
        if (this == &Other)
        {
            return *this;
        }

        Reset();

        State       = Other.State;
        Ref         = Other.Ref;
        CachedType  = Other.CachedType;

        Other.State = nullptr;
        Other.Ref   = LUA_NOREF;
        Other.CachedType = EType::Nil;

        return *this;
    }

    void FRef::Reset()
    {
        if (State && Ref != LUA_NOREF)
        {
            lua_unref(State, Ref);
            Ref         = LUA_NOREF;
            State       = nullptr;
            CachedType  = EType::Nil;
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
        return CachedType;
    }

    void FRef::Push() const
    {
        DEBUG_ASSERT(IsValid());
        lua_getref(State, Ref);
    }

    void FRef::Push(lua_State* TargetState) const
    {
        DEBUG_ASSERT(IsValid());
        lua_getref(TargetState, Ref);
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
        return IsValid() && CachedType == EType::Function;
    }

    bool FRef::IsTable() const
    {
        return IsValid() && CachedType == EType::Table;
    }

    bool FRef::IsUserdata(int Tag) const
    {
        // Userdata tag is per-instance, not encoded in lua_type, so still need a stack peek.
        if (!IsValid() || CachedType != EType::Userdata)
        {
            return false;
        }
        Push();
        bool bResult = lua_userdatatag(State, -1) == Tag;
        lua_pop(State, 1);
        return bResult;
    }
}
