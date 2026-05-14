#pragma once
#include "UI/Tools/EditorTool.h"

#include <atomic>
#include <mutex>
#include <thread>

#include "Memory/SmartPtr.h"

namespace Lumina
{
    /**
     * Editor tool for packaging the project — Godot-style "Export":
     *   1. Cooks the asset graph rooted at Project.GameStartupMap into a .pak
     *   2. Optionally invokes MSBuild to produce the Game|Shipping executable
     *   3. Copies the resulting exe + DLLs alongside the .pak
     *
     * The cook step runs on the main thread (touches the asset registry +
     * CObject system, both racy with engine GC), but the long MSBuild +
     * binary-copy stage runs on a worker thread so the editor stays
     * interactive. Build output streams into the log live via a
     * mutex-protected buffer drained on each UI tick.
     */
    class FProjectPackagerEditorTool : public FEditorTool
    {
    public:

        LUMINA_SINGLETON_EDITOR_TOOL(FProjectPackagerEditorTool)

        FProjectPackagerEditorTool(IEditorToolContext* Context)
            : FEditorTool(Context, "Package Project", nullptr)
        {}

        ~FProjectPackagerEditorTool() override;

        bool IsSingleWindowTool() const override { return true; }
        const char* GetTitlebarIcon() const override { return LE_ICON_PACKAGE_VARIANT; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void DrawHelpMenu() override;

    private:

        // High-level state. Cook is sub-second so it lives on the main thread
        // for one frame; Build+Copy is the long part and runs on the worker.
        enum class EStage
        {
            Idle,
            Cooking,
            Building,
            Copying,
            Done,
        };

        // Shared between the UI thread and the worker thread. Owned by a
        // shared_ptr so the worker keeps it alive even if the tool is
        // closed mid-build.
        struct FBuildSession
        {
            std::atomic<bool>           bDone{false};
            std::atomic<bool>           bSuccess{false};
            std::mutex                  PendingMutex;
            TVector<FString>            PendingLines;       // worker -> UI
            FString                     OutputDirectory;    // populated on completion
            FString                     PakPath;
            FString                     ErrorMessage;
        };

        void DrawWindow(bool bIsFocused);
        void DrawExtrasSection();
        void RunCookOnly();
        void RunFullPackage();

        // Pulls any new lines from Session->PendingLines into LogLines.
        void DrainSession();

        // Adds a line directly (UI thread only — main path for synchronous events).
        void AppendLog(FStringView Line);
        void ClearLog();

        FString             OutputDir;          // editable; defaults set on first draw
        FString             MSBuildPath;        // editable
        int32               ConfigIndex     = 0; // 0=Shipping, 1=Development, 2=Debug
        bool                bExtractScriptsLoose = false;
        TVector<FString>    ExtraFiles;
        TVector<FString>    ExtraDirectories;
        int32               SelectedExtraFile      = -1;
        int32               SelectedExtraDirectory = -1;
        EStage              Stage           = EStage::Idle;
        bool                bLastSuccess    = false;
        FString             LastError;
        FString             LastPakPath;
        FString             LastOutputDir;
        TVector<FString>    LogLines;           // ring-bounded; UI thread only
        bool                bAutoScroll     = true;

        // Async pipeline state.
        TSharedPtr<FBuildSession>   Session;
        std::thread                 Worker;

        static constexpr int32 MaxLogLines = 2000;
    };
}
