#include "pch.h"
#include "Engine.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Audio/AudioContext.h"
#include "Audio/AudioGlobals.h"
#include "Config/Config.h"
#include "Core/Application/Application.h"
#include "Core/Console/ConsoleVariable.h"
#include "Core/Delegates/CoreDelegates.h"
#include "Core/Module/ModuleManager.h"
#include "Core/Object/ObjectIterator.h"
#include "Core/Profiler/Profile.h"
#include "Core/Windows/Window.h"
#include "encoder/basisu_enc.h"
#include "FileSystem/FileSystem.h"
#include "Input/InputProcessor.h"
#include "nlohmann/json.hpp"
#include "Paths/Paths.h"
#include "Physics/Physics.h"
#include "Platform/Filesystem/FileHelper.h"
#include "Prism/WidgetDeclaration.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderManager.h"
#include "Renderer/RenderResource.h"
#include "Renderer/RHIGlobals.h"
#include "Scripting/Lua/Scripting.h"
#include "TaskSystem/TaskSystem.h"
#include "TaskSystem/ThreadedCallback.h"
#include "Tools/UI/DevelopmentToolUI.h"
#include "World/WorldManager.h"
#include "GameInstance.h"
#include "Core/Object/Cast.h"
#include "Core/Object/Class.h"
#include "Core/Object/ObjectCore.h"
#include "Prism/Prism.h"

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
        GApp->GetEventProcessor().RegisterEventHandler(DeveloperToolUI);
        #endif
        
        FCoreDelegates::OnPostEngineInit.BroadcastAndClear();
        
        GApp->GetPrismApp().SetWindowSize(GetEngineViewport()->GetSize());
        
        return true;
    }

    bool FEngine::Shutdown()
    {
        LUMINA_PROFILE_SCOPE();

        FCoreDelegates::OnPreEngineShutdown.BroadcastAndClear();

        //-------------------------------------------------------------------------
        // Shutdown core engine state.
        //-------------------------------------------------------------------------

        #if USING(WITH_EDITOR)
        DeveloperToolUI->Deinitialize(UpdateContext);
        delete DeveloperToolUI;
        #endif

        DestroyGameInstance();

        Memory::Delete(GWorldManager);
		GWorldManager = nullptr;

        ShutdownCObjectSystem();
        
        GApp->GetPrismApp().Shutdown();
        
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
        
        if (!Windowing::GetPrimaryWindowHandle()->IsWindowMinimized())
        {
            // Frame Start
            //-------------------------------------------------------------------
            {
                LUMINA_PROFILE_SECTION_COLORED("FrameStart", tracy::Color::Red);
                UpdateContext.UpdateStage = EUpdateStage::FrameStart;
                
                MainThread::ProcessQueue();
                
                GRenderManager->FrameStart(UpdateContext);

                #if USING(WITH_EDITOR)
                DeveloperToolUI->StartFrame(UpdateContext);
                DeveloperToolUI->Update(UpdateContext);
                #endif
                
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

                #if USING(WITH_EDITOR)
                DeveloperToolUI->Update(UpdateContext);
                #endif

                GWorldManager->UpdateWorlds(UpdateContext);
                GWorldManager->RenderWorlds(CmdList);

                #if USING(WITH_EDITOR)
                DeveloperToolUI->EndFrame(UpdateContext);
                #endif

                GApp->GetPrismApp().Tick((float)GEngine->GetDeltaTime());

                GRenderManager->FrameEnd(UpdateContext, CmdList);

                Lua::FScriptingContext::Get().ProcessDeferredActions();

                OnUpdateStage(UpdateContext);

            }
        }
        
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

    entt::meta_ctx& FEngine::GetEngineMetaContext() const
    {
        return entt::locator<entt::meta_ctx>::value_or();
    }

    entt::locator<entt::meta_ctx>::node_type FEngine::GetEngineMetaService() const
    {
        return entt::locator<entt::meta_ctx>::handle();
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
        FFixedString GameDir            = Paths::Combine(ProjectPath, "Game");
        FFixedString BinariesDirectory  = Paths::Combine(ProjectPath, "Binaries");
        FFixedString GameScriptsDir     = Paths::Combine(ProjectPath, "Game", "Scripts");
        
        VFS::Mount<VFS::FNativeFileSystem>("/Game", GameDir);
        VFS::Mount<VFS::FNativeFileSystem>("/Config", ConfigDir);

        GConfig->LoadPath("/Config");
        
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
        const FString MapName = GConfig->Get<std::string>("Project.GameStartupMap").c_str();
        if (MapName.empty())
        {
            LOG_WARN("No Project.GameStartupMap configured; runtime has no world to run.");
            return;
        }

        CWorld* StartupWorld = LoadObject<CWorld>(FStringView(MapName.c_str()));
        if (StartupWorld == nullptr)
        {
            LOG_ERROR("Failed to load startup map '{}'.", MapName.c_str());
            return;
        }

        GWorldManager->CreateWorldContext(StartupWorld, EWorldType::Game, ENetMode::Standalone);
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

    FFixedString FEngine::GetProjectScriptDirectory() const
    {
        if (!HasLoadedProject())
        {
            return {};
        }
        
        return Paths::Combine(ProjectPath, "Game", "Scripts");
    }

    FFixedString FEngine::GetProjectGameDirectory() const
    {
        if (!HasLoadedProject())
        {
            return {};
        }
        
        return Paths::Combine(ProjectPath, "Game");

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
