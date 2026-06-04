#pragma once

#include "Containers/Array.h"
#include "Containers/String.h"
#include "LuaAnnotationSchema.h"
#include "LuaAstAnalyzer.h"
#include "LuaTypeContext.h"
#include "Memory/SmartPtr.h"
#include "Scripting/Lua/Scripting.h"
#include "UI/ColorTextEdit/TextEditor.h"
#include "UI/Tools/AssetEditors/AssetEditorTool.h"

namespace Lumina
{
    // Editor for raw .lua / .luau files. Not in the CObject asset pipeline: scripts stay as
    // plain text so the runtime VM and external tools (Luau LSP, git diff) work as-is.
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
        void DrawHelpMenu() override;

        bool IsUnsavedDocument() override { return bBufferDirty; }

    private:

        enum class EPalette : uint8 { Dark, Light };

        void LoadFromDisk();
        // Copy persisted CLuaEditorSettings values into the cached member fields. Run at construction
        // and on the OnSettingsSaved live-refresh.
        void PullSettings();
        void ApplyEditorSettings();
        void RefreshBreakpointMarkers();

        void RebuildSymbolIndex();
        void OnAutoCompleteRequest(TextEditor::AutoCompleteState& State);

        // Annotation-DSL autocomplete: fills directive names or --@export/--@rpc argument tokens for the
        // given context, filtered by Partial. Used inside `--@...` comments where Lua symbols don't apply.
        void FillAnnotationCompletions(TextEditor::AutoCompleteState& State,
                                       ELuaAnnotationContext Context, FStringView Partial);
        void OnHoverIdentifier(const std::string& Word, const std::string& DottedPath);

        // Populates DocumentOutline with clickable function/local declarations.
        // Cheap regex-style scan, fine to re-run on every delayed change callback.
        void RebuildDocumentOutline();

        // Re-evaluates every watch expression in the paused frame and caches the result
        // so the UI doesn't evaluate per draw. No-op when not paused.
        void RefreshWatchValues();

        // Context popup to set a breakpoint's condition, log message, hit-count ignore, and
        // enable flag. Backed by Lua::FBreakpointSettings on FLuaDebugger.
        void DrawBreakpointSettingsPopup();

        // Harvests `local` declarations from the buffer so hover can show "local Name: Type"
        // without a full Luau type checker. Cheap; runs after each delayed change callback.
        void RebuildLocalIndex();

        // Harvests table-member assignments (`Script.X = 0`, methods, `self.X`) from the
        // AST so hover/autocomplete describe user-authored fields. Runs alongside RebuildLocalIndex.
        void RebuildMemberIndex();

        // Scans `--@export(...)` annotations from the buffer into ExportMetaByMember for hover.
        void RebuildExportMeta();

        // Validates --@ annotations (wrong placement, duplicate params) into AnnotationErrors.
        void RebuildAnnotationDiagnostics(FStringView Body);

        // Parameter-hint popup shown while the cursor sits inside a function call's
        // argument list; resolves the callee against engine symbols and buffer functions.
        void DrawSignatureHelp();

        // Free-text hover: tooltip when over a string/number/keyword/type annotation.
        // Identifier hovers still go through the editor's built-in hover callback.
        void DrawFreeFormHoverTooltip();

        // Map of dotted path -> full symbol record. Built alongside the other
        // autocomplete indices in RebuildSymbolIndex so hover lookups are O(1).
        THashMap<FString, Lua::FLuaSymbol>                                      SymbolByPath;

        void DrawToolbar();
        void DrawStatusBar();
        void DrawSettingsPopup();
        void DrawSnippetsPopup();
        void DrawHelpPopup();
        void DrawGotoLinePopup();
        void DrawFormatPopup();
        void DrawProblemsPopup();
        void HandleEditorShortcuts();
        void InsertSnippet(const char* Snippet);

        // Inline debugger overlay below the editor when paused here; avoids a separate
        // top-level tool that would steal layout space from the user's docking config.
        void DrawDebuggerPanel();
        void DrawDebuggerWatchSection();
        void DrawDebuggerBreakHistorySection();
        void DrawDebuggerHoverDuringPauseTooltip(const std::string& Word);
        bool IsDebuggerPausedHere() const;

        // Recursive expandable-value row for the debugger panels.
        void DrawExpandableValueRow(const FString& Path,
                                     const char* Key,
                                     const char* Value,
                                     const char* TypeName,
                                     bool bIsExpandable,
                                     int Frame);

        // Right-side outline panel listing functions / locals / events.
        // Toggled from the toolbar; persists between draws via bShowOutline.
        void DrawOutlinePanel();

        // During pause, draws "name = value" ghost-text at end-of-line for each frame
        // local where it's referenced.
        void DrawInlineValueOverlay();

        void ToggleBreakpoint(int Line);
        void ToggleBookmark(int Line);
        void NavigateBookmark(bool bForward);
        void RunToCursor();

        FString             VirtualPath;
        FString             ParentDir;

        TextEditor          CodeEditor;
        std::string         LastSyncedText;
        size_t              LastSyncedUndoIndex = 0;
        // Cached doc size for the status bar (avoids a GetText() copy per frame);
        // recomputed in OnSave/LoadFromDisk and the debounced change callback.
        size_t              CachedBodySize = 0;
        // Per-line text cache for overlays, avoiding a std::string rebuild per line per
        // frame. Refreshed only when the undo index moves.
        std::vector<std::string> CachedLines;
        size_t              CachedLinesUndoIndex = ~size_t(0);
        bool                bBufferDirty = false;
        // Set in OnSave so our own OnScriptLoaded broadcast doesn't bounce back as an
        // external change. Cleared on the first matching broadcast.
        bool                bIgnoreNextReload = false;

        THashSet<int>       Breakpoints;

        // Editor display options.
        float               EditorFontScale = 1.25f;
        int                 EditorTabSize = 4;
        float               EditorLineSpacing = 1.0f;
        bool                bEditorShowWhitespace = false;
        bool                bEditorShowLineNumbers = true;
        bool                bEditorShowMiniMap = true;
        bool                bEditorReadOnly = false;
        bool                bAutoIndent = true;
        bool                bShowMatchingBrackets = true;
        bool                bCompletePairedGlyphs = true;
        bool                bInsertSpacesOnTabs = false;
        bool                bTrimTrailingOnSave = false;
        bool                bAutoTriggerCompletion = true;
        int                 AutoTriggerDelayMs = 100;
        EPalette            EditorPalette = EPalette::Dark;

        int                 GotoLineBuffer = 1;
        bool                bRequestOpenGoto = false;

        // Deferred-open flags for overflow-menu popups: OpenPopup can't fire from inside the
        // menu (wrong ID-stack scope), so DrawToolbar opens them at root scope next frame.
        bool                bRequestOpenSnippets    = false;
        bool                bRequestOpenBookmarks   = false;
        bool                bRequestOpenBreakpoints = false;
        bool                bRequestOpenHelp        = false;
        bool                bRequestOpenSettings    = false;

        // External-change flag set by the OnScriptLoaded broadcast (our save or the file
        // watcher detecting a disk change). Update() consumes it on the main thread.
        bool                bExternalChangePending = false;
        FDelegateHandle     ScriptLoadedHandle;

        // Live-refresh subscription: re-pull + re-apply when CLuaEditorSettings is saved from the
        // global Settings panel, so palette/appearance edits show up without reopening the editor.
        FDelegateHandle     SettingsSavedHandle;

        // Retargets VirtualPath when this file is renamed/moved in the content browser, so a
        // subsequent save writes the new file instead of recreating the old path.
        FDelegateHandle     FileRenamedHandle;

        // Selected stack frame for the debugger panel; re-clamped each pause so a deeper
        // prior call stack doesn't index out of range when the new one is shorter.
        int                 DebuggerSelectedFrame = 0;

        // Tracks the last line number we marked as the program-counter line
        // so we can clear it cheaply when the debugger advances past it.
        int                 PCMarkerLine = -1;

        // Autocomplete index harvested from the live Lua VM (refreshable): TopLevelSymbols
        // (global scope), SymbolsByPath ("Foo.Bar" members), TableNames (is-a-table checks).
        TVector<Lua::FLuaSymbol>                                          AllSymbols;
        TVector<Lua::FLuaSymbol>                                          TopLevelSymbols;
        THashMap<FString, TVector<Lua::FLuaSymbol>>                             SymbolsByPath;
        THashSet<FString>                                                       TableNames;

        struct FLocalDecl
        {
            FString TypeAnnotation;   // empty when the user didn't write `: T`
            FString ValueHint;        // best-effort: "table" / "number" / "string" / ...
            FString OriginName;       // when the type/hint was inherited from another local
            int           Line = -1;        // zero-based source line of the declaration
        };
        THashMap<FString, FLocalDecl>                                           Locals;

        // Members written onto a table in this buffer (`Script.X = 0`, methods,
        // `self.X = ...`), keyed by dotted path "Owner.Name". Drives hover ("X:
        // number, field of Script") and member autocomplete for user-authored
        // fields the runtime VM never harvested. Rebuilt from the AST per edit.
        struct FMemberDecl
        {
            FString Owner;       // owning table expression, e.g. "Script" / "self" (empty for a plain function)
            FString TypeName;    // best-effort type: annotation or syntactic value hint
            int     Line = -1;   // 1-based declaration line
            bool    bMethod = false;
            bool    bFunction = false;
            bool    bVararg = false;
            TVector<FString> Params; // parameter names when bFunction (powers signature help)
        };
        THashMap<FString, FMemberDecl>                                          Members;

        // --@export(...) metadata per field name, scanned from the buffer (cheap text pass)
        // in RefreshAnalysis. Surfaced in the field hover so clamp/category/units/tooltip read
        // off the same tooltip. Mirrored as plain strings to keep the runtime header out of here.
        struct FExportArg { FString Key; FString Value; };
        THashMap<FString, TVector<FExportArg>>                                  ExportMetaByMember;

        TextEditor::AutoCompleteConfig                                  AutoCompleteCfg;

        // Bookmarks live in editor state alone, not persisted across sessions.
        // F2 toggles, Shift+F2 cycles.
        THashSet<int>       Bookmarks;

        bool                bShowOutline = false;
        bool                bShowInlineValuesWhilePaused = true;

        // Watch expressions evaluated against the paused frame's environment;
        // cleared between sessions, not persisted (won't fit a different call site).
        struct FWatchEntry
        {
            FString Expression;
            FString LastValue;
            FString LastType;
            bool          bDirty = true;
        };
        TVector<FWatchEntry>            Watches;
        char                            WatchInputBuffer[256] = {0};

        // Document outline harvested by RebuildDocumentOutline.
        struct FOutlineItem
        {
            FString Name;
            FString Detail;              // signature for functions, type/value for locals
            char          Kind = 'l';    // 'f' function, 'l' local, 'e' export, 'c' comment marker
            int           Line = -1;
            int           Indent = 0;
        };
        TVector<FOutlineItem>           DocumentOutline;
        char                            OutlineFilterBuffer[64] = {0};

        // Breakpoint settings popup state; RequestedBreakpointSettingsLine is set by the
        // gutter "Configure..." entry, opening next frame bound to that line.
        int                             RequestedBreakpointSettingsLine = -1;
        char                            BpConditionBuffer[256] = {0};
        char                            BpLogMessageBuffer[256] = {0};
        int                             BpIgnoreCount = 0;
        bool                            bBpEnabled = true;

        // Compile-error overlay: the latest diagnostic (line + message), shown as a red
        // gutter marker and status-bar line. Cleared on a successful recompile.
        FDelegateHandle                     CompileErrorHandle;
        FDelegateHandle                     CompileSuccessHandle;

        // Globals changed (e.g. a runtime component type created/removed) -> re-harvest the symbol
        // index. Coalesced to one rebuild per frame via the dirty flag (the signal can fire in bursts).
        FDelegateHandle                     GlobalsChangedHandle;
        bool                                bSymbolsDirty = false;
        bool                                bHasCompileError = false;
        int                                 CompileErrorLine = -1; // 1-based
        FString                             CompileErrorMessage;

        void ApplyCompileError(int Line, const FString& Message);
        void ClearCompileError();

        void RefreshAnalysis(FStringView Body);
        void RefreshAnalysis();
        
        void ExpandSelectionToEnclosingNode();
        
        bool GoToDefinitionAtCursor();

        void ToggleHighlightReferencesAtCursor();
        
        void FormatDocument();
        
        TVector<FLuaLintWarning>            LintWarnings;
        
        TVector<FLuaTypeDiagnostic>         TypeErrors;

        // Our own annotation validation (wrong-place --@export/@rpc/@replicated, duplicate params),
        // reused as FLuaTypeDiagnostic so they render exactly like type errors. Rebuilt per analysis.
        TVector<FLuaTypeDiagnostic>         AnnotationErrors;
        
        FLuaAstAnalyzer                     AstAnalyzer;
        
        TVector<FLuaSymbolRef>              HighlightedReferences;
        
        TUniquePtr<FLuaTypeContext>         TypeContext;

        struct FHoverTypeCache
        {
            int     Line   = -1;
            int     Column = -1;
            FString Text;       // resolved type text; empty when not resolved
            bool    bChecked = false;
        };
        FHoverTypeCache                     HoverTypeCache;
        
        void RefreshLineCache();
    };
}
