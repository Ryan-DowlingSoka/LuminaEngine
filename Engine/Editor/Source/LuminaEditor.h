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

        /**
         * Creates a new project on disk from the Blank template.
         * Returns true on success and fills OutProjectFile with the absolute
         * path to the generated .lproject. On failure, returns false and
         * writes a human-readable reason into OutError.
         */
        bool CreateProject(FStringView NewProjectName, FStringView NewProjectPath, FFixedString& OutProjectFile, FString& OutError);

        /**
         * Synchronously runs the project's GenerateProject.bat in a detached
         * console so the user can watch premake's output. Returns true if the
         * spawn succeeded; the user closes the console when premake finishes.
         */
        bool GenerateProjectFiles(FStringView ProjectDirectory) const;

    protected:

        /** Editor doesn't auto-load a runtime world; the user picks one via the editor UI. */
        void LoadStartupMap() override {}
    };
    
    
    
}
