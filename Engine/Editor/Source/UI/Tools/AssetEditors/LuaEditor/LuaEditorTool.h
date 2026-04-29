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

        void DrawToolbar();
        void DrawStatusBar();
        void DrawSettingsPopup();

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

        // Autocomplete index, harvested from the live Lua VM at OnInitialize
        // and refreshable on demand. Two flat structures:
        //   TopLevelSymbols  — names visible at global scope
        //   MembersByPath    — for "Foo.Bar" → ["Baz", "Qux"]; lets us
        //                      offer table-member completions after `.`/`:`
        eastl::vector<Lua::FLuaSymbol>                                  AllSymbols;
        eastl::vector<eastl::string>                                    TopLevelSymbols;
        eastl::hash_map<eastl::string, eastl::vector<eastl::string>>    MembersByPath;
        eastl::hash_set<eastl::string>                                  TableNames;

        TextEditor::AutoCompleteConfig                                  AutoCompleteCfg;
    };
}
