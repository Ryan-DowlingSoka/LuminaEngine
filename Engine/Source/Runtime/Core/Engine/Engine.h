#pragma once

#include "Core/UpdateContext.h"
#include "Core/Delegates/Delegate.h"
#include "Memory/SmartPtr.h"
#include "Scripting/Lua/Reference.h"
#include "Assets/AssetRegistry/CookRoot.h"


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
        
        RUNTIME_API void SetEngineViewportSize(const FUIntVector2& InSize);
        
        /** Used to optionally load a project as a DLL from the command line */
        RUNTIME_API virtual void LoadProject(FStringView Path);

        /** Loads the project's script module */
        RUNTIME_API void LoadProjectScript(FStringView Path);

        /** Cooked-runtime entry: mounts .pak next to exe, loads config/scripts/DLL, spawns game instance, loads startup map. */
        RUNTIME_API bool LoadCookedRuntime();

        /** Cooked-runtime: mount the .pak + loose overlay so VFS reads work during engine init. */
        RUNTIME_API bool MountCookedRuntime();

        /** Cooked-runtime: post-init half — asset discovery, project DLL, game instance, startup map. */
        RUNTIME_API bool StartCookedGame();

        #if WITH_EDITOR
        RUNTIME_API virtual IDevelopmentToolUI* CreateDevelopmentTools() = 0;
        RUNTIME_API IDevelopmentToolUI* GetDevelopmentToolsUI() const { return DeveloperToolUI; }
        #endif

        // Meta-context accessors live in EngineMetaContext.h to avoid pulling <entt/entt.hpp> into Engine.h.

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

        // Union of all cook roots in effect for the loaded project:
        // the .lproject's `CookRoots` array + every enabled plugin's
        // `CookRoots`. Legacy `Project.GameStartupMap` (if set and no
        // explicit roots exist) gets auto-converted to a single root
        // for backward compatibility. Cooker iterates this for BFS seeds.
        RUNTIME_API TVector<FCookRoot> GetCookRoots() const;

        RUNTIME_API CGameInstance* GetGameInstance() const { return GameInstance; }

        /** Queues world travel; swap runs at next FrameStart. Prefers PIE Game world; preserves editor proxy on PIE exit. */
        RUNTIME_API void Travel(FStringView WorldPath);

    protected:

        /** Constructs Project.GameInstanceClass (or base CGameInstance) and calls Init. */
        RUNTIME_API virtual void CreateGameInstance();

        /** Loads Project.GameStartupMap as a Game world. Editor overrides to no-op. */
        RUNTIME_API virtual void LoadStartupMap();

        RUNTIME_API virtual void DestroyGameInstance();

        /** Drains a queued Travel request; called at FrameStart. */
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
