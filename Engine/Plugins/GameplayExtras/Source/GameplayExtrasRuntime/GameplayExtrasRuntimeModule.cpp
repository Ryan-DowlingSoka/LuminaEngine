#include "GameplayExtrasRuntimeModule.h"

#include "Core/Module/ModuleManager.h"
#include "Log/Log.h"

using namespace Lumina;

IMPLEMENT_MODULE(FGameplayExtrasRuntimeModule, "GameplayExtrasRuntime");

void FGameplayExtrasRuntimeModule::StartupModule()
{
    LOG_INFO("[GameplayExtras] Runtime module ready");
}

void FGameplayExtrasRuntimeModule::ShutdownModule()
{
    LOG_INFO("[GameplayExtras] Runtime module shutdown");
}
