#pragma once

#include <imgui/imgui.h>
#include "Events/EventProcessor.h"
#include "nlohmann/json.hpp"
#include "Tools/EditorToolContext.h"
#include "Tools/EditorToolModal.h"
#include "Tools/UI/DevelopmentToolUI.h"
#include "Tools/UI/ImGui/ImGuiX.h"



namespace Lumina
{
    class FGamePreviewTool;
    class FEditorToolModal;
    class FContentBrowserEditorTool;
    class FPrimitiveDrawManager;
    class FConsoleLogEditorTool;
    class FWorldEditorTool;
    class FEditorTool;
}


namespace Lumina
{
    class FEditorUI : public IDevelopmentToolUI, public IEditorToolContext
    {
    public:

        FEditorUI() = default;
        ~FEditorUI() override = default;
        LE_NO_COPYMOVE(FEditorUI);

        // Begin IEventHandler
        bool OnEvent(FEvent& Event) override;
        //~ End IEventHandler

        void Initialize(const FUpdateContext& UpdateContext) override;
        void Deinitialize(const FUpdateContext& UpdateContext) override;

        void OnStartFrame(const FUpdateContext& UpdateContext) override;
        void OnUpdate(const FUpdateContext& UpdateContext) override;
        void OnEndFrame(const FUpdateContext& UpdateContext) override;

        void DestroyTool(const FUpdateContext& UpdateContext, FEditorTool* Tool);

        void PushModal(const FString& Title, ImVec2 Size, TMoveOnlyFunction<bool()> DrawFunction) override;

        void OpenScriptEditor(FStringView ScriptPath) override;
        void OpenAssetEditor(const FGuid& AssetGUID) override;
        void OpenFileEditor(FStringView VirtualPath) override;
        void OnDestroyAsset(CObject* InAsset) override;


        template<typename T, typename... Args>
        requires eastl::is_base_of_v<FEditorTool, T> && eastl::is_constructible_v<T, Args...>
        T* CreateTool(Args&&... args);

        // Find an active tool by its singleton-style unique type id, or nullptr if not present.
        template<typename T>
        requires eastl::is_base_of_v<FEditorTool, T>
        T* FindTool() const;

        // Returns true if a tool of type T is currently alive (not pending destroy).
        template<typename T>
        requires eastl::is_base_of_v<FEditorTool, T>
        bool IsToolActive() const { return FindTool<T>() != nullptr; }

        // Toggle a singleton-style debug tool. Creates one with the given args
        // if none exists; otherwise schedules the live one for destruction.
        template<typename T, typename... Args>
        requires eastl::is_base_of_v<FEditorTool, T> && eastl::is_constructible_v<T, Args...>
        void ToggleTool(Args&&... args);

        // Convenience for the Tools menu — draws a MenuItem whose check state
        // mirrors the tool's existence and whose click toggles it.
        template<typename T, typename... Args>
        requires eastl::is_base_of_v<FEditorTool, T> && eastl::is_constructible_v<T, Args...>
        void DrawToolMenuItem(const char* Label, Args&&... args);

        void VerifyDirtyPackages();

        // Re-entry guard for the dirty-packages prompt. Goes true the first
        // frame we open the dialog and stays true until the dialog closes
        // (either an exit-completion path or user-cancel). Kept as a member
        // — not a function-local static — so Cancel can re-arm it.
        bool bVerifyingDirtyPackages = false;

    private:

        // Shared tail of tool creation: initializes the tool and queues it for
        // addition. Accepts (and passes through) nullptr so registry-driven
        // creation can chain without a null check at every call site.
        FEditorTool* FinalizeNewTool(FEditorTool* Tool);

        // Registers the built-in asset/file editors with FEditorToolRegistry.
        // Plugins add their own via the same registry in StartupModule.
        void RegisterBuiltinEditorTools();

        FEditorTool* FindToolByTypeID(uint32 TypeID) const;

        void EditorToolLayoutCopy(FEditorTool* SourceTool);

        /** Returns false if the tool wants to close */
        bool SubmitToolMainWindow(const FUpdateContext& UpdateContext, FEditorTool* EditorTool, ImGuiID TopLevelDockspaceID);
        void DrawToolContents(const FUpdateContext& UpdateContext, FEditorTool* Tool);


        void CreateGameViewportTool(const FUpdateContext& UpdateContext);
        void DestroyGameViewportTool(const FUpdateContext& UpdateContext);

        void DrawTitleBarMenu(const FUpdateContext& UpdateContext);
        void DrawTitleBarInfoStats(const FUpdateContext& UpdateContext);
        void DrawFileMenu();
        void DrawProjectMenu();
        void DrawToolsMenu();
        void DrawHelpMenu();
        void OpenProjectDialog();
        void NewProjectDialog();
        void ProjectCreatedDialog(FStringView ProjectFile);

        // Queues a modal to be opened on the next frame; used to chain dialogs
        // (Open → New) because FEditorModalManager only allows one active modal
        // at a time. Consumed in OnInitialize/post-DrawDialogue.
        void DeferShowDialog(TFunction<void()> Action) { PendingDialogAction = std::move(Action); }

        void OnProjectLoaded();

        void HandleUserInput(const FUpdateContext& UpdateContext);

    private:

        FEditorModalManager                             ModalManager;
        TFunction<void()>                               PendingDialogAction;

        ImGuiX::ApplicationTitleBar                     TitleBar;
        ImGuiWindowClass                                EditorWindowClass;

        FGamePreviewTool*                               GamePreviewTool = nullptr;
        FWorldEditorTool*                               WorldEditorTool = nullptr;

        FConsoleLogEditorTool*                          ConsoleLogTool = nullptr;
        FContentBrowserEditorTool*                      ContentBrowser = nullptr;
        TVector<FEditorTool*>                           EditorTools;
        FEditorTool*                                    LastActiveTool = nullptr;
        FString                                         FocusTargetWindowName; // If this is set we need to switch focus to this window

        THashMap<CObject*, FEditorTool*>                ActiveAssetTools;
        THashMap<FString, FEditorTool*>                 ActiveFileTools;
        TQueue<FEditorTool*>                            ToolsPendingAdd;
        TQueue<FEditorTool*>                            ToolsPendingDestroy;

        bool                                            bShowDearImGuiDemoWindow = false;
        bool                                            bShowImGuiStyleEditor = false;
        bool                                            bShowImPlotDemoWindow = false;

        // Last source path the debugger auto-opened. We re-open + refocus
        // whenever the paused source changes — covers both the initial
        // running→paused transition and the case where a step crosses into
        // a different file, where we want the editor tab to follow.
        FString                                         LuaDebuggerLastOpenedSource;

        float                                           SmoothedFPS = 60.0f;
        float                                           SmoothedFrameTime = 16.67f;
        static constexpr float                          FPSSmoothingFactor = 0.01f;
        static constexpr float                          ObjectSmoothingFactor = 0.05f;
    };

    template <typename T, typename ... Args>
    requires eastl::is_base_of_v<FEditorTool, T> && eastl::is_constructible_v<T, Args...>
    T* FEditorUI::CreateTool(Args&&... args)
    {
        return static_cast<T*>(FinalizeNewTool(Memory::New<T>(std::forward<Args>(args)...)));
    }

    template <typename T>
    requires eastl::is_base_of_v<FEditorTool, T>
    T* FEditorUI::FindTool() const
    {
        return static_cast<T*>(FindToolByTypeID(T::s_toolTypeID));
    }

    template <typename T, typename... Args>
    requires eastl::is_base_of_v<FEditorTool, T> && eastl::is_constructible_v<T, Args...>
    void FEditorUI::ToggleTool(Args&&... args)
    {
        if (FEditorTool* Existing = FindToolByTypeID(T::s_toolTypeID))
        {
            ToolsPendingDestroy.push(Existing);
            return;
        }
        CreateTool<T>(std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    requires eastl::is_base_of_v<FEditorTool, T> && eastl::is_constructible_v<T, Args...>
    void FEditorUI::DrawToolMenuItem(const char* Label, Args&&... args)
    {
        const bool bActive = IsToolActive<T>();
        if (ImGui::MenuItem(Label, nullptr, bActive))
        {
            ToggleTool<T>(std::forward<Args>(args)...);
        }
    }
}
