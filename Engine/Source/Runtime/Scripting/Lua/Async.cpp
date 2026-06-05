#include "pch.h"
#include "Async.h"
#include "ScriptTypes.h"
#include "lua.h"
#include "lualib.h"
#include "Log/Log.h"


namespace Lumina::Lua
{
    FYieldToken::~FYieldToken()
    {
        if (MainState != nullptr && ThreadRef != LUA_NOREF)
        {
            lua_unref(MainState, ThreadRef);
        }
    }

    TSharedPtr<FYieldToken> BeginYield(lua_State* L)
    {
        if (!lua_isyieldable(L))
        {
            return nullptr;
        }

        TSharedPtr<FYieldToken> Token = MakeShared<FYieldToken>();
        Token->Co        = L;
        Token->MainState = lua_mainthread(L);

        // Pin the running coroutine in the registry so GC can't reclaim it while it's parked. The ref
        // lives on the main state (guaranteed to outlive the sub-thread), matching ~FYieldToken's unref.
        lua_pushthread(L);
        Token->ThreadRef = lua_ref(L, -1);
        lua_pop(L, 1);

        if (const auto* ThreadData = static_cast<const FScriptThreadData*>(lua_getthreaddata(L)))
        {
            Token->Owner = ThreadData->Entity;
        }

        return Token;
    }

    void ResumeYield(const TSharedPtr<FYieldToken>& Token, const TFunction<int(lua_State*)>& Pusher)
    {
        if (!Token || Token->bResumed || Token->Co == nullptr)
        {
            return;
        }
        Token->bResumed = true;

        const int NumResults = Pusher ? Pusher(Token->Co) : 0;

        const int Status = lua_resume(Token->Co, nullptr, NumResults);
        if (Status != LUA_OK && Status != LUA_YIELD && Status != LUA_BREAK)
        {
            const char* ErrMsg = lua_tostring(Token->Co, -1);
            LOG_ERROR("[Lua] - Coroutine resume failed: {}", ErrMsg ? ErrMsg : "<unknown>");
        }
    }
}
