#pragma once

#include "lua.h"
#include "Containers/Function.h"
#include "Memory/SmartPtr.h"
#include "entt/entt.hpp"


namespace Lumina::Lua
{
    // A suspended Lua coroutine waiting to be resumed. BeginYield creates one and pins the running
    // thread; whatever completes the wait (a timer, an async load) holds the token and calls ResumeYield
    // exactly once. Destroying the last reference releases the registry pin -- so dropping the token
    // without resuming (e.g. the owning entity died and the timer was cleared) cleanly cancels the wait.
    struct RUNTIME_API FYieldToken
    {
        lua_State*   Co        = nullptr;       // the coroutine to resume
        lua_State*   MainState = nullptr;       // owns the registry ref used to pin Co
        int          ThreadRef = LUA_NOREF;
        entt::entity Owner     = entt::null;    // owning entity (for caller-side validity checks), or null
        bool         bResumed  = false;

        FYieldToken() = default;
        ~FYieldToken();

        FYieldToken(const FYieldToken&) = delete;
        FYieldToken& operator=(const FYieldToken&) = delete;
    };

    // Suspends the running coroutine L: pins it so GC can't reclaim it while parked, and captures its
    // owner entity from thread data. Returns null if L cannot yield (the caller should then luaL_error).
    // Usage: auto Tok = BeginYield(L); if (!Tok) luaL_error(...); <register completion -> ResumeYield(Tok)>;
    //        return lua_yield(L, 0);
    RUNTIME_API TSharedPtr<FYieldToken> BeginYield(lua_State* L);

    // Resumes the parked coroutine, pushing its return values via Pusher (which pushes onto the coroutine
    // and returns the count). Guarded: the first call resumes, later calls are no-ops, so a timeout racing
    // a completion can't double-resume. Pusher may be empty for a no-result resume.
    RUNTIME_API void ResumeYield(const TSharedPtr<FYieldToken>& Token, const TFunction<int(lua_State*)>& Pusher = {});
}
