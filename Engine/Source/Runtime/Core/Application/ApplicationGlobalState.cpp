#include "pch.h"
#include "ApplicationGlobalState.h"

#include "Containers/Name.h"
#include "Core/Assertions/Assert.h"
#include "Core/Threading/Thread.h"
#include "Log/Log.h"
#include "Memory/Memory.h"

namespace Lumina
{
    namespace
    {
        bool bGlobalStateInitialize = false;
    }
    
    FApplicationGlobalState::FApplicationGlobalState(char const* MainThreadName)
    {
        ASSERT(!bGlobalStateInitialize);
        
        Threading::Initialize(MainThreadName == nullptr ? "Main Thread" : MainThreadName);
        FName::Initialize();
        Logging::Init();

        bGlobalStateInitialize = true;
    }

    FApplicationGlobalState::~FApplicationGlobalState()
    {
        ASSERT(bGlobalStateInitialize);
        bGlobalStateInitialize = false;

        Logging::Shutdown();
        FName::Shutdown();
        Threading::Shutdown();
    }
}
