#include "pch.h"
#include "Application.h"
#include "Assets/AssetManager/AssetManager.h"
#include "Core/CommandLine/CommandLine.h"
#include "Core/Module/ModuleManager.h"
#include "Core/Windows/Window.h"
#include "Core/Windows/WindowTypes.h"
#include "FileSystem/FileSystem.h"
#include "Input/InputProcessor.h"
#include "Paths/Paths.h"

namespace Lumina
{
    RUNTIME_API FApplication* GApp;

    int32 FApplication::Run(int argc, char** argv)
    {
        LUMINA_PROFILE_SCOPE();
        
        ASSERT(GEngine);
        
        LOG_TRACE("Initializing Lumina");
        
        //---------------------------------------------------------------
        // Application initialization.
        //--------------------------------------------------------------

        PreInitStartup();
        CreateApplicationWindow();
        GEngine->Init();
        
        EventProcessor.RegisterEventHandler(&FInputProcessor::Get());
        
        //---------------------------------------------------------------
        // Core application loop.
        //--------------------------------------------------------------

        bool bEngineWantsExit = false;
        while(!bEngineWantsExit)
        {
            LUMINA_PROFILE_FRAME();

            MainWindow->ProcessMessages();

            bool bApplicationWantsExit = ShouldExit();
            
            bEngineWantsExit = !GEngine->Update(bApplicationWantsExit);

            FInputProcessor::Get().EndFrame();
        }
        
        //---------------------------------------------------------------
        // Application shutdown.
        //--------------------------------------------------------------

        LOG_TRACE("Shutting down Lumina");
        
        GEngine->Shutdown();
        
        Shutdown();
        
        return 0;
    }

    void FApplication::Shutdown()
    {
        
    }
    
    void FApplication::WindowResized(FWindow* Window, const glm::uvec2& Extent)
    {
        if (!Window->IsWindowMinimized())
        {
            GEngine->SetEngineViewportSize(Extent);
        }
    }

    void FApplication::RequestExit()
    {
        GApp->bExitRequested = true;
    }
    
    void FApplication::PreInitStartup()
    {
        InitializeCObjectSystem();
        
        Paths::InitializePaths();
        
        if (TOptional<FFixedString> Project = GCommandLine->Get("Project"))
        {
            GEngine->LoadProject(Project.value());
        }
        
    }

    bool FApplication::CreateApplicationWindow()
    {
        (void)FWindow::OnWindowResized.AddMember(this, &FApplication::WindowResized);
        
        MainWindow = new FWindow(FWindowSpecs{});
        
        Windowing::SetPrimaryWindowHandle(MainWindow);
        
        return true;
    }

    bool FApplication::ShouldExit() const
    {
        return MainWindow->ShouldClose() || bExitRequested;
    }
}
