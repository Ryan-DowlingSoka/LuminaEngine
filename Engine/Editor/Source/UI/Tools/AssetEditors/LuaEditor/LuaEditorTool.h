#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/hash_set.h>
#include <EASTL/set.h>
#include <EASTL/vector.h>

#include "Containers/String.h"
#include "LuaAstAnalyzer.h"
#include "LuaTypeContext.h"
#include "Memory/SmartPtr.h"
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
        void DrawHelpMenu() override;

        bool IsUnsavedDocument() override { return bBufferDirty; }

    private:

        enum class EPalette : uint8 { Dark, Light };

        void LoadFromDisk();
        void ApplyEditorSettings();
        void RefreshBreakpointMarkers();

        void RebuildSymbolIndex();
        void OnAutoCompleteRequest(TextEditor::AutoCompleteState& State);
        void OnHoverIdentifier(const std::string& Word, const std::string& DottedPath);

        // Walks the buffer to populate DocumentOutline with function/local
        // declarations the user can click to navigate. Cheap regex-style scan
        // so it can re-run on every delayed change callback.
        void RebuildDocumentOutline();

        // Re-evaluates every watch expression in the current paused frame and
        // caches the result so the UI doesn't pay for evaluation per draw.
        // No-op when the debugger isn't paused.
        void RefreshWatchValues();

        // Draws a context popup for a single breakpoint allowing the user to
        // set a Lua condition, log message, hit-count ignore, and enable flag.
        // Uses Lua::FBreakpointSettings on FLuaDebugger as the data source.
        void DrawBreakpointSettingsPopup();

        // Buffer-driven local-symbol harvest: parses the editor buffer for
        // `local Name: Type = ...` / `local Name = ...` / `local function Name`
        // declarations so hover tooltips can show "local Name: Type" without
        // a full Luau type checker. Cheap and runs after each delayed change
        // callback fires.
        void RebuildLocalIndex();

        // Free-text hover. computes the (line, col) under the mouse from the
        // editor's screen-coord helpers and pops a tooltip when over a
        // string / number / keyword / type annotation. Identifier hovers
        // still go through the editor's built-in hover callback.
        void DrawFreeFormHoverTooltip();

        // Map of dotted path -> full symbol record. Built alongside the other
        // autocomplete indices in RebuildSymbolIndex so hover lookups are O(1).
        eastl::hash_map<eastl::string, Lua::FLuaSymbol>                         SymbolByPath;

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

        // Inline debugger overlay shown below the editor when FLuaDebugger
        // is paused at this file. Avoids spawning a separate top-level tool
        // that would steal layout space from the user's docking config.
        void DrawDebuggerPanel();
        void DrawDebuggerWatchSection();
        void DrawDebuggerBreakHistorySection();
        void DrawDebuggerHoverDuringPauseTooltip(const std::string& Word);
        bool IsDebuggerPausedHere() const;

        // Recursive expandable-value row for the debugger panels.
        void DrawExpandableValueRow(const eastl::string& Path,
                                     const char* Key,
                                     const char* Value,
                                     const char* TypeName,
                                     bool bIsExpandable,
                                     int Frame);

        // Right-side outline panel listing functions / locals / events.
        // Toggled from the toolbar; persists between draws via bShowOutline.
        void DrawOutlinePanel();

        // Inline value annotations after each visible line during pause:
        // walks the current frame's locals and draws "name = value"
        // ghost-text at end-of-line where the local is referenced.
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
        // Cached document size used by the status bar so we don't pay a full
        // GetText() copy every render frame. Recomputed in OnSave / LoadFromDisk
        // and in the (debounced) change callback.
        size_t              CachedBodySize = 0;
        // Per-line text cache for overlays (inlay hints, hover tooltip) that would
        // otherwise rebuild a std::string per visible line every frame. Refreshed
        // only when the undo index moves.
        std::vector<std::string> CachedLines;
        size_t              CachedLinesUndoIndex = ~size_t(0);
        bool                bBufferDirty = false;
        // Set in OnSave so the OnScriptLoaded broadcast we just emitted from
        // FScriptingContext::ScriptReloaded doesn't bounce back as an external
        // change. Cleared on the first matching broadcast.
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

        // External-change flag: flipped on by the OnScriptLoaded broadcast
        // (fired from FScriptingContext after either our own save or the
        // central content-browser file watcher detects a disk modification).
        // Update() consumes it on the main thread.
        bool                bExternalChangePending = false;
        FDelegateHandle     ScriptLoadedHandle;

        // Selected stack frame for the inline debugger panel. Re-clamped each
        // pause so a deeper call stack from a previous break doesn't index
        // out-of-range when the new call stack is shorter.
        int                 DebuggerSelectedFrame = 0;

        // Tracks the last line number we marked as the program-counter line
        // so we can clear it cheaply when the debugger advances past it.
        int                 PCMarkerLine = -1;

        // Autocomplete index, harvested from the live Lua VM at OnInitialize
        // and refreshable on demand. Three flat structures:
        //   TopLevelSymbols . full symbol records visible at global scope
        //   SymbolsByPath   . for "Foo.Bar" -> [child symbol records]; lets
        //                      us offer table-member completions after `.`/`:`
        //                      and surface kind/type/value-preview metadata.
        //   TableNames      . set of dotted paths that are tables, used for
        //                      cheap "is this a table?" checks during prefix
        //                      resolution.
        eastl::vector<Lua::FLuaSymbol>                                          AllSymbols;
        eastl::vector<Lua::FLuaSymbol>                                          TopLevelSymbols;
        eastl::hash_map<eastl::string, eastl::vector<Lua::FLuaSymbol>>          SymbolsByPath;
        eastl::hash_set<eastl::string>                                          TableNames;

        struct FLocalDecl
        {
            eastl::string TypeAnnotation;   // empty when the user didn't write `: T`
            eastl::string ValueHint;        // best-effort: "table" / "number" / "string" / ...
            eastl::string OriginName;       // when the type/hint was inherited from another local
            int           Line = -1;        // zero-based source line of the declaration
        };
        eastl::hash_map<eastl::string, FLocalDecl>                              Locals;

        TextEditor::AutoCompleteConfig                                  AutoCompleteCfg;

        // Bookmarks live in editor state alone. they're not source-of-truth
        // anywhere else and aren't persisted across editor sessions. F2 toggles,
        // Shift+F2 cycles through them.
        THashSet<int>       Bookmarks;

        bool                bShowOutline = false;
        bool                bShowInlineValuesWhilePaused = true;
        bool                bShowBreakHistory = true;

        // Watch expressions evaluated against the paused frame's environment.
        // Cleared between sessions; not persisted because an expression that
        // worked yesterday probably won't fit today's call site.
        struct FWatchEntry
        {
            eastl::string Expression;
            eastl::string LastValue;
            eastl::string LastType;
            bool          bDirty = true;
        };
        eastl::vector<FWatchEntry>      Watches;
        char                            WatchInputBuffer[256] = {0};

        // Document outline harvested by RebuildDocumentOutline.
        struct FOutlineItem
        {
            eastl::string Name;
            eastl::string Detail;        // signature for functions, type/value for locals
            char          Kind = 'l';    // 'f' function, 'l' local, 'e' export, 'c' comment marker
            int           Line = -1;
            int           Indent = 0;
        };
        eastl::vector<FOutlineItem>     DocumentOutline;
        char                            OutlineFilterBuffer[64] = {0};

        // Breakpoint settings popup state. RequestedBreakpointSettingsLine is
        // set by the gutter context menu's "Configure..." entry; the popup
        // opens on the next frame and binds against the line in question.
        int                             RequestedBreakpointSettingsLine = -1;
        char                            BpConditionBuffer[256] = {0};
        char                            BpLogMessageBuffer[256] = {0};
        int                             BpIgnoreCount = 0;
        bool                            bBpEnabled = true;

        // Compile-error overlay. The most recent compile diagnostic for this
        // script (line + message); rendered as a red gutter marker plus a
        // status-bar line. Cleared on a successful recompile.
        FDelegateHandle                     CompileErrorHandle;
        FDelegateHandle                     CompileSuccessHandle;
        bool                                bHasCompileError = false;
        int                                 CompileErrorLine = -1; // 1-based
        FString                             CompileErrorMessage;

        void ApplyCompileError(int Line, const FString& Message);
        void ClearCompileError();

        // Single canonical analysis-refresh entry point. Reparses the buffer
        // through the AST analyzer, rebuilds outline + locals, runs lint
        // against that same parse, pushes the source into the type context,
        // and refreshes inlay hints + gutter markers. Callers that already
        // have the buffer text pass it through to avoid an extra full-doc
        // copy; the no-arg overload is for paths that don't.
        //
        // Idempotent: safe to call multiple times in a row. Skips the heavy
        // passes when the buffer exceeds the per-feature size cap.
        void RefreshAnalysis(FStringView Body);
        void RefreshAnalysis();

        // Smart-selection action: expand the main cursor's selection (or the
        // word at the cursor when nothing is selected) to the smallest
        // enclosing AST scope. Repeated invocations expand outward.
        void ExpandSelectionToEnclosingNode();

        // Jump the cursor to the declaration of the local under the cursor.
        // Returns true on success - callers use the return to play a tiny
        // notification when the lookup hits no local (eases the "did F12 do
        // anything?" question).
        bool GoToDefinitionAtCursor();

        // Toggle highlight-all-references for the local at the cursor. Markers
        // are dropped on every line that references the local; activating
        // again clears them. Read-only feature; never edits the buffer.
        void ToggleHighlightReferencesAtCursor();

        // Replace the buffer with Luau's pretty-printed version. No-op on a
        // parse error; the diagnostic is surfaced via a notification.
        void FormatDocument();

        // Lint warnings produced by the Luau Analysis linter. Refreshed on
        // load, save, and external reloads. Surfaced as orange gutter strips
        // alongside breakpoint and compile-error markers.
        TVector<FLuaLintWarning>            LintWarnings;

        // Type errors from the typed Frontend's most recent check. Distinct
        // from compile errors (which are syntactic) and from lint warnings
        // (which are stylistic): each entry indicates code the runtime
        // can't safely execute - calling a non-function, missing field,
        // arity mismatch, etc.
        TVector<FLuaTypeDiagnostic>         TypeErrors;

        // Live AST of the current buffer. Owned by the editor; kept in sync
        // with the change callback. Drives outline, locals harvest, hover,
        // go-to-definition, find-references, and smart selection.
        FLuaAstAnalyzer                     AstAnalyzer;

        // Cached references for the highlight-all-references feature. Empty
        // when the highlight is off.
        TVector<FLuaSymbolRef>              HighlightedReferences;

        // Lazily-constructed Luau Frontend wrapper. Driven on demand so
        // opening a script doesn't pay the type-checker init cost upfront
        // (it can be 50-100 ms on first use); subsequent checks reuse the
        // already-warm Frontend and only pay re-check when the buffer is
        // dirty. Disabled on huge files (kTypeContextByteCap in the .cpp).
        TUniquePtr<FLuaTypeContext>         TypeContext;

        // User-facing toggle for the inlay-hint overlay (": type" ghost text
        // after `local x = ...` declarations). Enabled by default.
        bool                                bShowInlayHints = true;

        // Cached inlay hints, refreshed whenever AST + type-context updates.
        TVector<FLuaInlayHint>              InlayHints;

        // Per-frame cache for typed-hover lookups. The hover callback fires
        // on every frame the cursor sits on an identifier; without caching
        // we'd run findTypeAtPosition (and its arena walk) every redraw.
        struct FHoverTypeCache
        {
            int     Line   = -1;
            int     Column = -1;
            FString Text;       // resolved type text; empty when not resolved
            bool    bChecked = false;
        };
        FHoverTypeCache                     HoverTypeCache;

        // Inlay-hints renderer; reads InlayHints and paints them on top of
        // the editor's already-drawn glyphs.
        void DrawInlayHintsOverlay();

        // Refresh CachedLines from the editor if the buffer changed since last call.
        void RefreshLineCache();
    };
}
