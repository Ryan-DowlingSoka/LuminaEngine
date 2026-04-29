#include "LuaEditorTool.h"

#include "Config/Config.h"
#include "FileSystem/FileSystem.h"
#include "Log/Log.h"
#include "Scripting/Lua/Scripting.h"
#include "Scripting/Lua/Debugger/LuaDebugger.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"
#include "Tools/UI/ImGui/ImGuiFonts.h"
#include "Tools/UI/ImGui/ImGuiX.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace Lumina
{
    namespace
    {
        FString DisplayNameFromPath(FStringView Path)
        {
            const FStringView Name = VFS::FileName(Path);
            return FString(Name.data(), Name.size());
        }

        // Luau reserved words. Surfaced first in the suggestion list when they
        // match the search prefix.
        const char* const kLuauKeywords[] = {
            "and", "break", "continue", "do", "else", "elseif", "end", "false",
            "for", "function", "if", "in", "local", "nil", "not", "or", "repeat",
            "return", "then", "true", "until", "while", "self", "type", "export",
        };

        bool IsIdentChar(char C)
        {
            return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z')
                || (C >= '0' && C <= '9') || C == '_';
        }

        // Walks left from `EndCol` over a chain of `Ident.Ident` / `Ident:`
        // segments and returns the resolved owner path (e.g. "Engine.VFS"
        // when the cursor is just after "Engine.VFS." or "Engine.VFS:").
        // Returns empty if the cursor isn't sitting at a member-access spot.
        eastl::string ResolveOwnerPath(const std::string& Line, int EndCol)
        {
            int I = std::min(EndCol, (int)Line.size());
            if (I <= 0) return {};

            // Must be immediately preceded by '.' or ':'.
            const char Sep = Line[I - 1];
            if (Sep != '.' && Sep != ':') return {};

            // Walk back accumulating segments separated by '.'.
            // ':' only valid as the last (closest-to-cursor) separator.
            eastl::vector<eastl::string> SegmentsRev;
            int Cursor = I - 1;
            while (Cursor > 0)
            {
                const char Here = Line[Cursor];
                if (Here != '.' && Here != ':') break;

                int End = Cursor;
                int Start = End;
                while (Start > 0 && IsIdentChar(Line[Start - 1])) --Start;
                if (Start == End) return {};
                SegmentsRev.emplace_back(Line.data() + Start, Line.data() + End);

                if (Here == ':' && SegmentsRev.size() != 1) return {};
                Cursor = Start - 1;
            }

            if (SegmentsRev.empty()) return {};

            eastl::string Path;
            for (auto Itr = SegmentsRev.rbegin(); Itr != SegmentsRev.rend(); ++Itr)
            {
                if (!Path.empty()) Path += '.';
                Path += *Itr;
            }
            return Path;
        }

        const char* kKeyUsePlatformEditor = "Editor.LuaEditor.UsePlatformEditor";
        const char* kKeyFontScale         = "Editor.LuaEditor.FontScale";
        const char* kKeyTabSize           = "Editor.LuaEditor.TabSize";
        const char* kKeyLineSpacing       = "Editor.LuaEditor.LineSpacing";
        const char* kKeyShowWhitespace    = "Editor.LuaEditor.ShowWhitespace";
        const char* kKeyShowLineNumbers   = "Editor.LuaEditor.ShowLineNumbers";
        const char* kKeyAutoIndent        = "Editor.LuaEditor.AutoIndent";
        const char* kKeyMatchBrackets     = "Editor.LuaEditor.MatchBrackets";
        const char* kKeyCompletePairs     = "Editor.LuaEditor.CompletePairs";
        const char* kKeyPalette           = "Editor.LuaEditor.Palette";
    }

    FLuaEditorTool::FLuaEditorTool(IEditorToolContext* Context, FStringView InVirtualPath)
        : FAssetEditorTool(Context, DisplayNameFromPath(InVirtualPath))
        , VirtualPath(InVirtualPath.data(), InVirtualPath.size())
    {
        const FStringView ParentView = VFS::Parent(InVirtualPath, true);
        ParentDir = FString(ParentView.data(), ParentView.size());

        // Pull persisted preferences. Defaults are registered in LuminaEditor.cpp.
        EditorFontScale         = GConfig->GetFloat(kKeyFontScale);
        EditorTabSize           = std::max(1, std::min(8, GConfig->GetInt(kKeyTabSize)));
        EditorLineSpacing       = GConfig->GetFloat(kKeyLineSpacing);
        bEditorShowWhitespace   = GConfig->GetBool(kKeyShowWhitespace);
        bEditorShowLineNumbers  = GConfig->GetBool(kKeyShowLineNumbers);
        bAutoIndent             = GConfig->GetBool(kKeyAutoIndent);
        bShowMatchingBrackets   = GConfig->GetBool(kKeyMatchBrackets);
        bCompletePairedGlyphs   = GConfig->GetBool(kKeyCompletePairs);
        const FString PaletteStr = GConfig->GetString(kKeyPalette);
        EditorPalette = (PaletteStr == "Light") ? EPalette::Light : EPalette::Dark;
    }

    void FLuaEditorTool::OnInitialize()
    {
        FAssetEditorTool::OnInitialize();

        CodeEditor.SetLanguage(TextEditor::Language::Lua());
        ApplyEditorSettings();
        LoadFromDisk();

        // Re-hydrate breakpoints from the runtime debugger so they survive
        // closing and reopening the editor. The debugger is the source of
        // truth — the editor's THashSet just mirrors it for gutter rendering.
        for (int Line : Lua::FLuaDebugger::Get().GetBreakpointLines(FStringView(VirtualPath.c_str(), VirtualPath.size())))
        {
            Breakpoints.insert(Line);
        }
        RefreshBreakpointMarkers();

        // Toggle a breakpoint by right-clicking the line number gutter.
        CodeEditor.SetLineNumberContextMenuCallback([this](int Line)
        {
            if (ImGui::MenuItem(Breakpoints.count(Line) ? "Remove breakpoint" : "Add breakpoint"))
            {
                ToggleBreakpoint(Line);
            }
            if (ImGui::MenuItem("Clear all breakpoints", nullptr, false, !Breakpoints.empty()))
            {
                Lua::FLuaDebugger::Get().ClearBreakpointsFor(FStringView(VirtualPath.c_str(), VirtualPath.size()));
                Breakpoints.clear();
                RefreshBreakpointMarkers();
            }
        });

        CodeEditor.SetChangeCallback([this]
        {
            const std::string Current = CodeEditor.GetText();
            if (Current == LastSyncedText)
            {
                return;
            }
            bBufferDirty = true;
        }, /*delay ms*/ 100);

        StartWatching();

        RebuildSymbolIndex();
        AutoCompleteCfg.triggerOnTyping = true;
        AutoCompleteCfg.triggerOnShortcut = true;
        AutoCompleteCfg.triggerInComments = false;
        AutoCompleteCfg.triggerInStrings = false;
        AutoCompleteCfg.triggerDelay = std::chrono::milliseconds{100};
        AutoCompleteCfg.callback = [this](TextEditor::AutoCompleteState& State)
        {
            OnAutoCompleteRequest(State);
        };
        CodeEditor.SetAutoCompleteConfig(&AutoCompleteCfg);

        // Hover info: identifier under the mouse → tooltip showing kind +
        // type + value/signature, sourced from the same harvested symbol
        // table the autocomplete uses.
        CodeEditor.SetHoverCallback([this](const std::string& Word, const std::string& DottedPath)
        {
            OnHoverIdentifier(Word, DottedPath);
        });

        CreateToolWindow("LuaEditor", [this](bool bFocused)
        {
            DrawToolbar();
            ImGui::Separator();

            const bool bPausedHere = IsDebuggerPausedHere();

            const ImVec2 Avail = ImGui::GetContentRegionAvail();
            const float StatusBarHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

            // Reserve a slot under the editor for the inline debugger panel
            // when paused at this file. Tuned to fit toolbar + a small split
            // for call stack / locals without dwarfing the code view.
            const float DebuggerPanelHeight = bPausedHere ? std::min(Avail.y * 0.45f, 320.0f) : 0.0f;
            const float SpacingForPanel = bPausedHere ? ImGui::GetStyle().ItemSpacing.y : 0.0f;
            const ImVec2 EditorSize(Avail.x, std::max(32.0f, Avail.y - StatusBarHeight - DebuggerPanelHeight - SpacingForPanel));

            // Ctrl+wheel over the editor adjusts font scale. Steal the wheel
            // so TextEditor doesn't also use it for vertical scroll.
            const ImVec2 EditorMin = ImGui::GetCursorScreenPos();
            const ImVec2 EditorMax(EditorMin.x + EditorSize.x, EditorMin.y + EditorSize.y);
            ImGuiIO& Io = ImGui::GetIO();
            if (Io.KeyCtrl && Io.MouseWheel != 0.0f && ImGui::IsMouseHoveringRect(EditorMin, EditorMax))
            {
                EditorFontScale = std::clamp(EditorFontScale * (1.0f + Io.MouseWheel * 0.1f), 0.5f, 4.0f);
                Io.MouseWheel = 0.0f;
            }

            // The proportional UI font has variable advance widths — TextEditor
            // assumes a uniform glyph cell, so columns and selections drift.
            // Push the bundled JetBrainsMono font for code rendering.
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Mono);
            ImGui::PushFontSize(ImGui::GetStyle().FontSizeBase * EditorFontScale);
            CodeEditor.Render("##lua_text", EditorSize);
            ImGui::PopFontSize();
            ImGuiX::Font::PopFont();

            if (bPausedHere)
            {
                DrawDebuggerPanel();
            }

            DrawStatusBar();
        });
    }

    void FLuaEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
        FileWatcher.Stop();
    }

    void FLuaEditorTool::Update(const FUpdateContext& UpdateContext)
    {
        FAssetEditorTool::Update(UpdateContext);

        if (bExternalChangePending.exchange(false, Atomic::MemoryOrderAcquire))
        {
            if (!bBufferDirty)
            {
                LoadFromDisk();
            }
            else
            {
                LOG_WARN("[LuaEditor] '{}' changed on disk but buffer is dirty; ignoring.", VirtualPath.c_str());
            }
        }

        // Sync the PC marker with the live debugger state. The check is cheap
        // and runs every frame so the marker advances when the user steps to
        // a new line. Scroll-to-line only fires when the line actually moved
        // so the user's manual scroll is preserved between steps on the same
        // line (e.g. immediately after they look at the locals panel).
        const int LivePC = IsDebuggerPausedHere() ? Lua::FLuaDebugger::Get().GetPausedLineZeroBased() : -1;
        if (LivePC != PCMarkerLine)
        {
            RefreshBreakpointMarkers();
            if (LivePC >= 0)
            {
                CodeEditor.ScrollToLine(LivePC, TextEditor::Scroll::alignMiddle);
                DebuggerSelectedFrame = 0;
            }
        }
    }

    void FLuaEditorTool::OnSave()
    {
        const std::string Body = CodeEditor.GetText();
        const FStringView View(Body.data(), Body.size());

        if (!VFS::WriteFile(FStringView(VirtualPath.c_str(), VirtualPath.size()), View))
        {
            ImGuiX::Notifications::NotifyError("Failed to save '{0}'.", VirtualPath.c_str());
            return;
        }

        LastSyncedText = Body;
        bBufferDirty = false;

        // Tell FScriptingContext to recompile the source and re-bind every
        // attached SScriptComponent referencing this path. The reload is
        // deferred to the next ProcessDeferredActions tick on the main thread,
        // so the editor stays in sync without re-entering the Lua VM here.
        Lua::FScriptingContext::Get().ScriptReloaded(FStringView(VirtualPath.c_str(), VirtualPath.size()));

        ImGuiX::Notifications::NotifySuccess("Saved '{0}'.", VirtualPath.c_str());
    }

    // -------------------------------------------------------------- helpers ---

    void FLuaEditorTool::LoadFromDisk()
    {
        FString Body;
        if (!VFS::ReadFile(Body, FStringView(VirtualPath.c_str(), VirtualPath.size())))
        {
            LOG_WARN("[LuaEditor] Could not read '{}'.", VirtualPath.c_str());
            CodeEditor.SetText("");
            LastSyncedText.clear();
            bBufferDirty = false;
            return;
        }

        // Our own OnSave fires the directory watcher and routes back here.
        // If the disk content matches what we last synced, the file hasn't
        // actually changed under us — skip SetText so the user's cursor,
        // selection, scroll, and undo/redo stack are preserved.
        if (Body.size() == LastSyncedText.size()
            && std::memcmp(Body.data(), LastSyncedText.data(), Body.size()) == 0)
        {
            bBufferDirty = false;
            return;
        }

        const std::string_view View(Body.c_str(), Body.size());
        CodeEditor.SetText(View);
        LastSyncedText.assign(Body.c_str(), Body.size());
        bBufferDirty = false;
        RefreshBreakpointMarkers();
    }

    void FLuaEditorTool::ApplyEditorSettings()
    {
        CodeEditor.SetTabSize(std::max(1, std::min(8, EditorTabSize)));
        CodeEditor.SetLineSpacing(EditorLineSpacing);
        CodeEditor.SetShowWhitespacesEnabled(bEditorShowWhitespace);
        CodeEditor.SetShowLineNumbersEnabled(bEditorShowLineNumbers);
        CodeEditor.SetReadOnlyEnabled(bEditorReadOnly);
        CodeEditor.SetAutoIndentEnabled(bAutoIndent);
        CodeEditor.SetShowMatchingBrackets(bShowMatchingBrackets);
        CodeEditor.SetCompletePairedGlyphs(bCompletePairedGlyphs);
        CodeEditor.SetPalette(EditorPalette == EPalette::Dark
            ? TextEditor::GetDarkPalette()
            : TextEditor::GetLightPalette());
    }

    void FLuaEditorTool::ToggleBreakpoint(int Line)
    {
        const bool bWasSet = Breakpoints.count(Line) > 0;
        if (bWasSet)
        {
            Breakpoints.erase(Line);
        }
        else
        {
            Breakpoints.insert(Line);
        }

        // Mirror to the runtime debugger so the change takes effect on every
        // currently-loaded instance of this script — no save required.
        Lua::FLuaDebugger::Get().SetBreakpoint(
            FStringView(VirtualPath.c_str(), VirtualPath.size()),
            Line,
            !bWasSet);

        RefreshBreakpointMarkers();
    }

    // ---------------------------------------------------------- autocomplete ---

    void FLuaEditorTool::RebuildSymbolIndex()
    {
        AllSymbols.clear();
        TopLevelSymbols.clear();
        SymbolsByPath.clear();
        SymbolByPath.clear();
        TableNames.clear();

        Lua::FScriptingContext::Get().HarvestGlobalSymbols(AllSymbols);

        for (const Lua::FLuaSymbol& Symbol : AllSymbols)
        {
            const eastl::string Path(Symbol.Path.c_str(), Symbol.Path.size());

            // Path → symbol map serves both hover lookups (full path) and
            // a future jump-to-definition feature.
            SymbolByPath[Path] = Symbol;

            if (Symbol.Parent.empty())
            {
                TopLevelSymbols.push_back(Symbol);
            }
            else
            {
                const eastl::string Parent(Symbol.Parent.c_str(), Symbol.Parent.size());
                SymbolsByPath[Parent].push_back(Symbol);
            }

            if (Symbol.Kind == Lua::ELuaSymbolKind::Table)
            {
                TableNames.insert(Path);
            }
        }
    }

    void FLuaEditorTool::OnHoverIdentifier(const std::string& Word, const std::string& DottedPath)
    {
        // Try the full dotted path first ("Engine.VFS.ReadFile") so a hover
        // on a method shows method info rather than a top-level "ReadFile"
        // collision. Fall back to the bare word for unqualified globals
        // (locals like "self", or top-level functions).
        const eastl::string FullKey(DottedPath.c_str(), DottedPath.size());
        auto Itr = SymbolByPath.find(FullKey);
        if (Itr == SymbolByPath.end() && Word != DottedPath)
        {
            const eastl::string WordKey(Word.c_str(), Word.size());
            Itr = SymbolByPath.find(WordKey);
        }
        if (Itr == SymbolByPath.end())
        {
            return;
        }

        const Lua::FLuaSymbol& Symbol = Itr->second;

        ImGui::BeginTooltip();
        // Header: kind badge + dotted path in a contrasting color, picked
        // to match the suggestion popup's badge color so the visual link
        // between hover and autocomplete is obvious.
        ImVec4 HeaderColor;
        const char* KindLabel = "";
        switch (Symbol.Kind)
        {
        case Lua::ELuaSymbolKind::Function: HeaderColor = ImVec4(0.86f, 0.71f, 0.35f, 1.0f); KindLabel = "function"; break;
        case Lua::ELuaSymbolKind::Table:    HeaderColor = ImVec4(0.35f, 0.78f, 0.86f, 1.0f); KindLabel = "table";    break;
        default:                             HeaderColor = ImVec4(0.63f, 0.78f, 0.55f, 1.0f); KindLabel = Symbol.TypeName.c_str(); break;
        }

        ImGui::TextColored(HeaderColor, "(%s) %s", KindLabel, Symbol.Path.c_str());
        ImGui::Separator();

        if (Symbol.Kind == Lua::ELuaSymbolKind::Function)
        {
            // Build "name(arg1, arg2, ...)". Curated symbols supply real
            // parameter names; bare Lua functions fall back to "argN"; opaque
            // C functions render as "(...)".
            std::string Sig;
            Sig.reserve(64);
            Sig.assign(Symbol.Name.c_str(), Symbol.Name.size());
            Sig.append("(");
            if (Symbol.bIsCFunction && Symbol.ParamNames.empty())
            {
                Sig.append("...");
            }
            else if (!Symbol.ParamNames.empty())
            {
                for (size_t I = 0; I < Symbol.ParamNames.size(); ++I)
                {
                    if (I > 0) Sig.append(", ");
                    Sig.append(Symbol.ParamNames[I].c_str(), Symbol.ParamNames[I].size());
                }
                if (Symbol.bIsVararg)
                {
                    if (!Symbol.ParamNames.empty()) Sig.append(", ");
                    Sig.append("...");
                }
            }
            else
            {
                for (uint8 I = 0; I < Symbol.ParamCount; ++I)
                {
                    if (I > 0) Sig.append(", ");
                    char ArgBuf[16];
                    std::snprintf(ArgBuf, sizeof(ArgBuf), "arg%u", unsigned(I + 1));
                    Sig.append(ArgBuf);
                }
                if (Symbol.bIsVararg)
                {
                    if (Symbol.ParamCount > 0) Sig.append(", ");
                    Sig.append("...");
                }
            }
            Sig.append(")");
            ImGui::TextUnformatted(Sig.c_str());

            if (!Symbol.Description.empty())
            {
                ImGui::Spacing();
                ImGui::PushTextWrapPos(420.0f);
                ImGui::TextUnformatted(Symbol.Description.c_str());
                ImGui::PopTextWrapPos();
            }
            else if (Symbol.bIsCFunction)
            {
                ImGui::Spacing();
                ImGui::TextDisabled("Engine-bound C function (parameter names unavailable).");
            }
        }
        else if (!Symbol.ValuePreview.empty())
        {
            ImGui::Text("= %s", Symbol.ValuePreview.c_str());
            if (!Symbol.Description.empty())
            {
                ImGui::Spacing();
                ImGui::PushTextWrapPos(420.0f);
                ImGui::TextUnformatted(Symbol.Description.c_str());
                ImGui::PopTextWrapPos();
            }
        }
        else
        {
            ImGui::TextDisabled("type: %s", Symbol.TypeName.c_str());
            if (!Symbol.Description.empty())
            {
                ImGui::Spacing();
                ImGui::PushTextWrapPos(420.0f);
                ImGui::TextUnformatted(Symbol.Description.c_str());
                ImGui::PopTextWrapPos();
            }
        }

        ImGui::EndTooltip();
    }

    namespace
    {
        // suggestions, suggestionKinds, and suggestionDetails are parallel
        // arrays — sort the suggestion strings via an index permutation so
        // the kinds and details follow the same order. std::sort on each
        // independently would shuffle them out of sync.
        void CoSortSuggestions(TextEditor::AutoCompleteState& State)
        {
            const size_t N = State.suggestions.size();
            if (N <= 1) return;

            std::vector<size_t> Perm(N);
            for (size_t I = 0; I < N; ++I) Perm[I] = I;

            std::sort(Perm.begin(), Perm.end(), [&](size_t A, size_t B)
            {
                return State.suggestions[A] < State.suggestions[B];
            });

            std::vector<std::string> Names(N);
            std::vector<char>        Kinds;
            std::vector<std::string> Details;
            const bool bHasKinds   = State.suggestionKinds.size()   == N;
            const bool bHasDetails = State.suggestionDetails.size() == N;
            if (bHasKinds)   Kinds.resize(N);
            if (bHasDetails) Details.resize(N);

            for (size_t I = 0; I < N; ++I)
            {
                Names[I] = std::move(State.suggestions[Perm[I]]);
                if (bHasKinds)   Kinds[I]   = State.suggestionKinds[Perm[I]];
                if (bHasDetails) Details[I] = std::move(State.suggestionDetails[Perm[I]]);
            }

            State.suggestions       = std::move(Names);
            if (bHasKinds)   State.suggestionKinds   = std::move(Kinds);
            if (bHasDetails) State.suggestionDetails = std::move(Details);
        }

        // One-glyph kind tag for the suggestion popup's left badge.
        char KindBadge(Lua::ELuaSymbolKind Kind)
        {
            switch (Kind)
            {
                case Lua::ELuaSymbolKind::Function: return 'f';
                case Lua::ELuaSymbolKind::Table:    return 't';
                case Lua::ELuaSymbolKind::Value:    return 'v';
                default:                            return '?';
            }
        }

        // Right-aligned dim text shown after the suggestion name. Three
        // shapes depending on the symbol kind:
        //   - Function: "function(arg1, arg2, ...)" — Luau gives us the
        //     parameter count + vararg flag for Lua functions; C functions
        //     come back as "(...)" because Luau can't introspect their
        //     C++ signature.
        //   - Primitive: "type = value" (e.g. "number = 100").
        //   - Other:     just the type ("table", "userdata", ...).
        std::string BuildDetail(const Lua::FLuaSymbol& Symbol)
        {
            if (Symbol.Kind == Lua::ELuaSymbolKind::Function)
            {
                std::string Out;
                Out.reserve(48);
                Out.assign("function(");
                if (!Symbol.ParamNames.empty())
                {
                    // Curated docs: real parameter names ("x", "y", ...).
                    for (size_t I = 0; I < Symbol.ParamNames.size(); ++I)
                    {
                        if (I > 0) Out.append(", ");
                        Out.append(Symbol.ParamNames[I].c_str(), Symbol.ParamNames[I].size());
                    }
                    if (Symbol.bIsVararg)
                    {
                        if (!Symbol.ParamNames.empty()) Out.append(", ");
                        Out.append("...");
                    }
                }
                else if (Symbol.bIsCFunction)
                {
                    // Luau can't introspect a C function's args. Render an
                    // unknown-arity placeholder so users at least know it
                    // accepts arguments.
                    Out.append("...");
                }
                else
                {
                    for (uint8 I = 0; I < Symbol.ParamCount; ++I)
                    {
                        if (I > 0) Out.append(", ");
                        char Buf[16];
                        std::snprintf(Buf, sizeof(Buf), "arg%u", unsigned(I + 1));
                        Out.append(Buf);
                    }
                    if (Symbol.bIsVararg)
                    {
                        if (Symbol.ParamCount > 0) Out.append(", ");
                        Out.append("...");
                    }
                }
                Out.append(")");
                return Out;
            }

            if (!Symbol.ValuePreview.empty())
            {
                std::string Out;
                Out.reserve(Symbol.TypeName.size() + Symbol.ValuePreview.size() + 4);
                Out.assign(Symbol.TypeName.c_str(), Symbol.TypeName.size());
                Out.append(" = ");
                Out.append(Symbol.ValuePreview.c_str(), Symbol.ValuePreview.size());
                return Out;
            }
            return std::string(Symbol.TypeName.c_str(), Symbol.TypeName.size());
        }
    }

    void FLuaEditorTool::OnAutoCompleteRequest(TextEditor::AutoCompleteState& State)
    {
        State.suggestions.clear();
        State.suggestionKinds.clear();
        State.suggestionDetails.clear();

        // Use the raw character index, NOT the visible column. column is
        // tab-expanded; index is the codepoint position into the line text.
        // Mixing the two silently misaligns the dot/colon detection on any
        // line with leading tabs.
        const std::string Line = CodeEditor.GetLineText(static_cast<int>(State.line));
        const eastl::string OwnerPath = ResolveOwnerPath(Line, static_cast<int>(State.searchTermStartIndex));

        const std::string& Term = State.searchTerm;
        const eastl::string TermLower = [&]
        {
            eastl::string Out;
            Out.reserve(Term.size());
            for (char C : Term) Out.push_back((char)std::tolower((unsigned char)C));
            return Out;
        }();

        auto MatchesPrefix = [&](FStringView Candidate)
        {
            if (TermLower.empty()) return true;
            if (Candidate.size() < TermLower.size()) return false;
            for (size_t I = 0; I < TermLower.size(); ++I)
            {
                if ((char)std::tolower((unsigned char)Candidate[I]) != TermLower[I])
                {
                    return false;
                }
            }
            return true;
        };

        // Helper: push a suggestion + its parallel kind/detail. Sorting later
        // is a co-permute on indices so the parallel arrays stay aligned.
        auto PushSymbol = [&](const Lua::FLuaSymbol& Symbol)
        {
            State.suggestions.emplace_back(Symbol.Name.c_str(), Symbol.Name.size());
            State.suggestionKinds.push_back(KindBadge(Symbol.Kind));
            State.suggestionDetails.emplace_back(BuildDetail(Symbol));
        };

        auto PushKeyword = [&](const char* Word)
        {
            State.suggestions.emplace_back(Word);
            State.suggestionKinds.push_back('k');
            State.suggestionDetails.emplace_back("keyword");
        };

        auto PushBufferIdent = [&](const std::string& Identifier)
        {
            State.suggestions.push_back(Identifier);
            State.suggestionKinds.push_back('i');
            State.suggestionDetails.emplace_back("identifier");
        };

        // Member-access path: "Engine.VFS." or "Engine.VFS:". Only suggest
        // children of the resolved owner; nothing else makes sense in context.
        if (!OwnerPath.empty())
        {
            auto Itr = SymbolsByPath.find(OwnerPath);
            if (Itr != SymbolsByPath.end())
            {
                for (const Lua::FLuaSymbol& Symbol : Itr->second)
                {
                    if (MatchesPrefix(FStringView(Symbol.Name.c_str(), Symbol.Name.size())))
                    {
                        PushSymbol(Symbol);
                    }
                }
            }
            // Co-sort the three parallel arrays by suggestion name.
            CoSortSuggestions(State);
            return;
        }

        // Top-level: keywords + engine globals + identifiers in the buffer.
        eastl::hash_set<eastl::string> Seen;
        auto SeenInsert = [&](const char* Data, size_t Size) -> bool
        {
            const eastl::string Key(Data, Size);
            if (Seen.find(Key) != Seen.end()) return false;
            Seen.insert(Key);
            return true;
        };

        for (const char* Keyword : kLuauKeywords)
        {
            const size_t Len = std::strlen(Keyword);
            if (!MatchesPrefix(FStringView(Keyword, Len))) continue;
            if (!SeenInsert(Keyword, Len)) continue;
            PushKeyword(Keyword);
        }
        for (const Lua::FLuaSymbol& Top : TopLevelSymbols)
        {
            if (!MatchesPrefix(FStringView(Top.Name.c_str(), Top.Name.size()))) continue;
            if (!SeenInsert(Top.Name.c_str(), Top.Name.size())) continue;
            PushSymbol(Top);
        }

        // Identifiers harvested from the current buffer — picks up locals,
        // function names, etc. that aren't in the engine global table.
        CodeEditor.IterateIdentifiers([&](const std::string& Identifier)
        {
            // Don't shadow-suggest the word the user is currently typing.
            if (Identifier == Term) return;
            if (!MatchesPrefix(FStringView(Identifier.c_str(), Identifier.size()))) return;
            if (!SeenInsert(Identifier.c_str(), Identifier.size())) return;
            PushBufferIdent(Identifier);
        });

        CoSortSuggestions(State);
    }

    void FLuaEditorTool::RefreshBreakpointMarkers()
    {
        // TextEditor::AddMarker is additive; clear-and-readd to keep state in
        // sync with our breakpoint set AND the live PC if the debugger is
        // paused at this file.
        CodeEditor.ClearMarkers();
        const ImU32 Red         = IM_COL32(220, 60, 60, 255);
        const ImU32 Translucent = IM_COL32(220, 60, 60, 40);
        for (int Line : Breakpoints)
        {
            CodeEditor.AddMarker(Line, Red, Translucent, "Breakpoint", "Breakpoint - paused on hit");
        }

        // Yellow PC arrow on the line we're paused at — only if the
        // debugger's source matches this editor's virtual path. A breakpoint
        // marker on the same line gets overwritten visually by the PC color
        // because addMarker is additive but draws last-writer-wins per line.
        if (IsDebuggerPausedHere())
        {
            const int PCLine = Lua::FLuaDebugger::Get().GetPausedLineZeroBased();
            if (PCLine >= 0)
            {
                const ImU32 Yellow      = IM_COL32(255, 200, 50, 255);
                const ImU32 YellowFill  = IM_COL32(255, 200, 50, 60);
                CodeEditor.AddMarker(PCLine, Yellow, YellowFill, "Paused here", "Debugger paused at this line");
                PCMarkerLine = PCLine;
            }
        }
        else
        {
            PCMarkerLine = -1;
        }
    }

    bool FLuaEditorTool::IsDebuggerPausedHere() const
    {
        Lua::FLuaDebugger& Debugger = Lua::FLuaDebugger::Get();
        if (!Debugger.IsPaused())
        {
            return false;
        }
        return Debugger.GetPausedSource() == FStringView(VirtualPath.c_str(), VirtualPath.size());
    }

    // ------------------------------------------------------------- toolbar ---

    void FLuaEditorTool::DrawToolbar()
    {
        ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Large);
        ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "%s", VirtualPath.c_str());
        ImGuiX::Font::PopFont();

        if (bBufferDirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "  *unsaved");
        }

        ImGui::Spacing();

        if (ImGui::Button(LE_ICON_CONTENT_SAVE " Save"))
        {
            OnSave();
        }
        ImGuiX::TextTooltip("Write the buffer to disk (Ctrl+S).");

        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_REFRESH " Reload from Disk"))
        {
            LoadFromDisk();
        }
        ImGuiX::TextTooltip("Discard buffer changes and reload from disk.");

        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_MAGNIFY " Find/Replace"))
        {
            CodeEditor.OpenFindReplaceWindow();
        }
        ImGuiX::TextTooltip("Open the find/replace bar (Ctrl+F).");

        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_REFRESH " Symbols"))
        {
            RebuildSymbolIndex();
            ImGuiX::Notifications::NotifySuccess("Re-harvested {0} Lua symbols.", (int)AllSymbols.size());
        }
        ImGuiX::TextTooltip("Re-walk the live Lua VM to refresh autocomplete suggestions.\nUse after registering new globals from C++.");

        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_BUG " Breakpoints"))
        {
            ImGui::OpenPopup("##lua_breakpoints");
        }
        if (ImGui::BeginPopup("##lua_breakpoints"))
        {
            if (Breakpoints.empty())
            {
                ImGui::TextDisabled("No breakpoints set.");
                ImGui::TextDisabled("Right-click a line number to add one.");
            }
            else
            {
                for (auto Itr = Breakpoints.begin(); Itr != Breakpoints.end(); )
                {
                    const int Line = *Itr;
                    ImGui::PushID(Line);
                    bool bRemove = false;
                    if (ImGui::SmallButton("X")) bRemove = true;
                    ImGui::SameLine();
                    char LineLabel[32];
                    std::snprintf(LineLabel, sizeof(LineLabel), "Line %d", Line + 1);
                    if (ImGui::Selectable(LineLabel))
                    {
                        CodeEditor.SetCursor(Line, 0);
                        CodeEditor.ScrollToLine(Line, TextEditor::Scroll::alignMiddle);
                    }
                    ImGui::PopID();

                    if (bRemove)
                    {
                        Lua::FLuaDebugger::Get().SetBreakpoint(
                            FStringView(VirtualPath.c_str(), VirtualPath.size()),
                            Line, false);
                        Itr = Breakpoints.erase(Itr);
                    }
                    else
                    {
                        ++Itr;
                    }
                }

                if (ImGui::Button("Clear all", ImVec2(-1, 0)))
                {
                    Lua::FLuaDebugger::Get().ClearBreakpointsFor(FStringView(VirtualPath.c_str(), VirtualPath.size()));
                    Breakpoints.clear();
                }
            }

            // Always refresh markers; cheap and stays in sync with edits above.
            RefreshBreakpointMarkers();
            ImGui::EndPopup();
        }
        ImGuiX::TextTooltip("Manage breakpoints. Hit a breakpoint to open the debugger panel.");

        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_COG " Settings"))
        {
            ImGui::OpenPopup("##lua_editor_settings");
        }
        DrawSettingsPopup();
    }

    void FLuaEditorTool::DrawSettingsPopup()
    {
        if (!ImGui::BeginPopup("##lua_editor_settings"))
        {
            return;
        }

        bool bDirty = false;

        ImGui::TextDisabled("Display");
        ImGui::Separator();

        ImGui::SliderFloat("Font scale", &EditorFontScale, 0.75f, 3.0f, "%.2fx");

        if (ImGui::SliderFloat("Line spacing", &EditorLineSpacing, 1.0f, 2.0f, "%.2f")) bDirty = true;
        if (ImGui::SliderInt("Tab size", &EditorTabSize, 1, 8)) bDirty = true;
        if (ImGui::Checkbox("Show line numbers", &bEditorShowLineNumbers)) bDirty = true;
        if (ImGui::Checkbox("Show whitespace",   &bEditorShowWhitespace))  bDirty = true;
        if (ImGui::Checkbox("Read-only",         &bEditorReadOnly))        bDirty = true;

        ImGui::Spacing();
        ImGui::TextDisabled("Editing");
        ImGui::Separator();
        if (ImGui::Checkbox("Auto-indent",        &bAutoIndent))           bDirty = true;
        if (ImGui::Checkbox("Match brackets",     &bShowMatchingBrackets)) bDirty = true;
        if (ImGui::Checkbox("Auto-close pairs",   &bCompletePairedGlyphs)) bDirty = true;

        ImGui::Spacing();
        ImGui::TextDisabled("Theme");
        ImGui::Separator();
        int PaletteIdx = (int)EditorPalette;
        const char* PaletteLabels[] = { "Dark", "Light" };
        if (ImGui::Combo("Palette", &PaletteIdx, PaletteLabels, IM_ARRAYSIZE(PaletteLabels)))
        {
            EditorPalette = (EPalette)PaletteIdx;
            bDirty = true;
        }

        ImGui::Spacing();
        if (ImGui::Button("Persist as default", ImVec2(-1, 0)))
        {
            GConfig->Set<float>(kKeyFontScale,       EditorFontScale);
            GConfig->Set<int32>(kKeyTabSize,         EditorTabSize);
            GConfig->Set<float>(kKeyLineSpacing,     EditorLineSpacing);
            GConfig->Set<bool>(kKeyShowWhitespace,   bEditorShowWhitespace);
            GConfig->Set<bool>(kKeyShowLineNumbers,  bEditorShowLineNumbers);
            GConfig->Set<bool>(kKeyAutoIndent,       bAutoIndent);
            GConfig->Set<bool>(kKeyMatchBrackets,    bShowMatchingBrackets);
            GConfig->Set<bool>(kKeyCompletePairs,    bCompletePairedGlyphs);
            GConfig->Set<std::string>(kKeyPalette,   std::string(EditorPalette == EPalette::Dark ? "Dark" : "Light"));
            ImGuiX::Notifications::NotifySuccess("Lua editor settings saved.");
        }

        if (bDirty)
        {
            ApplyEditorSettings();
        }

        ImGui::EndPopup();
    }

    void FLuaEditorTool::DrawDebuggerPanel()
    {
        Lua::FLuaDebugger& Debugger = Lua::FLuaDebugger::Get();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(28, 28, 36, 230));
        if (!ImGui::BeginChild("##lua_inline_debugger", ImVec2(0, -ImGui::GetTextLineHeightWithSpacing() - ImGui::GetStyle().ItemSpacing.y),
            ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding))
        {
            ImGui::EndChild();
            ImGui::PopStyleColor();
            return;
        }

        // ---------- toolbar ----------
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), LE_ICON_PAUSE_CIRCLE " Paused at line %d", Debugger.GetPausedLineZeroBased() + 1);
        ImGui::SameLine(0, 16);

        if (ImGui::Button(LE_ICON_PLAY " Continue"))            Debugger.RequestContinue();
        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_DEBUG_STEP_OVER " Over"))     Debugger.RequestStepOver();
        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_DEBUG_STEP_INTO " Into"))     Debugger.RequestStepInto();
        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_DEBUG_STEP_OUT " Out"))       Debugger.RequestStepOut();
        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_STOP " Stop"))                Debugger.RequestStop();

        // Keyboard shortcuts only fire when this editor is the focused tool —
        // ImGui::IsWindowFocused gates them so they don't steal F5 etc. from
        // other panels (game preview, console, etc.).
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        {
            if (ImGui::IsKeyPressed(ImGuiKey_F5,  false)) Debugger.RequestContinue();
            if (ImGui::IsKeyPressed(ImGuiKey_F10, false)) Debugger.RequestStepOver();
            if (ImGui::IsKeyPressed(ImGuiKey_F11, false))
            {
                if (ImGui::GetIO().KeyShift) Debugger.RequestStepOut();
                else                          Debugger.RequestStepInto();
            }
        }

        ImGui::Separator();

        // ---------- split: call stack | locals/upvalues ----------
        const ImVec2 Avail = ImGui::GetContentRegionAvail();
        const float SplitW = (Avail.x - ImGui::GetStyle().ItemSpacing.x) * 0.4f;

        const TVector<Lua::FStackFrame>& Stack = Debugger.GetCallStack();
        if (DebuggerSelectedFrame >= (int)Stack.size())
        {
            DebuggerSelectedFrame = 0;
        }

        if (ImGui::BeginChild("##lua_inline_callstack", ImVec2(SplitW, Avail.y), ImGuiChildFlags_Borders))
        {
            ImGui::TextDisabled("Call Stack");
            ImGui::Separator();
            if (Stack.empty())
            {
                ImGui::TextDisabled("(empty)");
            }
            for (int I = 0; I < (int)Stack.size(); ++I)
            {
                const Lua::FStackFrame& Frame = Stack[I];

                // Show only the file basename in the inline panel — full
                // virtual paths chew up width fast in the narrow split.
                FStringView FullSrc(Frame.Source.c_str(), Frame.Source.size());
                FStringView Basename = VFS::FileName(FullSrc);

                char Label[256];
                std::snprintf(Label, sizeof(Label), "[%d] %s @ %.*s:%d##frm%d",
                    I,
                    Frame.FunctionName.empty() ? "?" : Frame.FunctionName.c_str(),
                    int(Basename.size()), Basename.data(),
                    Frame.Line + 1,
                    I);
                if (ImGui::Selectable(Label, DebuggerSelectedFrame == I))
                {
                    DebuggerSelectedFrame = I;
                }
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        if (ImGui::BeginChild("##lua_inline_locals", ImVec2(0, Avail.y), ImGuiChildFlags_Borders))
        {
            ImGui::TextDisabled("Locals / Upvalues");
            ImGui::Separator();

            if (DebuggerSelectedFrame >= 0 && DebuggerSelectedFrame < (int)Stack.size())
            {
                const Lua::FStackFrame& Frame = Stack[DebuggerSelectedFrame];

                auto DrawTable = [&](const TVector<Lua::FStackVariable>& Vars, const char* Header)
                {
                    if (Vars.empty()) return;
                    ImGui::SeparatorText(Header);
                    if (ImGui::BeginTable(Header, 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
                    {
                        ImGui::TableSetupColumn("Name");
                        ImGui::TableSetupColumn("Value");
                        ImGui::TableSetupColumn("Type");
                        ImGui::TableHeadersRow();
                        for (const Lua::FStackVariable& V : Vars)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(V.Name.c_str());
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(V.Value.c_str());
                            ImGui::TableNextColumn();
                            ImGui::TextDisabled("%s", V.TypeName.c_str());
                        }
                        ImGui::EndTable();
                    }
                };

                DrawTable(Frame.Locals,   "Locals");
                DrawTable(Frame.Upvalues, "Upvalues");
            }
        }
        ImGui::EndChild();

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void FLuaEditorTool::DrawStatusBar()
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 20, 25, 200));
        if (ImGui::BeginChild("##lua_editor_status", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding))
        {
            const TextEditor::CursorPosition Pos = CodeEditor.GetCurrentCursorPosition();
            const int LineCount = CodeEditor.GetLineCount();
            const std::string Body = CodeEditor.GetText();
            const size_t Bytes = Body.size();

            ImGui::Text("Ln %d, Col %d", Pos.line + 1, Pos.column + 1);
            ImGui::SameLine(0, 20);
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "%d lines", LineCount);
            ImGui::SameLine(0, 20);
            if (Bytes >= 1024)
            {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "%.1f KB", float(Bytes) / 1024.0f);
            }
            else
            {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "%zu B", Bytes);
            }

            ImGui::SameLine(0, 20);
            ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f), "Luau");

            if (!Breakpoints.empty())
            {
                ImGui::SameLine(0, 20);
                ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), LE_ICON_BUG " %zu", Breakpoints.size());
            }

            if (bEditorReadOnly)
            {
                ImGui::SameLine(0, 20);
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f), "READ-ONLY");
            }

            if (bBufferDirty)
            {
                ImGui::SameLine(0, 20);
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "modified");
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // -------------------------------------------------------- file watcher ---

    void FLuaEditorTool::StartWatching()
    {
        if (ParentDir.empty())
        {
            return;
        }

        FFixedString DiskParentDir;
        const FStringView TargetVirtual(VirtualPath.c_str(), VirtualPath.size());
        VFS::DirectoryIterator(FStringView(ParentDir.c_str(), ParentDir.size()),
            [&](const VFS::FFileInfo& Info)
            {
                if (!DiskParentDir.empty())
                {
                    return;
                }
                if (FStringView(Info.VirtualPath.c_str(), Info.VirtualPath.size()) != TargetVirtual)
                {
                    return;
                }
                FStringView Source(Info.PathSource.c_str(), Info.PathSource.size());
                const size_t Slash = Source.find_last_of('/');
                const size_t BackSlash = Source.find_last_of('\\');
                size_t Cut = FString::npos;
                if (Slash != FStringView::npos)
                {
                    Cut = Slash;
                }
                if (BackSlash != FStringView::npos && (Cut == FStringView::npos || BackSlash > Cut))
                {
                    Cut = BackSlash;
                }
                if (Cut == FStringView::npos)
                {
                    return;
                }
                DiskParentDir.assign(Source.data(), Cut);
            });

        if (DiskParentDir.empty())
        {
            return;
        }

        const FStringView FileNameView = VFS::FileName(TargetVirtual);
        const FString FileName(FileNameView.data(), FileNameView.size());

        FileWatcher.Watch(DiskParentDir, [this, FileName](const FFileEvent& Event)
        {
            if (Event.Action != EFileAction::Modified && Event.Action != EFileAction::Added)
            {
                return;
            }

            FStringView EventPath(Event.Path.c_str(), Event.Path.size());
            if (EventPath.size() < FileName.size())
            {
                return;
            }
            const FStringView Tail = EventPath.substr(EventPath.size() - FileName.size());
            if (Tail != FStringView(FileName.c_str(), FileName.size()))
            {
                return;
            }

            bExternalChangePending.store(true, Atomic::MemoryOrderRelease);
        }, false);
    }
}
