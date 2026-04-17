#pragma once

#include "Core/Application/Application.h"

namespace Lumina
{
    class CWorld;
    class FCamera;
    class FEditorSettings;
    class FEditorLayer;
    class FEditorPanel;

    EDITOR_API extern class FEditorEngine* GEditorEngine;
    
    class EDITOR_API FEditorEngine : public FEngine
    {
    public:
        bool Init() override;
        bool Shutdown() override;

        CWorld* GetCurrentEditorWorld() const;

        #if WITH_EDITOR
        IDevelopmentToolUI* CreateDevelopmentTools() override;
        #endif

        void CreateProject(FStringView NewProjectName, FStringView NewProjectPath);

    protected:

        /** Editor doesn't auto-load a runtime world; the user picks one via the editor UI. */
        void LoadStartupMap() override {}
    };
    
    
    
}
