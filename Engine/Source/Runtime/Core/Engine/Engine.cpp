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
#include "Tools/PrimitiveManager/PrimitiveManager.h"
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

        Platform::EnableHighResolutionTiming();
        
        const FString& EngineDir = Paths::GetEngineDirectory();
        if (!EngineDir.empty() && std::filesystem::exists(EngineDir.c_str()))
        {
            VFS::Mount<VFS::FNativeFileSystem>("/Engine", EngineDir);
        }
        const FString& InstallDir = Paths::GetEngineInstallDirectory();
        if (!InstallDir.empty() && std::filesystem::exists(InstallDir.c_str()))
        {
            const FString IntermediatesDir = InstallDir + "/Intermediates";
            std::filesystem::create_directories(IntermediatesDir.c_str());
            VFS::Mount<VFS::FNativeFileSystem>("/Intermediates", IntermediatesDir);
        }
        
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

        // Built-in primitive meshes must exist before any world deserializes.
        CPrimitiveManager::Get();

        GWorldManager = Memory::New<FWorldManager>();

        #if USING(WITH_EDITOR)
        DeveloperToolUI = CreateDevelopmentTools();
        DeveloperToolUI->Initialize(UpdateContext);
        // Below the viewport router, so panel clicks reach the world first.
        GApp->GetEventProcessor().RegisterEventHandler(DeveloperToolUI, (int32)EInputLayer::EditorChrome);
        #endif
        
        RmlUi::Initialise();

        FCoreDelegates::OnPostEngineInit.BroadcastAndClear();

        return true;
    }

    bool FEngine::Shutdown()
    {
        LUMINA_PROFILE_SCOPE();

        FCoreDelegates::OnPreEngineShutdown.BroadcastAndClear();

        // UI before renderer: RmlUi's shutdown releases resources through our render interface.
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

        Platform::DisableHighResolutionTiming();

        return false;
    }

    bool FEngine::Update(bool bApplicationWantsExit)
    {
        LUMINA_PROFILE_SCOPE();

        bEngineReadyToClose = true;
        bCloseRequested = bApplicationWantsExit;
        
        UpdateContext.MarkFrameStart(glfwGetTime());

        FCPUProfiler::Get().BeginFrame();

        if (!Windowing::GetPrimaryWindowHandle()->IsWindowMinimized())
        {
            {
                LUMINA_PROFILE_SECTION_COLORED("FrameStart", tracy::Color::Red);
                UpdateContext.UpdateStage = EUpdateStage::FrameStart;

                MainThread::ProcessQueue();

                // Drain Travel requests before world ticks; tearing down a world from inside its own update is unsafe.
                ProcessPendingTravel();

                GRenderManager->FrameStart(UpdateContext);

                #if USING(WITH_EDITOR)
                DeveloperToolUI->StartFrame(UpdateContext);
                DeveloperToolUI->Update(UpdateContext);
                #endif

                GWorldManager->UpdateWorlds(UpdateContext);

                OnUpdateStage(UpdateContext);
            }

            {
                LUMINA_PROFILE_SECTION_COLORED("Paused", tracy::Color::Purple);
                UpdateContext.UpdateStage = EUpdateStage::Paused;

                #if USING(WITH_EDITOR)
                DeveloperToolUI->Update(UpdateContext);
                #endif

                GWorldManager->UpdateWorlds(UpdateContext);

                OnUpdateStage(UpdateContext);
            }

            {
                LUMINA_PROFILE_SECTION_COLORED("Pre-Physics", tracy::Color::Green);
                UpdateContext.UpdateStage = EUpdateStage::PrePhysics;

                #if USING(WITH_EDITOR)
                DeveloperToolUI->Update(UpdateContext);
                #endif

                GWorldManager->UpdateWorlds(UpdateContext);

                OnUpdateStage(UpdateContext);
            }

            {
                LUMINA_PROFILE_SECTION_COLORED("During-Physics", tracy::Color::Blue);
                UpdateContext.UpdateStage = EUpdateStage::DuringPhysics;

                #if USING(WITH_EDITOR)
                DeveloperToolUI->Update(UpdateContext);
                #endif

                GWorldManager->UpdateWorlds(UpdateContext);

                OnUpdateStage(UpdateContext);
            }

            {
                LUMINA_PROFILE_SECTION_COLORED("Post-Physics", tracy::Color::Yellow);
                UpdateContext.UpdateStage = EUpdateStage::PostPhysics;

                #if USING(WITH_EDITOR)
                DeveloperToolUI->Update(UpdateContext);
                #endif

                GWorldManager->UpdateWorlds(UpdateContext);

                OnUpdateStage(UpdateContext);
            }

            {
                LUMINA_PROFILE_SECTION_COLORED("Frame-End", tracy::Color::Coral);
                UpdateContext.UpdateStage = EUpdateStage::FrameEnd;

                FRHICommandListRef PrimaryCommandList = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
                PrimaryCommandList->Open();
                ICommandList& CmdList = *PrimaryCommandList;

                // GPU scopes must close before FrameEnd(); it submits the cmd list and advances the profiler ring.
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

                    // Tick after all world updates so script mutations (SetText, class toggles)
                    // are reconciled into layout before Render walks the tree.
                    RmlUi::TickAll();

                    // RmlUi composites between world render and editor ImGui so editor chrome stays on top.
                    RmlUi::RenderAll(CmdList);

                    #if USING(WITH_EDITOR)
                    DeveloperToolUI->EndFrame(UpdateContext);
                    #endif
                }

                GRenderManager->FrameEnd(UpdateContext, CmdList);

                Lua::FScriptingContext::Get().ProcessDeferredActions();

                // Runs after ProcessDeferredActions so a hot-reloaded script gets fresh breakpoints before resume.
                Lua::FLuaDebugger::Get().Tick();

                OnUpdateStage(UpdateContext);

            }
        }
        
        FCPUProfiler::Get().EndFrame();

        UpdateContext.MarkFrameEnd(glfwGetTime());

        int32 MaxFrameRate = CVarMaxFrameRate.GetValue();
        if (MaxFrameRate > 0)
        {
            LUMINA_PROFILE_SECTION_COLORED("Frame-Rate-Limiter", tracy::Color::Gray);
            const double TargetFrameTime = 1.0 / static_cast<double>(MaxFrameRate);
            const double FrameStartTime  = UpdateContext.GetFrameStartTime();
            const double TargetEndTime   = FrameStartTime + TargetFrameTime;

            // Sleep the bulk, leaving margin for OS scheduler overshoot, then spin for precision.
            constexpr double SpinMargin = 0.001;
            double Remaining = TargetEndTime - glfwGetTime();
            if (Remaining > SpinMargin)
            {
                std::this_thread::sleep_for(std::chrono::duration<double>(Remaining - SpinMargin));
            }

            while (glfwGetTime() < TargetEndTime)
            {
                std::this_thread::yield();
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

        // Engine-read project settings; games may register more from their module init.
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
        
        // Sandbox stores its binaries elsewhere; normal projects use the standard bin path.
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

        // Tolerate legacy absolute paths from before the path resolver.
        const FFixedString MapName = VFS::ResolveToVirtualPath(RawMapName);

        CWorld* SourceWorld = LoadObject<CWorld>(FStringView(MapName.c_str(), MapName.size()));
        if (SourceWorld == nullptr)
        {
            LOG_ERROR("Failed to load startup map '{}' (resolved to '{}').", RawMapName.c_str(), MapName.c_str());
            return;
        }

        // Run on a duplicate so the cached asset is never the live world. Without this, the first
        // Travel call tears down the cached asset (LoadObject returns it again on the next Travel,
        // now empty), and DuplicateWorld would serialize a live, already-initialized registry.
        CWorld* StartupWorld = CWorld::DuplicateWorld(SourceWorld);
        if (StartupWorld == nullptr)
        {
            LOG_ERROR("Failed to duplicate startup map '{}'.", MapName.c_str());
            return;
        }

        FWorldContext* Context = GWorldManager->CreateWorldContext(StartupWorld, EWorldType::Game, ENetMode::Standalone);
        if (Context != nullptr)
        {
            Context->SourceWorld  = SourceWorld;
            Context->GameInstance = GameInstance;
        }

        if (FInputViewport* Primary = GApp ? GApp->GetPrimaryViewport() : nullptr)
        {
            Primary->SetWorld(StartupWorld);
        }
    }

    void FEngine::Travel(FStringView WorldPath)
    {
        // Deferred: tearing down a world from inside its own tick is unsafe. Drained at next FrameStart.
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

        // Prefer a PIE Game context so Travel replaces the running world, not the editor proxy world.
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

        // No running game world yet (cold-boot): spawn a fresh game context.
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

        const EWorldType                Type           = OldContext->Type;
        const ENetMode                  NetMode        = OldContext->NetMode;
        const bool                      bPIE           = OldContext->bPIE;
        const TWeakObjectPtr<CWorld>    SourceWorldRef = OldContext->SourceWorld;
        CGameInstance* const            SavedInstance  = OldContext->GameInstance != nullptr
                                                            ? OldContext->GameInstance
                                                            : GameInstance;
        CWorld* const                   OldWorld       = OldContext->World.Get();

        // Always duplicate: PIE never runs on the cached asset, and Travel-to-same-map would self-destroy.
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

        // OldWorld memory still alive (only TeardownWorld has run); safe to compare identity, do not inspect state.
        FCoreDelegates::OnWorldTravelled.Broadcast(OldWorld, NewWorld);
    }

    bool FEngine::LoadCookedRuntime()
    {
        if (!MountCookedRuntime())
        {
            return false;
        }
        return StartCookedGame();
    }

    bool FEngine::MountCookedRuntime()
    {
        // Find the single .pak next to the exe. Platform::BaseDir returns wide on Windows; convert first.
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

        // Mount under each TOC top-level alias (/Engine, /Game, /Config), sharing one FPakArchive via TSharedPtr.
        TVector<FString> Aliases = Archive->GetTopLevelAliases();
        for (const FString& Alias : Aliases)
        {
            FFixedString AliasFixed(Alias.c_str(), Alias.size());
            VFS::Mount<VFS::FPakFileSystem>(AliasFixed, Archive);
            LOG_INFO("FEngine::LoadCookedRuntime: mounted PAK at '{}'", Alias.c_str());
        }

        // Loose-files overlay; mounted after PAK so most-recently-mounted wins and users can tweak shipped scripts.
        const FString LooseGameDir = ExeDir + "/Game";
        if (std::filesystem::exists(LooseGameDir.c_str()))
        {
            VFS::Mount<VFS::FNativeFileSystem>("/Game", LooseGameDir);
            LOG_INFO("FEngine::LoadCookedRuntime: mounted loose overlay at '/Game' -> {}", LooseGameDir.c_str());
        }

        if (Archive->HasEntry("/Config/GameSettings.json"))
        {
            GConfig->LoadPath("/Config");
        }
        else
        {
            LOG_WARN("FEngine::LoadCookedRuntime: no /Config/GameSettings.json in PAK; using defaults.");
        }

        return true;
    }

    bool FEngine::StartCookedGame()
    {
        // Resolve exe dir again — used for project DLL lookup.
        const FString ExeFullPath = StringUtils::FromWideString(Platform::BaseDir());
        const size_t LastSlash = ExeFullPath.find_last_of("/\\");
        const FString ExeDir = (LastSlash == FString::npos)
            ? ExeFullPath
            : ExeFullPath.substr(0, LastSlash);

        // Discovery is async; MUST wait before LoadStartupMap or GetAssetByPath silently fails on empty registry.
        FAssetRegistry::Get().RunInitialDiscovery();
        if (GTaskSystem != nullptr)
        {
            GTaskSystem->WaitForAll();
        }
        LOG_INFO("FEngine::LoadCookedRuntime: asset discovery complete.");

        const FString ScriptPath = GConfig->Get<std::string>("Project.LuaModuleFile").c_str();
        if (!ScriptPath.empty())
        {
            LoadProjectScript(ScriptPath);
        }

        FInputActionMap::Get().LoadFromConfig();

        // Project DLL: cooker stashes Project.Name in config to resolve "<Name>-<Config>.dll" next to the exe.
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
