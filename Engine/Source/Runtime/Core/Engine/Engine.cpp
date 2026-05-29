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
#include "Core/Plugin/PluginManager.h"
#include "Core/Object/Package/Package.h"
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
#include "Physics/PhysicsThread.h"
#include "Platform/Filesystem/FileHelper.h"
#include "Renderer/GPUProfiler/GPUProfiler.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderManager.h"
#include "Renderer/RenderResource.h"
#include "Renderer/RenderThread.h"
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
#include "Core/CommandLine/CommandLine.h"
#include "Core/Object/Cast.h"
#include "Core/Object/Class.h"
#include "Core/Object/ObjectCore.h"

#define SANDBOX_PROJECT_ID "C9396E54-2E00-4874-B051-FCD1792359AC"

namespace Lumina
{
    RUNTIME_API FEngine* GEngine;

    static FRHIViewportRef EngineViewport;

    static TConsoleVar CVarMaxFrameRate("Core.MaxFPS", 4000, "Changes the maximum frame-rate of your engine");

    // Lightweight pre-parse of just the "Plugins" array from a .lproject so
    // engine-plugin module loading at Earliest/Core/PreEngineInit phases
    // can already see project overrides. The full LoadProject() runs much
    // later (post Lua + post WorldManager); without this pass, disabling a
    // plugin in the .lproject would have no effect on its PreEngineInit
    // modules because the override would land after the DLL was loaded.
    static void PreloadProjectPluginOverrides(FStringView LprojPath)
    {
        if (LprojPath.empty()) return;
        std::error_code EC;
        if (!std::filesystem::exists(std::filesystem::path(LprojPath.data(), LprojPath.data() + LprojPath.size()), EC))
        {
            return;
        }

        FString Json;
        if (!FileHelper::LoadFileIntoString(Json, LprojPath))
        {
            return;
        }

        nlohmann::json Root;
        try
        {
            Root = nlohmann::json::parse(Json.c_str(), Json.c_str() + Json.size());
        }
        catch (const std::exception&)
        {
            return; // Malformed JSON; LoadProject will surface a real error later.
        }
        if (!Root.is_object()) return;

        auto It = Root.find("Plugins");
        if (It == Root.end() || !It->is_array() || It->empty()) return;

        TVector<FProjectPluginOverride> Overrides;
        Overrides.reserve(It->size());
        for (const auto& Entry : *It)
        {
            if (!Entry.is_object()) continue;
            FProjectPluginOverride Override;
            if (auto NameIt = Entry.find("Name"); NameIt != Entry.end() && NameIt->is_string())
            {
                const std::string& S = NameIt->get_ref<const std::string&>();
                Override.Name.assign(S.c_str(), S.size());
            }
            if (auto EnIt = Entry.find("Enabled"); EnIt != Entry.end() && EnIt->is_boolean())
            {
                Override.bEnabled = EnIt->get<bool>();
            }
            if (!Override.Name.empty())
            {
                Overrides.emplace_back(Move(Override));
            }
        }
        if (!Overrides.empty())
        {
            FPluginManager::Get().ApplyProjectOverrides(Overrides);
        }
    }
    
    bool FEngine::Init()
    {
        LUMINA_PROFILE_SCOPE();

        Platform::EnableHighResolutionTiming();

        // Plugin discovery has to happen before the renderer / Lua so
        // Earliest- and Core-phase plugins can wedge themselves in ahead.
        // Discovery is read-only (parses .lplugin files) and idempotent.
        FPluginManager::Get().DiscoverEnginePlugins();

        // Apply project overrides BEFORE any module loads. The resolved
        // .lproject path comes from --Project, falling back to the editor's
        // last-loaded stash (Editor.StartupProject) so a bare-launch reopen
        // still respects per-project plugin enable/disable. The full
        // LoadProject() runs later and re-applies these as a no-op, plus
        // picks up any newly-discovered project plugins.
        {
            FString PreloadLproj;
            if (TOptional<FFixedString> ProjectArg = GCommandLine->Get("Project"))
            {
                const FFixedString& V = ProjectArg.value();
                PreloadLproj.assign(V.c_str(), V.size());
            }
            else if (GConfig)
            {
                const std::string Stashed = GConfig->Get<std::string>("Editor.StartupProject");
                if (!Stashed.empty() && Stashed != "NULL")
                {
                    PreloadLproj.assign(Stashed.c_str(), Stashed.size());
                }
            }
            PreloadProjectPluginOverrides(PreloadLproj);
        }

        FPluginManager::Get().LoadModulesForPhase(EPluginLoadingPhase::Earliest);

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

        FPluginManager::Get().LoadModulesForPhase(EPluginLoadingPhase::Core);

        GPhysicsThread = Memory::New<FPhysicsThread>();
        GPhysicsThread->Start();
        
        GRenderManager = Memory::New<FRenderManager>();
        GRenderManager->Initialize();
        EngineViewport = GRenderContext->CreateViewport(Windowing::GetPrimaryWindowHandle()->GetExtent(), "Engine Viewport");
        
        Lua::Initialize();

        ProcessNewlyLoadedCObjects();

        // Engine-plugin runtime modules go here: post Lua + reflection, so
        // their IMPLEMENT_MODULE/ConstructCEnum static initializers don't
        // null-deref the VM; pre world manager, so anything they spawn at
        // WorldManager construction can see them.
        FPluginManager::Get().LoadModulesForPhase(EPluginLoadingPhase::PreEngineInit);
        ProcessNewlyLoadedCObjects();

        // Built-in primitive meshes must exist before any world deserializes.
        CPrimitiveManager::Get();

        GWorldManager = Memory::New<FWorldManager>();

        FPluginManager::Get().LoadModulesForPhase(EPluginLoadingPhase::EngineInit);
        ProcessNewlyLoadedCObjects();

        // --Project= load runs HERE (post-Lua, post-WorldManager, pre-EditorUI):
        //   - post Lua::Initialize, so reflection ConstructCEnum/Class/Struct
        //     that touch the VM during DLL load don't null-deref.
        //   - post WorldManager, so LoadStartupMap can spawn a world.
        //   - pre editor UI, so EditorUI::OnInitialize sees a non-empty
        //     ProjectName and skips the Open Project dialog.
        if (TOptional<FFixedString> ProjectArg = GCommandLine->Get("Project"))
        {
            LoadProject(ProjectArg.value());
        }

        #if USING(WITH_EDITOR)
        DeveloperToolUI = CreateDevelopmentTools();
        DeveloperToolUI->Initialize(UpdateContext);
        // Below the viewport router, so panel clicks reach the world first.
        GApp->GetEventProcessor().RegisterEventHandler(DeveloperToolUI, (int32)EInputLayer::EditorChrome);

        FPluginManager::Get().LoadModulesForPhase(EPluginLoadingPhase::EditorInit);
        ProcessNewlyLoadedCObjects();
        #endif
        
        RmlUi::Initialize();

        FPluginManager::Get().LoadModulesForPhase(EPluginLoadingPhase::PostEngineInit);
        ProcessNewlyLoadedCObjects();

        FCoreDelegates::OnPostEngineInit.BroadcastAndClear();

        return true;
    }

    bool FEngine::Shutdown()
    {
        LUMINA_PROFILE_SCOPE();

        FCoreDelegates::OnPreEngineShutdown.BroadcastAndClear();

        // Drain the render thread + GPU before tearing down UI: RmlUi::Shutdown frees widget
        // render targets, and an in-flight frame may still sample them (bindless) -- freeing
        // them under the GPU is a use-after-free / device loss.
        FlushRenderingCommands();
        GRenderContext->WaitIdle();

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

        // Stop the worker before Physics::Shutdown so no in-flight step touches Jolt globals being destroyed.
        if (GPhysicsThread)
        {
            GPhysicsThread->Stop();
            Memory::Delete(GPhysicsThread);
            GPhysicsThread = nullptr;
        }

        Physics::Shutdown();
        Task::Shutdown();
        Audio::Shutdown();

        // Drop our plugin records before the module manager rips DLLs out
        // from under us; FPluginManager just clears its bookkeeping, the
        // actual DLL teardown happens in UnloadAllModules.
        FPluginManager::Get().ShutdownAllPlugins();
        FModuleManager::Get().UnloadAllModules();

        Platform::DisableHighResolutionTiming();

        return false;
    }

    bool FEngine::Update(bool bApplicationWantsExit)
    {
        LUMINA_PROFILE_SCOPE();

        bEngineReadyToClose = true;
        bCloseRequested = bApplicationWantsExit;

        UpdateContext.MarkFrameStart(Platform::GetTime());

        FCPUProfiler::Get().BeginFrame();

        if (!Windowing::GetPrimaryWindowHandle()->IsWindowMinimized())
        {
            {
                LUMINA_PROFILE_SECTION_COLORED("FrameStart", tracy::Color::Red);
                UpdateContext.UpdateStage = EUpdateStage::FrameStart;

                // Join the previous frame's physics worker before any game-thread code touches the
                // ECS. ProcessQueue runs marshaled callbacks that may mutate it, so it follows the join.
                {
                    LUMINA_PROFILE_SECTION_COLORED("WaitForPhysics", tracy::Color::DarkOliveGreen);
                    GWorldManager->WaitForPhysics();
                    GWorldManager->DispatchPhysicsEvents();
                }

                MainThread::ProcessQueue();

                // Drain Travel before world ticks; tearing down a world from inside its own update is unsafe.
                ProcessPendingTravel();

                GRenderManager->FrameStart(UpdateContext);

                #if USING(WITH_EDITOR)
                DeveloperToolUI->StartFrame(UpdateContext);
                DeveloperToolUI->Update(UpdateContext);
                #endif

                // Reclaim hidden-world renderers after the grace window expires.
                GWorldManager->ReclaimIdleRenderers(UpdateContext.GetFrameStartTime());

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

                #if USING(WITH_EDITOR)
                DeveloperToolUI->Update(UpdateContext);
                #endif

                // Final world update stage runs on game thread.
                GWorldManager->UpdateWorlds(UpdateContext);

                #if USING(WITH_EDITOR)
                DeveloperToolUI->EndFrame(UpdateContext);
                #endif

                // Freed packages may own GPU resources captured by in-flight render lambdas;
                // gate the drain on the queue being empty. Skip the wait when nothing is pending.
                if (CPackage::HasPendingDestroys())
                {
                    LUMINA_PROFILE_SECTION_COLORED("WaitForRender (Package Teardown)", tracy::Color::Crimson);
                    FlushRenderingCommands();
                    CPackage::DrainPendingDestroys();
                }

                // Per-world UI ticks inside each world's Extract; only editor preview contexts remain global.
                RmlUi::TickEditorContexts();
                GWorldManager->ExtractWorlds();

                GRenderManager->FrameEnd();

                Lua::FScriptingContext::Get().ProcessDeferredActions();

                // Runs after ProcessDeferredActions so a hot-reloaded script gets fresh breakpoints before resume.
                Lua::FLuaDebugger::Get().Tick();

                OnUpdateStage(UpdateContext);

                // Kick physics after all game-thread ECS access; results land next frame.
                GWorldManager->KickPhysics();
            }
        }
        
        FCPUProfiler::Get().EndFrame();

        UpdateContext.MarkFrameEnd(Platform::GetTime());

        int32 MaxFrameRate = CVarMaxFrameRate.GetValue();
        if (MaxFrameRate > 0)
        {
            LUMINA_PROFILE_SECTION_COLORED("Frame-Rate-Limiter", tracy::Color::Gray);
            const double TargetFrameTime = 1.0 / static_cast<double>(MaxFrameRate);
            const double FrameStartTime  = UpdateContext.GetFrameStartTime();
            const double TargetEndTime   = FrameStartTime + TargetFrameTime;

            // Sleep the bulk, leaving margin for OS scheduler overshoot, then spin for precision.
            constexpr double SpinMargin = 0.001;
            double Remaining = TargetEndTime - Platform::GetTime();
            if (Remaining > SpinMargin)
            {
                std::this_thread::sleep_for(std::chrono::duration<double>(Remaining - SpinMargin));
            }

            while (Platform::GetTime() < TargetEndTime)
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

    void FEngine::SetEngineViewportSize(const FUIntVector2& InSize)
    {
        EngineViewport = GRenderContext->CreateViewport(InSize, "Engine Viewport");
    }

    TVector<FCookRoot> FEngine::GetCookRoots() const
    {
        TVector<FCookRoot> Result;

        // Project roots from the Project.CookRoots StringArray (edited via
        // the Project Settings UI; persisted in GameSettings.json). Each
        // entry is an asset path with implicit chunk "Main"; advanced
        // chunking is plugin-only.
        if (GConfig != nullptr)
        {
            const TVector<FString> Paths = GConfig->GetStringArray("Project.CookRoots");
            Result.reserve(Paths.size());
            for (const FString& P : Paths)
            {
                if (P.empty()) continue;
                FCookRoot Root;
                Root.Asset = P;
                Root.Chunk = FName("Main");
                Result.emplace_back(Move(Root));
            }
        }

        // Plugin-contributed roots (only from enabled plugins). Plugin
        // descriptors can specify per-root chunk hints.
        for (const FPlugin* Plugin : FPluginManager::Get().GetAllPlugins())
        {
            if (!Plugin->IsEnabled())                  continue;
            for (const FCookRoot& Root : Plugin->GetDescriptor().CookRoots)
            {
                Result.push_back(Root);
            }
        }

        return Result;
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
        
        Json Data;
        try
        {
            Data = Json::parse(JsonData.c_str(), JsonData.c_str() + JsonData.size());
        }
        catch (const std::exception& Ex)
        {
            LOG_ERROR("Failed to parse project file '{}': {}", Path, Ex.what());
            return;
        }

        if (!Data.is_object() || !Data.contains("ProjectID") || !Data.contains("Name"))
        {
            LOG_ERROR("Project file '{}' is missing required fields (ProjectID/Name).", Path);
            return;
        }

        FGuid ProjectID                 = FGuid::FromString(Data["ProjectID"].get<std::string>().c_str());
        ProjectPath                     .assign_convert(VFS::Parent(Paths::Normalize(Path)));
        ProjectName                     = Data["Name"].get<std::string>().c_str();

        FFixedString ConfigDir          = Paths::Combine(ProjectPath, "Config");
        FFixedString GameDir            = Paths::Combine(ProjectPath, "Game", "Content");
        FFixedString BinariesDirectory  = Paths::Combine(ProjectPath, "Binaries");

        VFS::Mount<VFS::FNativeFileSystem>("/Game", GameDir);
        VFS::Mount<VFS::FNativeFileSystem>("/Config", ConfigDir);

        GConfig->LoadPath("/Config");

        // Project plugins are discovered AFTER /Game mounts (so they can
        // refer to project paths if they want) but BEFORE the project DLL
        // loads, so any types they introduce are already in reflection
        // when the project DLL's modules construct.
        FPluginManager::Get().DiscoverProjectPlugins(ProjectPath);

        // Per-project plugin overrides from the .lproject "Plugins" array.
        // Each entry: { "Name": "...", "Enabled": true|false }. Missing
        // array means "no overrides"; descriptor defaults stand.
        if (auto It = Data.find("Plugins"); It != Data.end() && It->is_array())
        {
            TVector<FProjectPluginOverride> Overrides;
            Overrides.reserve(It->size());
            for (const auto& Entry : *It)
            {
                if (!Entry.is_object()) continue;
                FProjectPluginOverride Override;
                if (auto NameIt = Entry.find("Name"); NameIt != Entry.end() && NameIt->is_string())
                {
                    const std::string& S = NameIt->get_ref<const std::string&>();
                    Override.Name.assign(S.c_str(), S.size());
                }
                if (auto EnIt = Entry.find("Enabled"); EnIt != Entry.end() && EnIt->is_boolean())
                {
                    Override.bEnabled = EnIt->get<bool>();
                }
                if (!Override.Name.empty())
                {
                    Overrides.emplace_back(Move(Override));
                }
            }
            FPluginManager::Get().ApplyProjectOverrides(Overrides);
        }

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

        // Cook roots — the asset paths the cooker uses to seed the
        // dependency graph. Typically a list of map .lasset paths; everything
        // they transitively reference gets cooked into the shipped PAK.
        // Plugin-supplied roots come from .lplugin descriptors separately.
        // The .lasset FileFilter activates the per-row Browse picker in the
        // StringArray renderer (ProjectSettingsEditorTool).
        GConfig->RegisterSetting(FConfigSetting::Make("Project.CookRoots", EConfigValueType::StringArray)
            .WithCategory("Project/Maps")
            .WithDescription("Asset paths the cooker walks from to build the shipped PAK. Usually maps.")
            .WithFileFilter("Lumina Asset (*.lasset)\0*.lasset\0")
            .WithOwnerFile(ProjectFile));

        // One-shot migration: if an older project still has its CookRoots[]
        // in the .lproject JSON and Project.CookRoots in GameSettings.json
        // is empty, copy them over so the editor's Project Settings UI
        // picks them up. Chunk hints from the JSON form are dropped (the
        // settings UI only exposes asset paths — plugins still own chunked
        // routing via .lplugin).
        if (auto It = Data.find("CookRoots"); It != Data.end() && It->is_array() && GConfig != nullptr)
        {
            const TVector<FString> Existing = GConfig->GetStringArray("Project.CookRoots");
            if (Existing.empty() && !It->empty())
            {
                std::vector<std::string> Migrated;
                Migrated.reserve(It->size());
                for (const auto& R : *It)
                {
                    if (R.is_string())
                    {
                        Migrated.push_back(R.get<std::string>());
                    }
                    else if (R.is_object())
                    {
                        if (auto AIt = R.find("Asset"); AIt != R.end() && AIt->is_string())
                        {
                            Migrated.push_back(AIt->get<std::string>());
                        }
                    }
                }
                if (!Migrated.empty())
                {
                    LOG_INFO("LoadProject: migrating {} cook root(s) from .lproject to Project.CookRoots", Migrated.size());
                    GConfig->Set("Project.CookRoots", Migrated);
                }
            }
        }
        
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

        // Project DLL is now in; project-plugin modules that need to wire
        // up to project types (Lua bindings for project gameplay classes,
        // editor extensions tied to project assets, etc.) load here.
        FPluginManager::Get().LoadModulesForPhase(EPluginLoadingPhase::PostProjectLoad);
        ProcessNewlyLoadedCObjects();

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
        // Priority: explicit Project.GameStartupMap > first CookRoots entry.
        // The cook-roots fallback means a simple project that only declares
        // CookRoots[] (the now-preferred path) Just Works without also
        // setting the legacy single-map field.
        FString RawMapName = GConfig->Get<std::string>("Project.GameStartupMap").c_str();
        if (RawMapName.empty())
        {
            const TVector<FCookRoot> Roots = GetCookRoots();
            if (!Roots.empty())
            {
                RawMapName = Roots[0].Asset;
                // DISPLAY (not INFO) so a Shipping post-mortem can see this fallback fired.
                LOG_DISPLAY("No Project.GameStartupMap set; falling back to first cook root '{}'.", RawMapName.c_str());
            }
        }

        if (RawMapName.empty())
        {
            LOG_WARN("No startup map: set Project.GameStartupMap or add at least one Project.CookRoots entry. Runtime has no world to run.");
            return;
        }

        // Tolerate legacy absolute paths from before the path resolver.
        const FFixedString MapName = VFS::ResolveToVirtualPath(RawMapName);
        // DISPLAY — survives Shipping; first diagnostic to look at on a black screen.
        LOG_DISPLAY("LoadStartupMap: loading '{}' (resolved '{}').", RawMapName.c_str(), MapName.c_str());

        CWorld* SourceWorld = LoadObject<CWorld>(FStringView(MapName.c_str(), MapName.size()));
        if (SourceWorld == nullptr)
        {
            LOG_ERROR("Failed to load startup map '{}' (resolved to '{}').", RawMapName.c_str(), MapName.c_str());
            return;
        }

        // Duplicate so the cached asset isn't the live world; Travel would tear it down otherwise.
        CWorld* StartupWorld = CWorld::DuplicateWorld(SourceWorld);
        if (StartupWorld == nullptr)
        {
            LOG_ERROR("Failed to duplicate startup map '{}'.", MapName.c_str());
            return;
        }

        FWorldContext* Context = GWorldManager->CreateWorldContext(StartupWorld, EWorldType::Game, ENetMode::Standalone);
        if (Context == nullptr)
        {
            LOG_ERROR("LoadStartupMap: CreateWorldContext returned null; world won't tick.");
            return;
        }
        Context->SourceWorld  = SourceWorld;
        Context->GameInstance = GameInstance;

        if (FInputViewport* Primary = GApp ? GApp->GetPrimaryViewport() : nullptr)
        {
            Primary->SetWorld(StartupWorld);
        }
        else
        {
            LOG_WARN("LoadStartupMap: no primary viewport — world loaded but nothing will render.");
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

        // Collect every .pak next to the exe — chunked cooking produces
        // one file per chunk (Main + Primary + UI + Script + ...). Sorted
        // so mount order is deterministic across launches.
        TVector<FFixedString> PakPaths;
        for (const auto& Entry : std::filesystem::directory_iterator(ExeDir.c_str()))
        {
            if (!Entry.is_regular_file()) continue;
            if (Entry.path().extension() != ".pak") continue;
            FFixedString P;
            P.assign_convert(Entry.path().generic_string().c_str());
            PakPaths.push_back(Move(P));
        }
        eastl::sort(PakPaths.begin(), PakPaths.end());

        if (PakPaths.empty())
        {
            LOG_ERROR("FEngine::LoadCookedRuntime: no .pak file found next to '{}'.", ExeDir.c_str());
            return false;
        }

        // Keep one FPakArchive alive per file (shared via TSharedPtr across
        // every alias mount). The first archive that exposes /Config drives
        // the GameSettings probe below.
        TSharedPtr<FPakArchive> ConfigArchive;
        for (const FFixedString& PakPath : PakPaths)
        {
            TSharedPtr<FPakArchive> Archive = FPakArchive::Open(PakPath);
            if (!Archive)
            {
                LOG_ERROR("FEngine::LoadCookedRuntime: failed to open '{}'.", PakPath.c_str());
                return false;
            }

            const TVector<FString> Aliases = Archive->GetTopLevelAliases();
            for (const FString& Alias : Aliases)
            {
                FFixedString AliasFixed(Alias.c_str(), Alias.size());
                VFS::Mount<VFS::FPakFileSystem>(AliasFixed, Archive);
                LOG_DISPLAY("FEngine::LoadCookedRuntime: mounted PAK '{}' at '{}'", PakPath.c_str(), Alias.c_str());
            }

            if (!ConfigArchive && Archive->HasEntry("/Config/GameSettings.json"))
            {
                ConfigArchive = Archive;
            }
        }


        // Loose-files overlay; mounted after PAK so most-recently-mounted wins and users can tweak shipped scripts.
        const FString LooseGameDir = ExeDir + "/Game";
        if (std::filesystem::exists(LooseGameDir.c_str()))
        {
            VFS::Mount<VFS::FNativeFileSystem>("/Game", LooseGameDir);
            LOG_INFO("FEngine::LoadCookedRuntime: mounted loose overlay at '/Game' -> {}", LooseGameDir.c_str());
        }

        if (ConfigArchive)
        {
            GConfig->LoadPath("/Config");
        }
        else
        {
            LOG_WARN("FEngine::LoadCookedRuntime: no /Config/GameSettings.json in any PAK; using defaults.");
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

        // Prefer the pre-baked registry the cooker bundled at /Engine/AssetRegistry.bin
        // — avoids walking every .lasset on disk and re-extracting headers
        // at game startup. Fall back to the full discovery only if the
        // cooked blob is missing (dev iteration / corrupt PAK).
        bool bLoadedFromBlob = false;
        {
            TVector<uint8> Blob;
            if (VFS::ReadFile(Blob, "/Engine/AssetRegistry.bin"))
            {
                FMemoryReader Reader(Blob);
                if (FAssetRegistry::Get().LoadFromArchive(Reader))
                {
                    LOG_DISPLAY("FEngine::LoadCookedRuntime: loaded pre-baked registry ({} bytes).", Blob.size());
                    bLoadedFromBlob = true;
                }
                else
                {
                    LOG_WARN("FEngine::LoadCookedRuntime: /Engine/AssetRegistry.bin failed validation; falling back to discovery.");
                }
            }
        }

        if (!bLoadedFromBlob)
        {
            // Discovery is async; MUST wait before LoadStartupMap or GetAssetByPath silently fails on empty registry.
            FAssetRegistry::Get().RunInitialDiscovery();
            if (GTaskSystem != nullptr)
            {
                GTaskSystem->WaitForAll();
            }
            LOG_DISPLAY("FEngine::LoadCookedRuntime: asset discovery complete.");
        }

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
