#pragma once

#include "Reference.h"
#include "ScriptExports.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "entt/entt.hpp"
#include "lua.h"
#include "lualib.h"

namespace Lumina
{
    class CWorld;
}

namespace Lumina::Lua
{
    // Per-thread context published via lua_setthreaddata so yield-aware APIs find their owning entity/world.
    // Owned by FScript for stable address.
    struct FScriptThreadData
    {
        entt::entity Entity = entt::null;
        CWorld*      World  = nullptr;
    };

    struct FScript
    {
        FName                           Name;
        FString                         Path;
        FRef                            Reference;
        FRef                            Environment;
        FRef                            Thread;
        FRef                            MainFunction;

        FScriptExportSchema             ExportsSchema;
        TVector<FScriptPropertyEntry>   ExportDefaults;
        FScriptThreadData               ThreadData;
        
        lua_State*                      PooledCoroutine    = nullptr;
        int                             PooledCoroutineRef = LUA_NOREF;

        bool                            bDirty = false;

        FScript() = default;
        ~FScript()
        {
            if (PooledCoroutine != nullptr && PooledCoroutineRef != LUA_NOREF)
            {
                if (lua_State* MainState = Reference.GetState())
                {
                    lua_unref(MainState, PooledCoroutineRef);
                }
            }
        }

        FScript(const FScript&) = delete;
        FScript& operator=(const FScript&) = delete;
        FScript(FScript&&) = delete;
        FScript& operator=(FScript&&) = delete;
        
        template<typename... Args>
        ECoroutineStatus InvokeAsCoroutine(const FRef& Func, Args&&... args)
        {
            if (!Func.IsValid())
            {
                return ECoroutineStatus::Error;
            }

            lua_State* MainState = Func.GetState();
            if (MainState == nullptr)
            {
                return ECoroutineStatus::Error;
            }

            if (PooledCoroutine == nullptr)
            {
                PooledCoroutine = lua_newthread(MainState);
                PooledCoroutineRef = lua_ref(MainState, -1);
                lua_pop(MainState, 1);
                lua_setthreaddata(PooledCoroutine, &ThreadData);
            }

            const int CoStatus = lua_costatus(MainState, PooledCoroutine);
            const bool bUsePool = (CoStatus != LUA_COSUS && CoStatus != LUA_CORUN);

            lua_State* SubThread = nullptr;
            int        TempRef   = LUA_NOREF;

            if (bUsePool)
            {
                lua_resetthread(PooledCoroutine);
                // resetthread clears thread-data; re-publish so yield-aware APIs find context.
                lua_setthreaddata(PooledCoroutine, &ThreadData);
                SubThread = PooledCoroutine;
            }
            else
            {
                SubThread = lua_newthread(MainState);
                TempRef = lua_ref(MainState, -1);
                lua_pop(MainState, 1);
                lua_setthreaddata(SubThread, &ThreadData);
            }

            ECoroutineStatus Status = Func.InvokeAsCoroutineOn(SubThread, eastl::forward<Args>(args)...);

            // Pool path: leave the thread; next acquire will reset (or skip if still yielded).
            // Temp path: yielder (if any) holds its own ref, so unref ours unconditionally.
            if (TempRef != LUA_NOREF)
            {
                lua_unref(MainState, TempRef);
            }

            return Status;
        }
    };
}
