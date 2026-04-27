#include "pch.h"
#include "CoreDelegates.h"

namespace Lumina
{
    TMulticastDelegate<void>                    FCoreDelegates::OnPreEngineInit;
    TMulticastDelegate<void>		            FCoreDelegates::OnPostEngineInit;
    TMulticastDelegate<void>                    FCoreDelegates::PostWorldUnload;
    TMulticastDelegate<void>		            FCoreDelegates::OnPreEngineShutdown;
    TMulticastDelegate<void, FModuleInfo*>      FCoreDelegates::OnModuleLoaded;
    TMulticastDelegate<void>                    FCoreDelegates::OnModuleUnloaded;
    TMulticastDelegate<void, CWorld*, CWorld*>  FCoreDelegates::OnWorldTravelled;
}