#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"
#include "Core/Threading/Atomic.h"
#include "Platform/Filesystem/DirectoryWatcher.h"
#include "Renderer/RHIFwd.h"
#include "UI/ColorTextEdit/TextEditor.h"
#include "UI/Tools/AssetEditors/AssetEditorTool.h"

namespace Rml
{
    class Context;
}

namespace Lumina
{
    // Editor for raw .rml files. Doesn't go through the CObject asset pipeline:
    // RmlUI documents stay as plain text on disk so Lua / RmlUI can include
    // each other directly without a binary package layer.
    class FRmlUiEditorTool : public FAssetEditorTool
    {
    public:

        LUMINA_EDITOR_TOOL(FRmlUiEditorTool)

        FRmlUiEditorTool(IEditorToolContext* Context, FStringView VirtualPath);

        bool IsSingleWindowTool() const override { return false; }
        const char* GetTitlebarIcon() const override { return LE_ICON_FILE_CODE; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void Update(const FUpdateContext& UpdateContext) override;
        void OnSave() override;

        bool IsUnsavedDocument() override { return bBufferDirty; }

        void InitializeDockingLayout(ImGuiID InDockspaceID, const ImVec2& InDockspaceSize) const override;

    private:

        enum class EBgMode : uint8 { Checker, Solid, Transparent };
        enum class EPalette : uint8 { Dark, Light };

        void LoadFromDisk();
        void ReloadDocument();
        void EnsurePreviewTarget(uint32 Width, uint32 Height);
        void TearDownPreview();
        void StartWatching();
        void ApplyEditorSettings();
        void DrawPreviewToolbar();
        void DrawPreviewCanvas();
        void DrawEditorToolbar();
        void DrawEditorStatusBar();
        void DrawSnippetsPopup();
        void DrawFormatPopup();
        void DrawHelpPopup();
        void DrawGotoLinePopup();
        void HandleEditorShortcuts();
        void InsertSnippet(const char* Snippet);
        void PersistSettings() const;

        // Inline color-swatch overlay. Walks visible lines, scans for
        // #RRGGBB / #RRGGBBAA hex literals, draws a clickable square at the
        // start of each one, and on click opens an ImGui color picker that
        // commits edits back into the editor as a normal text replacement
        // (so undo/redo, dirty tracking, and live reload all work).
        void DrawInlineColorSwatches();

        FString                     VirtualPath;
        FString                     ParentDir;

        TextEditor                  CodeEditor;
        std::string                 LastSyncedText;     // matches disk + last preview reload
        bool                        bBufferDirty = false;
        bool                        bAutoReload = true;

        // --- Preview ---
        Rml::Context*               PreviewContext = nullptr;
        FRHIImageRef                PreviewTarget;
        uint32                      PreviewWidth = 0;
        uint32                      PreviewHeight = 0;

        // Canvas resolution selected by the user (decoupled from pane size).
        // 0,0 = "fit to pane".
        uint32                      CanvasWidth = 0;
        uint32                      CanvasHeight = 0;
        int                         ResolutionPreset = 0;     // 0 = Fit
        float                       PreviewDpiScale = 1.5f;
        float                       ViewZoom = 1.0f;          // pan/zoom over the canvas
        ImVec2                      ViewPan{0.0f, 0.0f};

        // Background.
        EBgMode                     BgMode = EBgMode::Checker;
        ImVec4                      BgColor{0.10f, 0.10f, 0.12f, 1.0f};

        // Overlays.
        bool                        bShowGrid = false;
        float                       GridSize = 32.0f;         // canvas-space px
        ImVec4                      GridColor{1.0f, 1.0f, 1.0f, 0.10f};

        bool                        bShowSafeZones = false;
        float                       SafeZoneAction = 0.95f;   // 95% — action safe
        float                       SafeZoneTitle = 0.90f;    // 90% — title safe
        ImVec4                      SafeZoneColor{1.0f, 0.85f, 0.30f, 0.65f};

        bool                        bShowRulers = false;

        // --- Editor ---
        float                       EditorFontScale = 1.25f;
        int                         EditorTabSize = 4;
        float                       EditorLineSpacing = 1.0f;
        bool                        bEditorShowWhitespace = false;
        bool                        bEditorShowLineNumbers = true;
        bool                        bEditorShowMiniMap = true;
        bool                        bEditorReadOnly = false;
        bool                        bAutoIndent = true;
        bool                        bShowMatchingBrackets = true;
        bool                        bCompletePairedGlyphs = true;
        bool                        bInsertSpacesOnTabs = false;
        bool                        bTrimTrailingOnSave = false;
        EPalette                    EditorPalette = EPalette::Dark;

        int                         GotoLineBuffer = 1;
        bool                        bRequestOpenGoto = false;

        FDirectoryWatcher           FileWatcher;
        TAtomic<bool>               bExternalChangePending{false};
    };
}
