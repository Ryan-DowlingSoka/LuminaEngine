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

        // Pinned pre-pcall so the debugger can walk its prototype tree for line breakpoints.
        FRef                            MainFunction;

        FScriptExportSchema             ExportsSchema;
        TVector<FScriptPropertyEntry>   ExportDefaults;
        FScriptThreadData               ThreadData;

        bool                            bDirty = false;
    };
}
