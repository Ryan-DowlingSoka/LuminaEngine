#pragma once

#include "Reference.h"
#include "ScriptExports.h"
#include "Containers/Name.h"
#include "Containers/String.h"
#include "entt/entt.hpp"

namespace Lumina
{
    class CWorld;
}

namespace Lumina::Lua
{
    /**
     * Per-thread context attached to a script's lua_State via lua_setthreaddata.
     * Yield-aware C APIs read this to resolve which entity / world owns the calling
     * coroutine — for example so FTimerManager:Wait can scope its timer to an entity
     * and have it auto-cleared if the entity is destroyed mid-wait.
     *
     * The struct is owned by FScript so its address is stable for the script's lifetime.
     */
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

        /** Schema parsed from the `type Exports = {...}` alias; empty if none/unrecognised. */
        FScriptExportSchema             ExportsSchema;

        /** Default values read from the runtime `Exports` table right after script execution. */
        TVector<FScriptPropertyEntry>   ExportDefaults;

        /** Stable-address per-thread context published via lua_setthreaddata. */
        FScriptThreadData               ThreadData;

        bool                            bDirty = false;
    };
}
