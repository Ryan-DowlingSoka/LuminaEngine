#pragma once

#include "Core/UpdateContext.h"
#include "Core/Delegates/Delegate.h"
#include "Memory/SmartPtr.h"
#include "Scripting/Lua/Reference.h"


namespace Lumina
{
    namespace Lua
    {
        struct FScript;
    }

    class FRHIViewport;
    class FWorldManager;
    class FAssetRegistry;
    class FRenderManager;
    class IImGuiRenderer;
    class IDevelopmentToolUI;
    class FAssetManager;
    class FApplication;
    class FWindow;
    class CGameInstance;
}

namespace Lumina
{
    
    DECLARE_MULTICAST_DELEGATE(FProjectLoadedDelegate);

    
    class FEngine
    {
    public:
        
        FEngine() = default;
        virtual ~FEngine() = default;
        
        FEngine(const FEngine&) = delete;
        FEngine(FEngine&&) = delete;
        FEngine& operator = (const FEngine&) = delete;
        FEngine& operator = (FEngine&&) = delete;
        
        RUNTIME_API virtual bool Init();
        RUNTIME_API virtual bool Shutdown();
        RUNTIME_API bool Update(bool bApplicationWantsExit);
        RUNTIME_API virtual void OnUpdateStage(const FUpdateContext& Context);

        RUNTIME_API static FRHIViewport* GetEngineViewport();
        
        RUNTIME_API void SetEngineViewportSize(const glm::uvec2& InSize);
        
        /** Used to optionally load a project as a DLL from the command line */
        RUNTIME_API virtual void LoadProject(FStringView Path);

        /** Loads the project's script module */
        RUNTIME_API void LoadProjectScript(FStringView Path);

        /**
         * Cooked-runtime entry point. Mounts the .pak located alongside the
         * executable, loads project config from inside it, runs asset
         * discovery, loads the project's script module + DLL (if alongside),
         * spawns the game instance, and loads the configured startup map.
         * Returns false if no .pak could be found or it failed to mount.
         */
        RUNTIME_API bool LoadCookedRuntime();

        #if WITH_EDITOR
        RUNTIME_API virtual IDevelopmentToolUI* CreateDevelopmentTools() = 0;
        RUNTIME_API IDevelopmentToolUI* GetDevelopmentToolsUI() const { return DeveloperToolUI; }
        #endif

        // Cross-DLL meta plumbing has moved to Core/Engine/EngineMetaContext.h
        // (free functions Lumina::GetEngineMetaContext / GetEngineMetaService)
        // so Engine.h doesn't have to drag <entt/entt.hpp> into ~22 transitive
        // include sites that don't actually touch entt.

        RUNTIME_API void SetReadyToClose(bool bReadyToClose) { bEngineReadyToClose = bReadyToClose; }
        
        RUNTIME_API NODISCARD double GetDeltaTime() const { return UpdateContext.DeltaTime; }
        
        RUNTIME_API NODISCARD bool HasLoadedProject() const { return !ProjectName.empty(); }

        RUNTIME_API const FUpdateContext& GetUpdateContext() const { return UpdateContext; }

        RUNTIME_API void SetEngineReadyToClose(bool bReady) { bEngineReadyToClose = bReady; }
        RUNTIME_API NODISCARD bool IsCloseRequested() const { return bCloseRequested; }
        
        RUNTIME_API FProjectLoadedDelegate& GetProjectLoadedDelegate() { return OnProjectLoaded; }
        
        RUNTIME_API NODISCARD FStringView GetProjectName() const { return ProjectName; }
        RUNTIME_API NODISCARD FStringView GetProjectPath() const { return ProjectPath; }
        RUNTIME_API NODISCARD FFixedString GetProjectContentDirectory() const;

        RUNTIME_API CGameInstance* GetGameInstance() const { return GameInstance; }

        /**
         * Queues a world travel to the asset at WorldPath. The actual swap
         * runs at the start of the next frame so calls from gameplay code,
         * scripts, or UI never tear down a world mid-tick.
         *
         * Targets the running Game-type world, preferring PIE when one exists.
         * The editor proxy world is preserved so exiting PIE restores the
         * original map. In packaged builds with no Game world yet, Travel
         * creates one.
         */
        RUNTIME_API void Travel(FStringView WorldPath);

    protected:

        /** Constructs the CGameInstance subclass named by Project.GameInstanceClass (or the base if unset) and calls Init. */
        RUNTIME_API virtual void CreateGameInstance();

        /** Loads Project.GameStartupMap as a Game world context. Editor overrides to no-op. */
        RUNTIME_API virtual void LoadStartupMap();

        /** Destroys the GameInstance. Called during Shutdown. */
        RUNTIME_API virtual void DestroyGameInstance();

        /** Drains a queued Travel request. Called once per frame at FrameStart. */
        RUNTIME_API void ProcessPendingTravel();

    protected:

        FUpdateContext          UpdateContext;

        FString                 PendingTravelPath;
        bool                    bHasPendingTravel = false;

        #if WITH_EDITOR
        IDevelopmentToolUI*     DeveloperToolUI =       nullptr;
        #endif
        
        FString                     ProjectName;
        FFixedString                ProjectPath;
        TSharedPtr<Lua::FScript>    ProjectScript;
        Lua::FRef                   ModuleUpdateFunc;
        CGameInstance*              GameInstance = nullptr;
        

        FProjectLoadedDelegate  OnProjectLoaded;
        
        bool                    bCloseRequested = false;
        bool                    bEngineReadyToClose = false;
    };
    
    RUNTIME_API extern FEngine* GEngine;
}
