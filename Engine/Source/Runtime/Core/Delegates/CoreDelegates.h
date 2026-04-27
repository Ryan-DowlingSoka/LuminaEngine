#pragma once
#include "Delegate.h"


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
    };
}
