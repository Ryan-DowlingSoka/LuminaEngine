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
#include "Platform/CrashHandler.h"

using namespace Lumina;


int LuminaMain(int ArgC, char** ArgV)  // NOLINT(misc-use-internal-linkage)
{
    // Must come before anything else so an early-init fault still produces a
    // dump and a modal.
    CrashHandler::Install();

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
    
    FCoreDelegates::OnPreEngineInit.AddLambda([]
    {
        GEngine->MountCookedRuntime();
    });
    FCoreDelegates::OnPostEngineInit.AddLambda([]
    {
        GEngine->StartCookedGame();
    });
    #endif

    Result = Application.Run(ArgC, ArgV);

    #if WITH_EDITOR
    GEditorEngine   = nullptr;
    #endif
    GApp            = nullptr;
    GCommandLine    = nullptr;
    GConfig         = nullptr;

    CrashHandler::Shutdown();
    return Result;
}
