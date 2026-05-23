#pragma once
#include "Delegate.h"
#include "Containers/String.h"


namespace Lumina
{
    struct FModuleInfo;
    class CWorld;

    struct FCoreDelegates
    {
        // Exported so applications (e.g. the Lumina launcher) and game DLLs
        // can register handlers across DLL boundaries.
        RUNTIME_API static TMulticastDelegate<void>		            OnPreEngineInit;
        RUNTIME_API static TMulticastDelegate<void>		            OnPostEngineInit;
        RUNTIME_API static TMulticastDelegate<void>                 PostWorldUnload;
        RUNTIME_API static TMulticastDelegate<void>		            OnPreEngineShutdown;
        RUNTIME_API static TMulticastDelegate<void, FModuleInfo*>   OnModuleLoaded;
        RUNTIME_API static TMulticastDelegate<void>                 OnModuleUnloaded;

        // Fired after FEngine::Travel swaps a running game/PIE world. OldWorld
        // is already torn down (ptr is identity-compare only); NewWorld is live.
        // Subscribers must drop cached entity handles / property tables on OldWorld.
        RUNTIME_API static TMulticastDelegate<void, CWorld*, CWorld*> OnWorldTravelled;

        // Fired by editor file watchers when a content file changes on disk (arg = VFS path).
        // Subscribers (UI hot-reload, Lua, plugins) filter by extension and react, so no
        // subsystem hard-codes its own watcher or content paths. Editor-only in practice.
        RUNTIME_API static TMulticastDelegate<void, FStringView>     OnContentFileModified;
    };
}
