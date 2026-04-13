#pragma once

#include "Core/UpdateContext.h"
#include "Subsystems/Subsystem.h"
#include <entt/entt.hpp>
#include "Core/Delegates/Delegate.h"
#include "Memory/SmartPtr.h"


namespace Lumina
{
    class FRHIViewport;
    class FWorldManager;
    class FAssetRegistry;
    class FRenderManager;
    class IImGuiRenderer;
    class IDevelopmentToolUI;
    class FAssetManager;
    class FApplication;
    class FWindow;
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
        RUNTIME_API virtual void OnUpdateStage(const FUpdateContext& Context) { }

        RUNTIME_API static FRHIViewport* GetEngineViewport();
        
        RUNTIME_API void SetEngineViewportSize(const glm::uvec2& InSize);
        
        /** Used to optionally load a project as a DLL from the command line */
        RUNTIME_API virtual void LoadProject(FStringView Path);

        #if WITH_EDITOR
        RUNTIME_API virtual IDevelopmentToolUI* CreateDevelopmentTools() = 0;
        RUNTIME_API IDevelopmentToolUI* GetDevelopmentToolsUI() const { return DeveloperToolUI; }
        #endif

        RUNTIME_API entt::meta_ctx& GetEngineMetaContext() const;
        RUNTIME_API entt::locator<entt::meta_ctx>::node_type GetEngineMetaService() const;

        RUNTIME_API void SetReadyToClose(bool bReadyToClose) { bEngineReadyToClose = bReadyToClose; }
        
        RUNTIME_API NODISCARD double GetDeltaTime() const { return UpdateContext.DeltaTime; }
        
        RUNTIME_API NODISCARD bool HasLoadedProject() const { return !ProjectName.empty(); }

        RUNTIME_API const FUpdateContext& GetUpdateContext() const { return UpdateContext; }

        RUNTIME_API void SetEngineReadyToClose(bool bReady) { bEngineReadyToClose = bReady; }
        RUNTIME_API NODISCARD bool IsCloseRequested() const { return bCloseRequested; }
        
        RUNTIME_API FProjectLoadedDelegate& GetProjectLoadedDelegate() { return OnProjectLoaded; }
        
        RUNTIME_API NODISCARD FStringView GetProjectName() const { return ProjectName; }
        RUNTIME_API NODISCARD FStringView GetProjectPath() const { return ProjectPath; }
        RUNTIME_API NODISCARD FFixedString GetProjectScriptDirectory() const;
        RUNTIME_API NODISCARD FFixedString GetProjectGameDirectory() const;
        RUNTIME_API NODISCARD FFixedString GetProjectContentDirectory() const;
    
    protected:
        
        FUpdateContext          UpdateContext;

        #if WITH_EDITOR
        IDevelopmentToolUI*     DeveloperToolUI =       nullptr;
        #endif
        
        FString                 ProjectName;
        FFixedString            ProjectPath;
        

        FProjectLoadedDelegate  OnProjectLoaded;
        
        bool                    bCloseRequested = false;
        bool                    bEngineReadyToClose = false;
    };
    
    RUNTIME_API extern FEngine* GEngine;
}
