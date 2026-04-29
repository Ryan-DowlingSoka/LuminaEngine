#include "pch.h"
#include "Engine.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Audio/AudioContext.h"
#include "Config/Config.h"
#include "Core/Application/Application.h"
#include "Core/Console/ConsoleVariable.h"
#include "Core/Delegates/CoreDelegates.h"
#include "Core/Module/ModuleManager.h"
#include "Core/Object/ObjectIterator.h"
#include "Core/Profiler/CPUProfiler.h"
#include "Core/Profiler/Profile.h"
#include "Core/Windows/Window.h"
#include "encoder/basisu_enc.h"
#include "FileSystem/FileSystem.h"
#include "FileSystem/PakFileSystem.h"
#include "Input/InputActionMap.h"
#include "Input/InputViewport.h"
#include "Pak/PakArchive.h"
#include "Platform/Process/PlatformProcess.h"
#include "TaskSystem/TaskSystem.h"
#include "Input/InputProcessor.h"
#include "nlohmann/json.hpp"
#include "Paths/Paths.h"
#include "Physics/Physics.h"
#include "Platform/Filesystem/FileHelper.h"
#include "Renderer/GPUProfiler/GPUProfiler.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderManager.h"
#include "Renderer/RenderResource.h"
#include "Renderer/RHIGlobals.h"
#include "Scripting/Lua/Scripting.h"
#include "Scripting/Lua/Debugger/LuaDebugger.h"
#include "TaskSystem/ThreadedCallback.h"
#include "Tools/UI/DevelopmentToolUI.h"
#include "World/WorldManager.h"
#include "World/World.h"
#include "World/WorldContext.h"
#include "UI/RmlUiBridge.h"
#include "GameInstance.h"
#include "Core/Object/Cast.h"
#include "Core/Object/Class.h"
#include "Core/Object/ObjectCore.h"

#define SANDBOX_PROJECT_ID "C9396E54-2E00-4874-B051-FCD1792359AC"

namespace Lumina
{
    RUNTIME_API FEngine* GEngine;
    
    static FRHIViewportRef EngineViewport;
    
    static TConsoleVar CVarMaxFrameRate("Core.MaxFPS", 144, "Changes the maximum frame-rate of your engine");
    
    bool FEngine::Init()
    {
        LUMINA_PROFILE_SCOPE();
        
        //-------------------------------------------------------------------------
        // Initialize core engine state.
        //-------------------------------------------------------------------------
        
        VFS::Mount<VFS::FNativeFileSystem>("/Engine", Paths::GetEngineDirectory());
        
        FCoreDelegates::OnPreEngineInit.BroadcastAndClear();
        
        FConsoleRegistry::Get().LoadFromConfig();
        
        basisu::basisu_encoder_init();

        Audio::Initialize();
        Task::Initialize();
        Physics::Initialize();
        
        GRenderManager = Memory::New<FRenderManager>();
        GRenderManager->Initialize();
        EngineViewport = GRenderContext->CreateViewport(Windowing::GetPrimaryWindowHandle()->GetExtent(), "Engine Viewport");
        
        Lua::Initialize();

        ProcessNewlyLoadedCObjects();
        
        GWorldManager = Memory::New<FWorldManager>();

        #if USING(WITH_EDITOR)
        DeveloperToolUI = CreateDevelopmentTools();
        DeveloperToolUI->Initialize(UpdateContext);
        // Below the viewport router, so panel clicks reach the world first.
        GApp->GetEventProcessor().RegisterEventHandler(DeveloperToolUI, (int32)EInputLayer::EditorChrome);
        #endif
        
        // RmlUi needs CompileEngineShaders done and the engine viewport alive
        // (both true by this point), AND it must be ready before
        // OnPostEngineInit fires; in packaged builds that broadcast triggers
        // LoadCookedRuntime, which loads worlds and runs scripts that may
        // call UI.LoadDocument straight away.
        RmlUi::Initialise();

        FCoreDelegates::OnPostEngineInit.BroadcastAndClear();

        return true;
    }

    bool FEngine::Shutdown()
    {
        LUMINA_PROFILE_SCOPE();

        FCoreDelegates::OnPreEngineShutdown.BroadcastAndClear();

        //-------------------------------------------------------------------------
        // Shutdown core engine state.
        //-------------------------------------------------------------------------

        // UI before renderer: RmlUi's shutdown releases resources through
        // our render interface.
        RmlUi::Shutdown();

        #if USING(WITH_EDITOR)
        DeveloperToolUI->Deinitialize(UpdateContext);
        delete DeveloperToolUI;
        #endif

        DestroyGameInstance();

        Memory::Delete(GWorldManager);
		GWorldManager = nullptr;

        ShutdownCObjectSystem();

        EngineViewport.SafeRelease();
        
        Lua::Shutdown();
        
        Memory::Delete(GRenderManager);
        GRenderManager = nullptr;

        Physics::Shutdown();
        Task::Shutdown();
        Audio::Shutdown();
        
        FModuleManager::Get().UnloadAllModules();
        
        return false;
    }

    bool FEngine::Update(bool bApplicationWantsExit)
    {
        LUMINA_PROFILE_SCOPE();

        //-------------------------------------------------------------------------
        // Update core engine state.
        //-------------------------------------------------------------------------

        bEngineReadyToClose = true;
        bCloseRequested = bApplicationWantsExit;
        
        UpdateContext.MarkFrameStart(glfwGetTime());

        FCPUProfiler::Get().BeginFrame();

        if (!Windowing::GetPrimaryWindowHandle()->IsWindowMinimized())
        {
            // Frame Start
            //-------------------------------------------------------------------
            {
                LUMINA_PROFILE_SECTION_COLORED("FrameStart", tracy::Color::Red);
                UpdateContext.UpdateStage = EUpdateStage::FrameStart;

                MainThread::ProcessQueue();

                // Drain pending Travel requests before any world ticks; tearing
                // down a world from inside its own update is unsafe.
                ProcessPendingTravel();

                GRenderManager->FrameStart(UpdateContext);

                #if USING(WITH_EDITOR)
                DeveloperToolUI->StartFrame(UpdateContext);
                DeveloperToolUI->Update(UpdateContext);
                #endif

                // Tick UI before world updates so game-side queries see fresh
                // layout. TickAll handles per-context resize internally.
                RmlUi::TickAll();

                GWorldManager->UpdateWorlds(UpdateContext);

                OnUpdateStage(UpdateContext);
            }
            
            // Paused
            //-------------------------------------------------------------------
            {
                LUMINA_PROFILE_SECTION_COLORED("Paused", tracy::Color::Purple);
                UpdateContext.UpdateStage = EUpdateStage::Paused;

                #if USING(WITH_EDITOR)
                DeveloperToolUI->Update(UpdateContext);
                #endif

                GWorldManager->UpdateWorlds(UpdateContext);

                OnUpdateStage(UpdateContext);
            }

            // Pre Physics
            //-------------------------------------------------------------------
            {
                LUMINA_PROFILE_SECTION_COLORED("Pre-Physics", tracy::Color::Green);
                UpdateContext.UpdateStage = EUpdateStage::PrePhysics;

                #if USING(WITH_EDITOR)
                DeveloperToolUI->Update(UpdateContext);
                #endif
                
                GWorldManager->UpdateWorlds(UpdateContext);

                OnUpdateStage(UpdateContext);
            }

            // During Physics
            //-------------------------------------------------------------------
            {
                LUMINA_PROFILE_SECTION_COLORED("During-Physics", tracy::Color::Blue);
                UpdateContext.UpdateStage = EUpdateStage::DuringPhysics;

                #if USING(WITH_EDITOR)
                DeveloperToolUI->Update(UpdateContext);
                #endif
                
                GWorldManager->UpdateWorlds(UpdateContext);

                OnUpdateStage(UpdateContext);
            }

            // Post Physics
            //-------------------------------------------------------------------
            {
                LUMINA_PROFILE_SECTION_COLORED("Post-Physics", tracy::Color::Yellow);
                UpdateContext.UpdateStage = EUpdateStage::PostPhysics;

                #if USING(WITH_EDITOR)
                DeveloperToolUI->Update(UpdateContext);
                #endif

                GWorldManager->UpdateWorlds(UpdateContext);

                OnUpdateStage(UpdateContext);
            }

            // Frame End / Render
            //-------------------------------------------------------------------
            {
                LUMINA_PROFILE_SECTION_COLORED("Frame-End", tracy::Color::Coral);
                UpdateContext.UpdateStage = EUpdateStage::FrameEnd;

                FRHICommandListRef PrimaryCommandList = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
                PrimaryCommandList->Open();
                ICommandList& CmdList = *PrimaryCommandList;

                // GPU scopes must close before GRenderManager->FrameEnd(); it submits
                // the command list and advances the profiler ring, after which EndScope is a no-op.
                {
                    GPU_PROFILE_SCOPE(&CmdList, "Frame");

                    #if USING(WITH_EDITOR)
                    DeveloperToolUI->Update(UpdateContext);
                    #endif

                    {
                        GPU_PROFILE_SCOPE_COLOR(&CmdList, "World Render", FColor(0.20f, 0.55f, 0.90f));
                        GWorldManager->UpdateWorlds(UpdateContext);
                        GWorldManager->RenderWorlds(CmdList);
                    }

                    // RmlUi composites between world render and editor ImGui:
                    // game UI overlays the world, editor chrome stays on top.
                    RmlUi::RenderAll(CmdList);

                    #if USING(WITH_EDITOR)
                    DeveloperToolUI->EndFrame(UpdateContext);
                    #endif
                }

                GRenderManager->FrameEnd(UpdateContext, CmdList);

                Lua::FScriptingContext::Get().ProcessDeferredActions();

                // Resume paused script threads when the user has clicked
                // Continue / Step in the editor debugger panel. Runs after
                // ProcessDeferredActions so a hot-reloaded script gets fresh
                // breakpoints applied before any potential resume.
                Lua::FLuaDebugger::Get().Tick();

                OnUpdateStage(UpdateContext);

            }
        }
        
        FCPUProfiler::Get().EndFrame();

        UpdateContext.MarkFrameEnd(glfwGetTime());

        
        // Frame-Rate Limiting
        //-------------------------------------------------------------------
        int32 MaxFrameRate = CVarMaxFrameRate.GetValue();
        if (MaxFrameRate > 0)
        {
            LUMINA_PROFILE_SECTION_COLORED("Frame-Rate-Limiter", tracy::Color::Gray);
            const double TargetFrameTime    = 1.0 / static_cast<double>(MaxFrameRate);
            const double CurrentTime        = UpdateContext.GetTime();
            const double FrameTime          = CurrentTime - UpdateContext.GetFrameStartTime();
            const double TimeToWait         = TargetFrameTime - FrameTime;
        
            if (TimeToWait > 0.0)
            {
                constexpr double SleepThreshold = 0.001;
                if (TimeToWait > SleepThreshold)
                {
                    std::this_thread::sleep_for(std::chrono::duration<double>(TimeToWait - SleepThreshold));
                }
            }
        }
        
        if (bApplicationWantsExit)
        {
            return !bEngineReadyToClose;
        }
        
        return true;
    }

    void FEngine::OnUpdateStage(const FUpdateContext& Context)
    {
        if (ModuleUpdateFunc.IsValid())
        {
            ModuleUpdateFunc(Context.GetDeltaTime());
        }
    }

    // Meta-context accessors moved to EngineMetaContext.cpp.

    FRHIViewport* FEngine::GetEngineViewport()
    {
        return EngineViewport;
    }

    void FEngine::SetEngineViewportSize(const glm::uvec2& InSize)
    {
        EngineViewport = GRenderContext->CreateViewport(InSize, "Engine Viewport");
    }

    void FEngine::LoadProject(FStringView Path)
    {
        using Json = nlohmann::json;
        
        FString JsonData;
        if (!FileHelper::LoadFileIntoString(JsonData, Path))
        {
            LOG_ERROR("Invalid project path");
            return;
        }
        
        Json Data = Json::parse(JsonData.c_str());
        DEBUG_ASSERT(!Data.empty());
        
        FGuid ProjectID                 = FGuid::FromString(Data["ProjectID"].get<std::string>().c_str());
        ProjectPath                     .assign_convert(VFS::Parent(Paths::Normalize(Path)));
        ProjectName                     = Data["Name"].get<std::string>().c_str();
        
        FFixedString ConfigDir          = Paths::Combine(ProjectPath, "Config");
        FFixedString GameDir            = Paths::Combine(ProjectPath, "Game", "Content");
        FFixedString BinariesDirectory  = Paths::Combine(ProjectPath, "Binaries");
        
        VFS::Mount<VFS::FNativeFileSystem>("/Game", GameDir);
        VFS::Mount<VFS::FNativeFileSystem>("/Config", ConfigDir);

        GConfig->LoadPath("/Config");

        // Project-side settings. Game projects can register additional ones from
        // their module init; these three are read by the engine itself.
        const FStringView ProjectFile = "/Config/GameSettings.json";
        GConfig->RegisterSetting(FConfigSetting::Make("Project.LuaModuleFile", EConfigValueType::Path)
            .WithCategory("Project/Scripting")
            .WithDescription("Lua module loaded after the project DLL is loaded.")
            .WithFileFilter("Luau Script (*.luau)\0*.luau\0All Files (*.*)\0*.*\0")
            .WithOwnerFile(ProjectFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Project.GameInstanceClass", EConfigValueType::String)
            .WithCategory("Project/Scripting")
            .WithDescription("Reflected CGameInstance subclass to instantiate at runtime. Empty = base CGameInstance.")
            .WithOwnerFile(ProjectFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Project.GameStartupMap", EConfigValueType::Path)
            .WithCategory("Project/Maps")
            .WithDescription("World loaded when the standalone game starts.")
            .WithFileFilter("Lumina Asset (*.lasset)\0*.lasset\0")
            .WithOwnerFile(ProjectFile));

        GConfig->RegisterSetting(FConfigSetting::Make("Project.EditorStartupMap", EConfigValueType::Path)
            .WithCategory("Project/Maps")
            .WithDescription("World opened automatically when the editor finishes loading the project.")
            .WithFileFilter("Lumina Asset (*.lasset)\0*.lasset\0")
            .WithOwnerFile(ProjectFile));
        
        FFixedString DLLPath;
        
        // Sandbox is a special project that has binaries hidden away elsewhere. A normal project will be at the normal bin path.
        if (ProjectID == FGuid::FromString(SANDBOX_PROJECT_ID))
        {
            DLLPath = Paths::Combine(Paths::GetEngineInstallDirectory(), "Binaries", LUMINA_PLATFORM_NAME, ProjectName);
        }
        else
        {
            DLLPath = Paths::Combine(ProjectPath, "Binaries", LUMINA_PLATFORM_NAME, ProjectName);
        }
        
        DLLPath.append("-").append(LUMINA_CONFIGURATION_NAME).append(LUMINA_SHAREDLIB_EXT_NAME);
        
        if (Paths::Exists(DLLPath))
        {
            if (IModuleInterface* Module = FModuleManager::Get().LoadModule(DLLPath))
            {
                ProcessNewlyLoadedCObjects();
            }
            else
            {
                LOG_INFO("No project module found");
            }
        }
        
        FAssetRegistry::Get().RunInitialDiscovery();

        FString ModuleFile = GConfig->Get<std::string>("Project.LuaModuleFile").c_str();
        LoadProjectScript(ModuleFile);

        // Must run after GConfig->LoadPath but before any OnReady script body.
        FInputActionMap::Get().LoadFromConfig();

        CreateGameInstance();
        LoadStartupMap();

        OnProjectLoaded.Broadcast();
    }

    void FEngine::CreateGameInstance()
    {
        const FString ClassName = GConfig->Get<std::string>("Project.GameInstanceClass").c_str();

        CClass* InstanceClass = nullptr;
        if (!ClassName.empty())
        {
            InstanceClass = FindObject<CClass>(FName(ClassName.c_str()));
            if (InstanceClass == nullptr)
            {
                LOG_WARN("Project.GameInstanceClass '{}' not found; falling back to CGameInstance.", ClassName.c_str());
            }
        }

        if (InstanceClass == nullptr)
        {
            InstanceClass = CGameInstance::StaticClass();
        }

        GameInstance = Cast<CGameInstance>(NewObject(InstanceClass, nullptr, NAME_None, FGuid::New(), OF_Transient));
        GameInstance->Init();
    }

    void FEngine::LoadStartupMap()
    {
        const FString RawMapName = GConfig->Get<std::string>("Project.GameStartupMap").c_str();
        if (RawMapName.empty())
        {
            LOG_WARN("No Project.GameStartupMap configured; runtime has no world to run.");
            return;
        }

        // Tolerate legacy absolute paths saved before the path resolver landed.
        const FFixedString MapName = VFS::ResolveToVirtualPath(RawMapName);

        CWorld* StartupWorld = LoadObject<CWorld>(FStringView(MapName.c_str(), MapName.size()));
        if (StartupWorld == nullptr)
        {
            LOG_ERROR("Failed to load startup map '{}' (resolved to '{}').", RawMapName.c_str(), MapName.c_str());
            return;
        }

        GWorldManager->CreateWorldContext(StartupWorld, EWorldType::Game, ENetMode::Standalone);

        if (FInputViewport* Primary = GApp ? GApp->GetPrimaryViewport() : nullptr)
        {
            Primary->SetWorld(StartupWorld);
        }
    }

    void FEngine::Travel(FStringView WorldPath)
    {
        // Defer the swap; it's unsafe to tear down a world from inside its own tick,
        // and Travel is typically called from gameplay/script code that runs during
        // world updates. ProcessPendingTravel drains this at the next FrameStart.
        PendingTravelPath.assign(WorldPath.data(), WorldPath.size());
        bHasPendingTravel = true;
    }

    void FEngine::ProcessPendingTravel()
    {
        if (!bHasPendingTravel)
        {
            return;
        }
        bHasPendingTravel = false;

        const FString RawPath = Move(PendingTravelPath);
        PendingTravelPath.clear();

        if (RawPath.empty())
        {
            LOG_ERROR("FEngine::Travel: empty world path.");
            return;
        }

        if (GWorldManager == nullptr)
        {
            LOG_ERROR("FEngine::Travel: WorldManager not initialized.");
            return;
        }

        const FFixedString MapName = VFS::ResolveToVirtualPath(RawPath);

        CWorld* WorldAsset = LoadObject<CWorld>(FStringView(MapName.c_str(), MapName.size()));
        if (WorldAsset == nullptr)
        {
            LOG_ERROR("FEngine::Travel: failed to load world '{}' (resolved to '{}').", RawPath.c_str(), MapName.c_str());
            return;
        }

        // Find the running game context. Prefer a PIE Game context (editor case)
        // so that Travel always replaces the *running* world, never the editor
        // proxy world we want to restore on PIE exit. Falls back to any Game
        // context (packaged build), and finally any Simulation if no Game exists.
        FWorldContext* OldContext = nullptr;
        for (const TUniquePtr<FWorldContext>& Ctx : GWorldManager->GetContexts())
        {
            if (Ctx->Type == EWorldType::Game)
            {
                OldContext = Ctx.get();
                if (Ctx->bPIE)
                {
                    break;
                }
            }
        }

        // No running game world yet (cold-boot or pre-startup-map call):
        // spawn a fresh game context. No source to duplicate from here.
        if (OldContext == nullptr)
        {
            FWorldContext* NewContext = GWorldManager->CreateWorldContext(WorldAsset, EWorldType::Game, ENetMode::Standalone);
            if (NewContext != nullptr)
            {
                NewContext->GameInstance = GameInstance;
            }
            FCoreDelegates::OnWorldTravelled.Broadcast(nullptr, WorldAsset);
            return;
        }

        // Snapshot role-state we need to reapply to the replacement context.
        const EWorldType                Type           = OldContext->Type;
        const ENetMode                  NetMode        = OldContext->NetMode;
        const bool                      bPIE           = OldContext->bPIE;
        const TWeakObjectPtr<CWorld>    SourceWorldRef = OldContext->SourceWorld;
        CGameInstance* const            SavedInstance  = OldContext->GameInstance != nullptr
                                                            ? OldContext->GameInstance
                                                            : GameInstance;
        CWorld* const                   OldWorld       = OldContext->World.Get();

        // Always duplicate the loaded asset. Two reasons:
        //   1) PIE semantics: never run on the cached source asset.
        //   2) Travel-to-same-map: LoadObject returns the cached CWorld; if
        //      we then DestroyWorldContext on it we'd be tearing down the very
        //      world we just intended to spin up.
        CWorld* NewWorld = CWorld::DuplicateWorld(WorldAsset);
        if (NewWorld == nullptr)
        {
            LOG_ERROR("FEngine::Travel: DuplicateWorld failed for '{}'.", MapName.c_str());
            return;
        }

        GWorldManager->DestroyWorldContext(OldWorld);

        FWorldContext* NewContext = GWorldManager->CreateWorldContext(NewWorld, Type, NetMode);
        if (NewContext != nullptr)
        {
            NewContext->bPIE         = bPIE;
            NewContext->SourceWorld  = SourceWorldRef;
            NewContext->GameInstance = SavedInstance;
        }

        if (FInputViewport* Primary = GApp ? GApp->GetPrimaryViewport() : nullptr)
        {
            Primary->SetWorld(NewWorld);
        }

        // Subscribers compare against OldWorld for identity. CObject memory
        // is still alive (only TeardownWorld has run); safe to compare but
        // state must not be inspected.
        FCoreDelegates::OnWorldTravelled.Broadcast(OldWorld, NewWorld);
    }

    bool FEngine::LoadCookedRuntime()
    {
        // Looks for a single .pak alongside the executable. We don't try to
        // pick "the right one" by name — there should only be one in a
        // shipped game folder. Platform::BaseDir returns wide on Windows;
        // convert before slicing.
        const FString ExeFullPath = StringUtils::FromWideString(Platform::BaseDir());
        const size_t LastSlash = ExeFullPath.find_last_of("/\\");
        const FString ExeDir = (LastSlash == FString::npos)
            ? ExeFullPath
            : ExeFullPath.substr(0, LastSlash);

        FFixedString PakPath;
        for (const auto& Entry : std::filesystem::directory_iterator(ExeDir.c_str()))
        {
            if (!Entry.is_regular_file())
            {
                continue;
            }
            if (Entry.path().extension() == ".pak")
            {
                PakPath.assign_convert(Entry.path().generic_string().c_str());
                break;
            }
        }

        if (PakPath.empty())
        {
            LOG_ERROR("FEngine::LoadCookedRuntime: no .pak file found next to '{}'.", ExeDir.c_str());
            return false;
        }

        TSharedPtr<FPakArchive> Archive = FPakArchive::Open(PakPath);
        if (!Archive)
        {
            LOG_ERROR("FEngine::LoadCookedRuntime: failed to open '{}'.", PakPath.c_str());
            return false;
        }

        // Mount the archive under each top-level alias the TOC contains. The
        // PAK shipped by the editor cooker carries entries like "/Engine/...",
        // "/Game/...", and "/Config/..." — each becomes a VFS mount sharing
        // the same FPakArchive instance via TSharedPtr.
        TVector<FString> Aliases = Archive->GetTopLevelAliases();
        for (const FString& Alias : Aliases)
        {
            FFixedString AliasFixed(Alias.c_str(), Alias.size());
            VFS::Mount<VFS::FPakFileSystem>(AliasFixed, Archive);
            LOG_INFO("FEngine::LoadCookedRuntime: mounted PAK at '{}'", Alias.c_str());
        }

        // Loose-files overlay. The packager can be configured to extract
        // /Game/Scripts/ (or other content) as editable files next to the
        // exe instead of bundling them in the PAK. Mounting this AFTER the
        // PAK means lookups walk the loose folder first (most-recently-mounted
        // wins per VFS dispatch order) — users can tweak scripts in the
        // shipped game without re-cooking.
        const FString LooseGameDir = ExeDir + "/Game";
        if (std::filesystem::exists(LooseGameDir.c_str()))
        {
            VFS::Mount<VFS::FNativeFileSystem>("/Game", LooseGameDir);
            LOG_INFO("FEngine::LoadCookedRuntime: mounted loose overlay at '/Game' -> {}", LooseGameDir.c_str());
        }

        // Project config is *inside* the PAK, written there by the cooker.
        if (Archive->HasEntry("/Config/GameSettings.json"))
        {
            GConfig->LoadPath("/Config");
        }
        else
        {
            LOG_WARN("FEngine::LoadCookedRuntime: no /Config/GameSettings.json in PAK; using defaults.");
        }

        // The asset registry already iterates /Engine/Resources/Content and
        // /Game/Content via VFS — those calls now hit the PAK transparently.
        // Discovery is async; we MUST wait before LoadStartupMap or the
        // GetAssetByPath lookup hits an empty registry and silently fails.
        FAssetRegistry::Get().RunInitialDiscovery();
        if (GTaskSystem != nullptr)
        {
            GTaskSystem->WaitForAll();
        }
        LOG_INFO("FEngine::LoadCookedRuntime: asset discovery complete.");

        // Project script (Lua) — load from PAK if the path is set.
        const FString ScriptPath = GConfig->Get<std::string>("Project.LuaModuleFile").c_str();
        if (!ScriptPath.empty())
        {
            LoadProjectScript(ScriptPath);
        }

        FInputActionMap::Get().LoadFromConfig();

        // Project DLL — the cooker stashes "Project.Name" in config so we can
        // resolve "<ProjectName>-<Config>.dll" sitting next to the exe. We
        // skip silently if it's missing; bare projects without a C++ module
        // can still boot.
        ProjectName = GConfig->Get<std::string>("Project.Name").c_str();
        if (!ProjectName.empty())
        {
            const FFixedString DLLName = FFixedString(FFixedString::CtorSprintf(),
                "%s-%s%s",
                ProjectName.c_str(),
                LUMINA_CONFIGURATION_NAME,
                LUMINA_SHAREDLIB_EXT_NAME);
            FFixedString DLLPath = Paths::Combine(ExeDir, DLLName);
            if (Paths::Exists(DLLPath))
            {
                if (FModuleManager::Get().LoadModule(DLLPath))
                {
                    ProcessNewlyLoadedCObjects();
                }
            }
            else
            {
                LOG_INFO("FEngine::LoadCookedRuntime: no project DLL at '{}' (ok if project has no C++ module)", DLLPath.c_str());
            }
        }

        CreateGameInstance();
        LoadStartupMap();
        return true;
    }

    void FEngine::DestroyGameInstance()
    {
        if (GameInstance == nullptr)
        {
            return;
        }

        GameInstance->Shutdown();
        GameInstance = nullptr;
    }

    void FEngine::LoadProjectScript(FStringView Path)
    {
        if (ProjectScript)
        {
            if (auto Ref = ProjectScript->Reference["OnUnload"])
            {
                Ref(Ref);
            }
            
            ModuleUpdateFunc = {};
        }
        
        ProjectScript = Lua::FScriptingContext::Get().LoadUniqueScriptPath(Path);
        
        if (ProjectScript)
        {
            if (auto Ref = ProjectScript->Reference["OnLoad"])
            {
                Ref(Ref);
            }
            
            ModuleUpdateFunc = ProjectScript->Reference["OnUpdate"];
        }
    }

    FFixedString FEngine::GetProjectContentDirectory() const
    {
        if (!HasLoadedProject())
        {
            return {};
        }
        
        return Paths::Combine(ProjectPath, "Game", "Content");

    }
}
