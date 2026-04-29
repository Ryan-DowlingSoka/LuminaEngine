#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/hash_set.h>
#include <EASTL/set.h>
#include <EASTL/vector.h>

#include "Containers/String.h"
#include "Core/Threading/Atomic.h"
#include "Platform/Filesystem/DirectoryWatcher.h"
#include "Scripting/Lua/Scripting.h"
#include "UI/ColorTextEdit/TextEditor.h"
#include "UI/Tools/AssetEditors/AssetEditorTool.h"

namespace Lumina
{
    // Editor for raw .lua / .luau files. Doesn't go through the CObject asset
    // pipeline: scripts stay as plain text on disk so the runtime VM can load
    // them directly and external tools (Luau LSP, git diff) work as-is.
    class FLuaEditorTool : public FAssetEditorTool
    {
    public:

        LUMINA_EDITOR_TOOL(FLuaEditorTool)

        FLuaEditorTool(IEditorToolContext* Context, FStringView VirtualPath);

        bool IsSingleWindowTool() const override { return true; }
        const char* GetTitlebarIcon() const override { return LE_ICON_LANGUAGE_LUA; }

        void OnInitialize() override;
        void OnDeinitialize(const FUpdateContext& UpdateContext) override;
        void Update(const FUpdateContext& UpdateContext) override;
        void OnSave() override;

        bool IsUnsavedDocument() override { return bBufferDirty; }

    private:

        enum class EPalette : uint8 { Dark, Light };

        void LoadFromDisk();
        void StartWatching();
        void ApplyEditorSettings();
        void RefreshBreakpointMarkers();

        void RebuildSymbolIndex();
        void OnAutoCompleteRequest(TextEditor::AutoCompleteState& State);
        void OnHoverIdentifier(const std::string& Word, const std::string& DottedPath);

        // Map of dotted path → full symbol record. Built alongside the other
        // autocomplete indices in RebuildSymbolIndex so hover lookups are O(1).
        eastl::hash_map<eastl::string, Lua::FLuaSymbol>                         SymbolByPath;

        void DrawToolbar();
        void DrawStatusBar();
        void DrawSettingsPopup();

        // Inline debugger overlay shown below the editor when FLuaDebugger
        // is paused at this file. Avoids spawning a separate top-level tool
        // that would steal layout space from the user's docking config.
        void DrawDebuggerPanel();
        bool IsDebuggerPausedHere() const;

        void ToggleBreakpoint(int Line);

        FString             VirtualPath;
        FString             ParentDir;

        TextEditor          CodeEditor;
        std::string         LastSyncedText;
        bool                bBufferDirty = false;
        
        THashSet<int>       Breakpoints;

        // Editor display options.
        float               EditorFontScale = 1.25f;
        int                 EditorTabSize = 4;
        float               EditorLineSpacing = 1.0f;
        bool                bEditorShowWhitespace = false;
        bool                bEditorShowLineNumbers = true;
        bool                bEditorReadOnly = false;
        bool                bAutoIndent = true;
        bool                bShowMatchingBrackets = true;
        bool                bCompletePairedGlyphs = true;
        EPalette            EditorPalette = EPalette::Dark;

        FDirectoryWatcher   FileWatcher;
        TAtomic<bool>       bExternalChangePending{false};

        // Selected stack frame for the inline debugger panel. Re-clamped each
        // pause so a deeper call stack from a previous break doesn't index
        // out-of-range when the new call stack is shorter.
        int                 DebuggerSelectedFrame = 0;

        // Tracks the last line number we marked as the program-counter line
        // so we can clear it cheaply when the debugger advances past it.
        int                 PCMarkerLine = -1;

        // Autocomplete index, harvested from the live Lua VM at OnInitialize
        // and refreshable on demand. Three flat structures:
        //   TopLevelSymbols  — full symbol records visible at global scope
        //   SymbolsByPath    — for "Foo.Bar" → [child symbol records]; lets
        //                      us offer table-member completions after `.`/`:`
        //                      and surface kind/type/value-preview metadata.
        //   TableNames       — set of dotted paths that are tables, used for
        //                      cheap "is this a table?" checks during prefix
        //                      resolution.
        eastl::vector<Lua::FLuaSymbol>                                          AllSymbols;
        eastl::vector<Lua::FLuaSymbol>                                          TopLevelSymbols;
        eastl::hash_map<eastl::string, eastl::vector<Lua::FLuaSymbol>>          SymbolsByPath;
        eastl::hash_set<eastl::string>                                          TableNames;

        TextEditor::AutoCompleteConfig                                  AutoCompleteCfg;
    };
}
