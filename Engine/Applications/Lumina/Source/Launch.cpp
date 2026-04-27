#include "Core/Application/Application.h"
#include "Core/Application/ApplicationGlobalState.h"
#if WITH_EDITOR
#include "LuminaEditor.h"
#endif
#include <exception>
#include <print>
#include "Config/Config.h"
#include "Core/CommandLine/CommandLine.h"
#include "Core/Delegates/CoreDelegates.h"
#include "Core/Engine/Engine.h"

using namespace Lumina;


int LuminaMain(int ArgC, char** ArgV)  // NOLINT(misc-use-internal-linkage)
{
    int Result = 0;
    FApplicationGlobalState GlobalState;

    FCommandLine Parsed{ArgC, ArgV};
    GCommandLine = &Parsed;

    FApplication Application{};
    GApp = &Application;

    FConfig Config{};
    GConfig = &Config;

    #if WITH_EDITOR
    FEditorEngine EdEngine{};
    GEditorEngine = &EdEngine;
    GEngine = GEditorEngine;
    #else
    FEngine Engine{};
    GEngine = &Engine;

    // Cooked-runtime entry. The PAK + project DLL are expected next to the exe;
    // FEngine::LoadCookedRuntime mounts them and starts the configured map.
    // We hook this onto OnPostEngineInit so it runs after VFS / asset registry
    // are alive and before the first frame.
    FCoreDelegates::OnPostEngineInit.AddLambda([]
    {
        GEngine->LoadCookedRuntime();
    });
    #endif

    Result = Application.Run(ArgC, ArgV);

    #if WITH_EDITOR
    GEditorEngine   = nullptr;
    #endif
    GApp            = nullptr;
    GCommandLine    = nullptr;
    GConfig         = nullptr;

    return Result;
}
