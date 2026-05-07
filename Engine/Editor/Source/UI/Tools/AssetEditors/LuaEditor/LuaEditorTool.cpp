#include "LuaEditorTool.h"

#include "../../EditorToolContext.h"
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

        // Luau reserved words; surfaced first in suggestions when they match the prefix.
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
        
        FString ResolveOwnerPath(const std::string& Line, int EndCol)
        {
            int I = std::min(EndCol, (int)Line.size());
            if (I <= 0)
            {
                return {};
            }

            // Must be immediately preceded by '.' or ':'.
            const char Sep = Line[I - 1];
            if (Sep != '.' && Sep != ':')
            {
                return {};
            }

            // Walk back collecting '.'-separated segments; ':' only valid as the closest-to-cursor separator.
            TVector<FString> SegmentsRev;
            int Cursor = I - 1;
            while (Cursor > 0)
            {
                const char Here = Line[Cursor];
                if (Here != '.' && Here != ':')
                {
                    break;
                }

                int End = Cursor;
                int Start = End;
                while (Start > 0 && IsIdentChar(Line[Start - 1]))
                {
                    --Start;
                }
                if (Start == End)
                {
                    return {};
                }
                SegmentsRev.emplace_back(Line.data() + Start, Line.data() + End);

                if (Here == ':' && SegmentsRev.size() != 1)
                {
                    return {};
                }
                Cursor = Start - 1;
            }

            if (SegmentsRev.empty())
            {
                return {};
            }

            FString Path;
            for (auto Itr = SegmentsRev.rbegin(); Itr != SegmentsRev.rend(); ++Itr)
            {
                if (!Path.empty())
                {
                    Path += '.';
                }
                Path += *Itr;
            }
            return Path;
        }
        
        const TextEditor::Language* GetLuauLanguage()
        {
            static bool Initialized = false;
            static TextEditor::Language Lang;
            if (Initialized)
            {
                return &Lang;
            }

            // Bundled Lua tokenizer/strings, with Luau overlaid on top.
            const TextEditor::Language* Base = TextEditor::Language::Lua();
            Lang = *Base;
            Lang.name = "Luau";

            // Backtick interpolated strings.
            Lang.otherStringAltStart = "`";
            Lang.otherStringAltEnd   = "`";

            static const char* const LuauOnlyKeywords[] = {
                "continue", "export", "type", "typeof", "self",
            };
            for (const char* K : LuauOnlyKeywords) Lang.keywords.insert(K);

            // Built-in types colored as identifiers so `local x: number` lights up.
            static const char* const TypeNames[] = {
                "any", "unknown", "never", "nil", "boolean", "number", "string",
                "thread", "userdata", "table", "function", "vector", "buffer",
            };
            for (const char* T : TypeNames) Lang.identifiers.insert(T);

            // Lua/Luau stdlib + engine globals; colored even before live-VM harvest.
            static const char* const Stdlib[] = {
                "_G", "_ENV", "_VERSION",
                "assert", "collectgarbage", "error", "getmetatable", "ipairs",
                "next", "pairs", "pcall", "xpcall", "rawequal", "rawget",
                "rawlen", "rawset", "select", "setmetatable", "tonumber",
                "tostring", "type", "print", "require", "unpack",
                "bit32", "buffer", "coroutine", "debug", "io", "math", "os",
                "string", "table", "utf8",
                // engine surfaces (defensively; may also be auto-discovered by VM harvest)
                "Engine", "Events", "World", "Entity", "Time",
            };
            for (const char* S : Stdlib) Lang.identifiers.insert(S);

            Initialized = true;
            return &Lang;
        }

        // Tooltip with low-alpha bg so code under the cursor stays visible.
        void BeginTranslucentTooltip()
        {
            ImVec4 Bg = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
            Bg.w *= 0.82f;
            ImGui::PushStyleColor(ImGuiCol_PopupBg, Bg);
            ImGui::BeginTooltip();
        }
        void EndTranslucentTooltip()
        {
            ImGui::EndTooltip();
            ImGui::PopStyleColor();
        }

        // Detect string literal at byte Col in Line. Triple-bracket [[...]] not handled.
        struct FStringHit
        {
            int  StartCol = -1;     // column of the opening quote
            int  EndCol   = -1;     // column AFTER the closing quote, or end-of-line
            char Quote    = '\0';   // " ' or `
            bool bClosed  = false;
        };
        bool FindStringAt(const std::string& Line, int Col, FStringHit& Out)
        {
            const int N = (int)Line.size();
            int I = 0;
            while (I < N)
            {
                const char C = Line[I];
                if (C == '"' || C == '\'' || C == '`')
                {
                    const char Quote = C;
                    const int Start = I;
                    int J = I + 1;
                    bool bClosed = false;
                    while (J < N)
                    {
                        const char D = Line[J];
                        if (D == '\\' && J + 1 < N) { J += 2; continue; }
                        if (D == Quote) { bClosed = true; ++J; break; }
                        ++J;
                    }
                    const int End = J;
                    if (Col >= Start && Col < End)
                    {
                        Out.StartCol = Start;
                        Out.EndCol   = End;
                        Out.Quote    = Quote;
                        Out.bClosed  = bClosed;
                        return true;
                    }
                    I = End;
                    continue;
                }
                // skip line comments
                if (C == '-' && I + 1 < N && Line[I + 1] == '-')
                {
                    return false;
                }
                ++I;
            }
            return false;
        }

        // Detect number literal at column; handles hex/binary/decimal/scientific.
        bool FindNumberAt(const std::string& Line, int Col, int& OutStart, int& OutEnd)
        {
            auto IsDigitOrSep = [](char C) { return (C >= '0' && C <= '9') || C == '_'; };
            const int N = (int)Line.size();
            if (Col < 0 || Col >= N)
            {
                return false;
            }
            // walk left to a digit or 0x/0b prefix
            int Start = Col;
            while (Start > 0)
            {
                const char P = Line[Start - 1];
                if (IsDigitOrSep(P) || P == '.' || P == 'x' || P == 'X' || P == 'b' || P == 'B'
                    || P == 'a' || P == 'A' || P == 'c' || P == 'C' || P == 'd' || P == 'D'
                    || P == 'e' || P == 'E' || P == 'f' || P == 'F' || P == '+' || P == '-' || P == 'p' || P == 'P')
                {
                    --Start;
                }
                else break;
            }
            if (Start >= N)
            {
                return false;
            }
            const char S = Line[Start];
            if (!(S >= '0' && S <= '9'))
            {
                return false;
            }
            int End = Start;
            bool SawDigit = false;
            while (End < N)
            {
                const char C = Line[End];
                if ((C >= '0' && C <= '9') || C == '_') { SawDigit = true; ++End; continue; }
                if (C == '.' || C == 'x' || C == 'X' || C == 'b' || C == 'B'
                    || C == 'a' || C == 'A' || C == 'c' || C == 'C' || C == 'd' || C == 'D'
                    || C == 'e' || C == 'E' || C == 'f' || C == 'F' || C == 'p' || C == 'P'
                    || C == '+' || C == '-')
                {
                    // accept only as valid suffixes; previous must be digit or this is '.','+','-'.
                    ++End;
                    continue;
                }
                break;
            }
            if (!SawDigit || Col >= End) return false;
            OutStart = Start;
            OutEnd   = End;
            return true;
        }

        const char* kKeyUsePlatformEditor = "Editor.LuaEditor.UsePlatformEditor";
        const char* kKeyFontScale         = "Editor.LuaEditor.FontScale";
        const char* kKeyTabSize           = "Editor.LuaEditor.TabSize";
        const char* kKeyLineSpacing       = "Editor.LuaEditor.LineSpacing";
        const char* kKeyShowWhitespace    = "Editor.LuaEditor.ShowWhitespace";
        const char* kKeyShowLineNumbers   = "Editor.LuaEditor.ShowLineNumbers";
        const char* kKeyShowMiniMap       = "Editor.LuaEditor.ShowMiniMap";
        const char* kKeyAutoIndent        = "Editor.LuaEditor.AutoIndent";
        const char* kKeyMatchBrackets     = "Editor.LuaEditor.MatchBrackets";
        const char* kKeyCompletePairs     = "Editor.LuaEditor.CompletePairs";
        const char* kKeyPalette           = "Editor.LuaEditor.Palette";
        const char* kKeyInsertSpaces      = "Editor.LuaEditor.InsertSpacesOnTabs";
        const char* kKeyTrimOnSave        = "Editor.LuaEditor.TrimTrailingOnSave";
        const char* kKeyAutoTrigger       = "Editor.LuaEditor.AutoTriggerCompletion";
        const char* kKeyTriggerDelayMs    = "Editor.LuaEditor.AutoTriggerDelayMs";

        struct FLuaSnippet
        {
            const char* Label;
            const char* Body;
        };

        const FLuaSnippet kLuaSnippets[] =
        {
            { "function (named)",       "function name()\n\t\nend\n" },
            { "local function",         "local function name()\n\t\nend\n" },
            { "anonymous function",     "function()\n\t\nend" },
            { "if / end",               "if condition then\n\t\nend\n" },
            { "if / else / end",        "if condition then\n\t\nelse\n\t\nend\n" },
            { "if / elseif / else",     "if condition then\n\t\nelseif other then\n\t\nelse\n\t\nend\n" },
            { "for i = 1, N",           "for i = 1, count do\n\t\nend\n" },
            { "for k, v in pairs",      "for k, v in pairs(t) do\n\t\nend\n" },
            { "for i, v in ipairs",     "for i, v in ipairs(t) do\n\t\nend\n" },
            { "while / end",            "while condition do\n\t\nend\n" },
            { "repeat / until",         "repeat\n\t\nuntil condition\n" },
            { "do / end (block)",       "do\n\t\nend\n" },
            { "local table",            "local t = {\n\t\n}\n" },
            { "module skeleton",        "local M = {}\n\nfunction M.foo()\n\t\nend\n\nreturn M\n" },
            { "ECS: OnUpdate",          "function OnUpdate(dt)\n\t\nend\n" },
            { "ECS: OnBeginPlay",       "function OnBeginPlay()\n\t\nend\n" },
            { "ECS: OnEndPlay",         "function OnEndPlay()\n\t\nend\n" },
            { "Events:Subscribe",       "Events:Subscribe(\"EventName\", function(payload)\n\t\nend)\n" },
            { "Events:Dispatch",        "Events:Dispatch(\"EventName\", { })\n" },
            { "pcall wrapper",          "local ok, err = pcall(function()\n\t\nend)\nif not ok then\n\tprint(err)\nend\n" },
        };
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
        bEditorShowMiniMap      = GConfig->GetBool(kKeyShowMiniMap);
        bAutoIndent             = GConfig->GetBool(kKeyAutoIndent);
        bShowMatchingBrackets   = GConfig->GetBool(kKeyMatchBrackets);
        bCompletePairedGlyphs   = GConfig->GetBool(kKeyCompletePairs);
        bInsertSpacesOnTabs     = GConfig->GetBool(kKeyInsertSpaces);
        bTrimTrailingOnSave     = GConfig->GetBool(kKeyTrimOnSave);
        bAutoTriggerCompletion  = GConfig->GetBool(kKeyAutoTrigger);
        AutoTriggerDelayMs      = std::max(0, std::min(2000, GConfig->GetInt(kKeyTriggerDelayMs)));
        const FString PaletteStr = GConfig->GetString(kKeyPalette);
        EditorPalette = (PaletteStr == "Light") ? EPalette::Light : EPalette::Dark;
    }

    void FLuaEditorTool::OnInitialize()
    {
        FAssetEditorTool::OnInitialize();

        CodeEditor.SetLanguage(GetLuauLanguage());
        ApplyEditorSettings();
        LoadFromDisk();
        RebuildLocalIndex();

        // Subscribe to compile diagnostics. The scripting context fires these
        // for every compile attempt; we filter to ones that match this
        // editor's virtual path. ApplyCompileError / ClearCompileError repaint
        // the marker stripe via RefreshBreakpointMarkers so the visual stays
        // consistent with breakpoints / bookmarks / PC arrow.
        Lua::FScriptingContext& SC = Lua::FScriptingContext::Get();
        CompileErrorHandle = SC.OnScriptCompileError.AddLambda(
            [this](FStringView Path, const Lua::FCompileDiagnostic& Diag)
            {
                if (Path == FStringView(VirtualPath.c_str(), VirtualPath.size()))
                {
                    ApplyCompileError(Diag.Line, Diag.Message);
                }
            });
        CompileSuccessHandle = SC.OnScriptCompileSuccess.AddLambda(
            [this](FStringView Path)
            {
                if (Path == FStringView(VirtualPath.c_str(), VirtualPath.size()))
                {
                    ClearCompileError();
                }
            });

        // Re-hydrate breakpoints from the debugger (source of truth) so they survive editor reopen.
        for (int Line : Lua::FLuaDebugger::Get().GetBreakpointLines(FStringView(VirtualPath.c_str(), VirtualPath.size())))
        {
            Breakpoints.insert(Line);
        }
        RefreshBreakpointMarkers();

        CodeEditor.SetLineNumberContextMenuCallback([this](int Line)
        {
            const bool bHasBp = Breakpoints.count(Line) > 0;
            if (ImGui::MenuItem(bHasBp ? "Remove breakpoint" : "Add breakpoint"))
            {
                ToggleBreakpoint(Line);
            }

            ImGui::BeginDisabled(!bHasBp);
            if (ImGui::MenuItem("Configure breakpoint..."))
            {
                RequestedBreakpointSettingsLine = Line;
                BpConditionBuffer[0] = '\0';
                BpLogMessageBuffer[0] = '\0';
                BpIgnoreCount = 0;
                bBpEnabled = true;
                if (const Lua::FBreakpointSettings* S = Lua::FLuaDebugger::Get().GetBreakpointSettings(
                        FStringView(VirtualPath.c_str(), VirtualPath.size()), Line))
                {
                    std::snprintf(BpConditionBuffer, sizeof(BpConditionBuffer), "%.*s", int(S->Condition.size()), S->Condition.c_str());
                    std::snprintf(BpLogMessageBuffer, sizeof(BpLogMessageBuffer), "%.*s", int(S->LogMessage.size()), S->LogMessage.c_str());
                    BpIgnoreCount = (int)S->IgnoreCount;
                    bBpEnabled = S->bEnabled;
                }
            }
            ImGui::EndDisabled();

            ImGui::Separator();

            if (ImGui::MenuItem(Bookmarks.count(Line) ? "Remove bookmark" : "Add bookmark", "F2"))
            {
                ToggleBookmark(Line);
            }

            if (ImGui::MenuItem("Run to this line", "Ctrl+F10"))
            {
                Lua::FLuaDebugger& Debugger = Lua::FLuaDebugger::Get();
                Debugger.RunToLine(FStringView(VirtualPath.c_str(), VirtualPath.size()), Line);
                if (Debugger.IsPaused())
                {
                    Debugger.RequestContinue();
                }
                RefreshBreakpointMarkers();
            }

            ImGui::Separator();

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
            // Re-harvest locals so the next hover reflects recent edits.
            RebuildLocalIndex();
            RebuildDocumentOutline();
        }, /*delay ms*/ 100);

        RebuildDocumentOutline();

        StartWatching();

        RebuildSymbolIndex();
        AutoCompleteCfg.triggerOnTyping = bAutoTriggerCompletion;
        AutoCompleteCfg.triggerOnShortcut = true;
        AutoCompleteCfg.triggerInComments = false;
        AutoCompleteCfg.triggerInStrings = false;
        AutoCompleteCfg.triggerDelay = std::chrono::milliseconds{AutoTriggerDelayMs};
        AutoCompleteCfg.callback = [this](TextEditor::AutoCompleteState& State)
        {
            OnAutoCompleteRequest(State);
        };
        CodeEditor.SetAutoCompleteConfig(&AutoCompleteCfg);

        // Hover tooltip uses the same harvested symbol table as autocomplete.
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

            // Reserve space under the editor for the inline debugger panel when paused.
            const float DebuggerPanelHeight = bPausedHere ? std::min(Avail.y * 0.5f, 360.0f) : 0.0f;
            const float SpacingForPanel = bPausedHere ? ImGui::GetStyle().ItemSpacing.y : 0.0f;

            const float OutlineWidth = bShowOutline ? std::clamp(Avail.x * 0.25f, 220.0f, 360.0f) : 0.0f;
            const float OutlineSpacing = bShowOutline ? ImGui::GetStyle().ItemSpacing.x : 0.0f;
            const ImVec2 EditorSize(Avail.x - OutlineWidth - OutlineSpacing, std::max(32.0f, Avail.y - StatusBarHeight - DebuggerPanelHeight - SpacingForPanel));

            // Ctrl+wheel adjusts font scale; steal the wheel so TextEditor doesn't also scroll.
            const ImVec2 EditorMin = ImGui::GetCursorScreenPos();
            const ImVec2 EditorMax(EditorMin.x + EditorSize.x, EditorMin.y + EditorSize.y);
            ImGuiIO& Io = ImGui::GetIO();
            if (Io.KeyCtrl && Io.MouseWheel != 0.0f && ImGui::IsMouseHoveringRect(EditorMin, EditorMax))
            {
                EditorFontScale = std::clamp(EditorFontScale * (1.0f + Io.MouseWheel * 0.1f), 0.5f, 4.0f);
                Io.MouseWheel = 0.0f;
            }

            // Push JetBrainsMono; TextEditor assumes uniform glyph cells, so the proportional UI font drifts.
            ImGuiX::Font::PushFont(ImGuiX::Font::EFont::Mono);
            ImGui::PushFontSize(ImGui::GetStyle().FontSizeBase * EditorFontScale);
            CodeEditor.Render("##lua_text", EditorSize);

            // Free-form hover (strings / numbers) sits on top of the editor's
            // own identifier hover. Mono font is still pushed so column math
            // matches the just-rendered glyph cells.
            DrawFreeFormHoverTooltip();

            // Inline value annotations are drawn on top of the just-rendered
            // editor area. Mono font is still pushed so column-cell math
            // matches the glyph layout.
            if (bPausedHere && bShowInlineValuesWhilePaused)
            {
                DrawInlineValueOverlay();
            }

            ImGui::PopFontSize();
            ImGuiX::Font::PopFont();

            // Right-side outline panel sits next to the editor. Drawn after
            // the editor so it shares the same horizontal cursor-line.
            if (bShowOutline)
            {
                ImGui::SameLine();
                DrawOutlinePanel();
            }

            // Tool-level shortcuts that aren't handled by TextEditor itself.
            // Gated on focus so we don't steal Ctrl+G from sibling panels.
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            {
                HandleEditorShortcuts();
            }

            if (bPausedHere)
            {
                DrawDebuggerPanel();
            }

            DrawBreakpointSettingsPopup();

            DrawStatusBar();
        });
    }

    void FLuaEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
        FileWatcher.Stop();

        Lua::FScriptingContext& SC = Lua::FScriptingContext::Get();
        SC.OnScriptCompileError.Remove(CompileErrorHandle);
        SC.OnScriptCompileSuccess.Remove(CompileSuccessHandle);
    }

    void FLuaEditorTool::ApplyCompileError(int Line, const FString& Message)
    {
        bHasCompileError    = true;
        CompileErrorLine    = Line;
        CompileErrorMessage = Message;
        RefreshBreakpointMarkers();
        if (Line >= 1)
        {
            CodeEditor.ScrollToLine(Line - 1, TextEditor::Scroll::alignMiddle);
        }
        ImGuiX::Notifications::NotifyError("Compile error: {0}", Message.c_str());
    }

    void FLuaEditorTool::ClearCompileError()
    {
        if (!bHasCompileError) return;
        bHasCompileError    = false;
        CompileErrorLine    = -1;
        CompileErrorMessage.clear();
        RefreshBreakpointMarkers();
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
                // Mark watches dirty so the Watch panel re-evaluates against
                // the new pause state on its next draw.
                for (FWatchEntry& W : Watches) W.bDirty = true;
                RefreshWatchValues();
            }
        }
    }

    void FLuaEditorTool::OnSave()
    {
        if (bTrimTrailingOnSave)
        {
            CodeEditor.StripTrailingWhitespaces();
        }

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
        // actually changed under us; skip SetText so the user's cursor,
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
        RebuildLocalIndex();
    }

    void FLuaEditorTool::ApplyEditorSettings()
    {
        CodeEditor.SetTabSize(std::max(1, std::min(8, EditorTabSize)));
        CodeEditor.SetInsertSpacesOnTabs(bInsertSpacesOnTabs);
        CodeEditor.SetLineSpacing(EditorLineSpacing);
        CodeEditor.SetShowWhitespacesEnabled(bEditorShowWhitespace);
        CodeEditor.SetShowLineNumbersEnabled(bEditorShowLineNumbers);
        CodeEditor.SetShowScrollbarMiniMapEnabled(bEditorShowMiniMap);
        CodeEditor.SetReadOnlyEnabled(bEditorReadOnly);
        CodeEditor.SetAutoIndentEnabled(bAutoIndent);
        CodeEditor.SetShowMatchingBrackets(bShowMatchingBrackets);
        CodeEditor.SetCompletePairedGlyphs(bCompletePairedGlyphs);
        CodeEditor.SetPalette(EditorPalette == EPalette::Dark
            ? TextEditor::GetDarkPalette()
            : TextEditor::GetLightPalette());

        AutoCompleteCfg.triggerOnTyping = bAutoTriggerCompletion;
        AutoCompleteCfg.triggerDelay = std::chrono::milliseconds{AutoTriggerDelayMs};
    }

    void FLuaEditorTool::ToggleBookmark(int Line)
    {
        if (Bookmarks.count(Line))
        {
            Bookmarks.erase(Line);
        }
        else
        {
            Bookmarks.insert(Line);
        }
        RefreshBreakpointMarkers();
    }

    void FLuaEditorTool::NavigateBookmark(bool bForward)
    {
        if (Bookmarks.empty())
        {
            return;
        }
        eastl::vector<int> Sorted(Bookmarks.begin(), Bookmarks.end());
        std::sort(Sorted.begin(), Sorted.end());
        const int Cur = CodeEditor.GetCurrentCursorPosition().line;
        int Target = -1;
        if (bForward)
        {
            for (int L : Sorted) { if (L > Cur) { Target = L; break; } }
            if (Target < 0) Target = Sorted.front();
        }
        else
        {
            for (auto It = Sorted.rbegin(); It != Sorted.rend(); ++It) { if (*It < Cur) { Target = *It; break; } }
            if (Target < 0) Target = Sorted.back();
        }
        CodeEditor.SetCursor(Target, 0);
        CodeEditor.ScrollToLine(Target, TextEditor::Scroll::alignMiddle);
    }

    void FLuaEditorTool::RunToCursor()
    {
        const int Line = CodeEditor.GetCurrentCursorPosition().line;
        Lua::FLuaDebugger& Debugger = Lua::FLuaDebugger::Get();
        Debugger.RunToLine(FStringView(VirtualPath.c_str(), VirtualPath.size()), Line);
        if (Debugger.IsPaused())
        {
            Debugger.RequestContinue();
        }
        RefreshBreakpointMarkers();
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
        // currently-loaded instance of this script; no save required.
        Lua::FLuaDebugger::Get().SetBreakpoint(
            FStringView(VirtualPath.c_str(), VirtualPath.size()),
            Line,
            !bWasSet);

        RefreshBreakpointMarkers();
    }


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
            const FString Path(Symbol.Path.c_str(), Symbol.Path.size());

            // Path -> symbol map serves both hover lookups (full path) and
            // a future jump-to-definition feature.
            SymbolByPath[Path] = Symbol;

            if (Symbol.Parent.empty())
            {
                TopLevelSymbols.push_back(Symbol);
            }
            else
            {
                const FString Parent(Symbol.Parent.c_str(), Symbol.Parent.size());
                SymbolsByPath[Parent].push_back(Symbol);
            }

            if (Symbol.Kind == Lua::ELuaSymbolKind::Table)
            {
                TableNames.insert(Path);
            }
        }
    }

    void FLuaEditorTool::RebuildLocalIndex()
    {
        Locals.clear();

        const int LineCount = CodeEditor.GetLineCount();
        for (int LineIdx = 0; LineIdx < LineCount; ++LineIdx)
        {
            const std::string Line = CodeEditor.GetLineText(LineIdx);
            const int N = (int)Line.size();
            int I = 0;

            // Skip leading whitespace.
            while (I < N && (Line[I] == ' ' || Line[I] == '\t')) ++I;
            if (I >= N) continue;

            // Single-line comment guard.
            if (I + 1 < N && Line[I] == '-' && Line[I + 1] == '-') continue;

            // Match `local` keyword followed by whitespace.
            const char* kLocal = "local";
            const int kLocalLen = 5;
            if (I + kLocalLen > N) continue;
            if (std::memcmp(Line.data() + I, kLocal, kLocalLen) != 0) continue;
            const int After = I + kLocalLen;
            if (After < N && (IsIdentChar(Line[After]) || Line[After] == '_')) continue; // not the keyword
            I = After;
            while (I < N && (Line[I] == ' ' || Line[I] == '\t')) ++I;
            if (I >= N) continue;

            // Optionally consume `function NAME` form first (we tag those as
            // `function` regardless of any explicit annotation).
            bool bIsFunctionForm = false;
            const char* kFunction = "function";
            const int kFunctionLen = 8;
            if (I + kFunctionLen <= N && std::memcmp(Line.data() + I, kFunction, kFunctionLen) == 0
                && (I + kFunctionLen >= N || !IsIdentChar(Line[I + kFunctionLen])))
            {
                bIsFunctionForm = true;
                I += kFunctionLen;
                while (I < N && (Line[I] == ' ' || Line[I] == '\t')) ++I;
            }

            // Capture the first identifier (we ignore tuple-binding cases like
            // `local a, b = ...` for the second name onward; keeping it simple
            // and predictable beats half-supporting destructuring).
            const int NameStart = I;
            while (I < N && IsIdentChar(Line[I])) ++I;
            if (I == NameStart) continue;
            const FString Name(Line.data() + NameStart, I - NameStart);

            FLocalDecl Decl;
            Decl.Line = LineIdx;

            if (bIsFunctionForm)
            {
                Decl.TypeAnnotation.assign("function");
                Locals[Name] = Decl;
                continue;
            }

            // Skip whitespace, look for `: TypeName`.
            while (I < N && (Line[I] == ' ' || Line[I] == '\t')) ++I;
            if (I < N && Line[I] == ':')
            {
                ++I;
                while (I < N && (Line[I] == ' ' || Line[I] == '\t')) ++I;
                // Type spans until '=' or end-of-line; trim trailing whitespace.
                int TypeStart = I;
                while (I < N && Line[I] != '=') ++I;
                int TypeEnd = I;
                while (TypeEnd > TypeStart && (Line[TypeEnd - 1] == ' ' || Line[TypeEnd - 1] == '\t')) --TypeEnd;
                if (TypeEnd > TypeStart)
                {
                    Decl.TypeAnnotation.assign(Line.data() + TypeStart, TypeEnd - TypeStart);
                }
            }

            // Read a value hint after `=` for an inferred type when there's
            // no explicit annotation.
            while (I < N && (Line[I] == ' ' || Line[I] == '\t')) ++I;
            if (I < N && Line[I] == '=')
            {
                ++I;
                while (I < N && (Line[I] == ' ' || Line[I] == '\t')) ++I;
                if (I < N)
                {
                    const char V = Line[I];
                    if (V == '"' || V == '\'' || V == '`')   Decl.ValueHint.assign("string");
                    else if (V == '{')                        Decl.ValueHint.assign("table");
                    else if (V == 't' && I + 3 < N && std::memcmp(Line.data() + I, "true", 4) == 0)  Decl.ValueHint.assign("boolean");
                    else if (V == 'f' && I + 4 < N && std::memcmp(Line.data() + I, "false", 5) == 0) Decl.ValueHint.assign("boolean");
                    else if (V == 'n' && I + 2 < N && std::memcmp(Line.data() + I, "nil", 3) == 0)   Decl.ValueHint.assign("nil");
                    else if (V == 'f' && I + 7 < N && std::memcmp(Line.data() + I, "function", 8) == 0) Decl.ValueHint.assign("function");
                    else if (V >= '0' && V <= '9')            Decl.ValueHint.assign("number");
                    else if (V == '-' && I + 1 < N && Line[I + 1] >= '0' && Line[I + 1] <= '9') Decl.ValueHint.assign("number");
                    else if (IsIdentChar(V) && !(V >= '0' && V <= '9'))
                    {
                        // RHS starts with an identifier; read it through to
                        // the next non-ident char, then check whether the
                        // assignment is a bare reference (`local X = Y`,
                        // optionally with trailing whitespace / comment).
                        // We deliberately don't try to chase `Y.field` or
                        // `Y()` here: those return arbitrary types that we
                        // can't infer without a real type checker, so we
                        // leave the hint blank rather than misreport.
                        const int RhsStart = I;
                        while (I < N && IsIdentChar(Line[I])) ++I;
                        const FString RhsName(Line.data() + RhsStart, I - RhsStart);
                        int Tail = I;
                        while (Tail < N && (Line[Tail] == ' ' || Line[Tail] == '\t')) ++Tail;

                        // Constructor pattern: `local X = Base.new(...)`. Treat
                        // the local as having type `Base` so hover and member
                        // completion can route through the existing global
                        // symbol harvest. Only kicks in when `Base` is a known
                        // global table; anything else stays untyped to avoid
                        // mislabeling unrelated `Foo.new` chains.
                        if (Tail + 4 <= N
                            && Line[Tail] == '.'
                            && std::memcmp(Line.data() + Tail + 1, "new", 3) == 0
                            && (Tail + 4 == N || !IsIdentChar(Line[Tail + 4])))
                        {
                            int After = Tail + 4;
                            while (After < N && (Line[After] == ' ' || Line[After] == '\t')) ++After;
                            if (After < N && Line[After] == '('
                                && SymbolsByPath.find(RhsName) != SymbolsByPath.end()
                                && Decl.TypeAnnotation.empty())
                            {
                                Decl.TypeAnnotation = RhsName;
                                Locals[Name] = Decl;
                                continue;
                            }
                        }

                        const bool bBareRef = (Tail >= N)
                            || (Tail + 1 < N && Line[Tail] == '-' && Line[Tail + 1] == '-')
                            || Line[Tail] == ';';
                        if (bBareRef)
                        {
                            // Inherit the source local's type / value hint.
                            // Because we scan top-down and skip pure-comment
                            // lines, RhsName has already been populated when
                            // we get here (Lua locals can only reference
                            // earlier declarations).
                            auto It = Locals.find(RhsName);
                            if (It != Locals.end() && Decl.TypeAnnotation.empty())
                            {
                                const FLocalDecl& Src = It->second;
                                if (!Src.TypeAnnotation.empty())
                                {
                                    Decl.TypeAnnotation = Src.TypeAnnotation;
                                }
                                else if (!Src.ValueHint.empty())
                                {
                                    Decl.ValueHint = Src.ValueHint;
                                }
                                Decl.OriginName = RhsName;
                            }
                        }
                    }
                }
            }

            Locals[Name] = Decl;
        }
    }

    void FLuaEditorTool::DrawFreeFormHoverTooltip()
    {
        // Use the editor's own coordinate API to translate mouse -> (line, col).
        // GetScreenPosForCoordinate(0,0) returns the origin of the text region.
        const ImVec2 Origin = CodeEditor.GetScreenPosForCoordinate(0, 0);
        const float LineHeight = CodeEditor.GetLineHeight();
        const float GlyphWidth = CodeEditor.GetGlyphWidth();
        if (LineHeight <= 0.0f || GlyphWidth <= 0.0f)
        {
            return;
        }

        const ImGuiIO& Io = ImGui::GetIO();
        const ImVec2 Mouse = Io.MousePos;
        const float RelX = Mouse.x - Origin.x;
        const float RelY = Mouse.y - Origin.y;
        if (RelX < 0.0f || RelY < 0.0f) return;

        const int Line = (int)(RelY / LineHeight);
        const int Col  = (int)(RelX / GlyphWidth);
        if (Line < 0 || Line >= CodeEditor.GetLineCount()) return;
        if (Line < CodeEditor.GetFirstVisibleLine() || Line > CodeEditor.GetLastVisibleLine()) return;

        const std::string LineText = CodeEditor.GetLineText(Line);

        // Visible-column to byte-index translation: tabs occupy [1..tabSize]
        // visual columns, so a naive Col-as-index lookup would skew on lines
        // that lead with tabs.
        const int TabSize = CodeEditor.GetTabSize();
        int Visible = 0;
        int ByteIdx = 0;
        const int N = (int)LineText.size();
        while (ByteIdx < N)
        {
            const char C = LineText[ByteIdx];
            const int Step = (C == '\t') ? (TabSize - (Visible % TabSize)) : 1;
            if (Visible + Step > Col) break;
            Visible += Step;
            ++ByteIdx;
        }
        if (ByteIdx >= N) return;

        // String literal hover.
        FStringHit Hit;
        if (FindStringAt(LineText, ByteIdx, Hit))
        {
            // Body excludes quotes. Bytes here are the raw source bytes (what
            // the file actually pays for; not the interpreted runtime length
            // (escapes like \n are 2 bytes in source, 1 at runtime).
            const int InnerStart = Hit.StartCol + 1;
            const int InnerEnd   = Hit.bClosed ? (Hit.EndCol - 1) : Hit.EndCol;
            const int BodyLen    = std::max(0, InnerEnd - InnerStart);

            // Decoded length: walk and resolve simple escape sequences so the
            // tooltip can also report the runtime char count.
            int RuntimeLen = 0;
            for (int K = InnerStart; K < InnerEnd; ++K)
            {
                if (LineText[K] == '\\' && K + 1 < InnerEnd) { ++K; }
                ++RuntimeLen;
            }

            BeginTranslucentTooltip();
            const char* Kind = (Hit.Quote == '`') ? "interpolated string" : "string";
            ImGui::TextColored(ImVec4(0.86f, 0.71f, 0.35f, 1.0f), "(%s)", Kind);
            ImGui::Separator();
            ImGui::Text("Source: %d byte%s", BodyLen, BodyLen == 1 ? "" : "s");
            if (RuntimeLen != BodyLen)
            {
                ImGui::Text("Runtime: %d char%s (after escapes)", RuntimeLen, RuntimeLen == 1 ? "" : "s");
            }
            if (!Hit.bClosed)
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Unterminated string literal.");
            }
            // Preview: first 64 chars to avoid runaway tooltips on long literals.
            if (BodyLen > 0)
            {
                ImGui::Spacing();
                const int PreviewN = std::min(64, BodyLen);
                std::string Preview;
                Preview.reserve(PreviewN + 4);
                Preview.assign(LineText.data() + InnerStart, PreviewN);
                ImGui::TextDisabled("%s%s", Preview.c_str(), BodyLen > PreviewN ? " ..." : "");
            }
            EndTranslucentTooltip();
            return;
        }

        // Number literal hover.
        int NumStart = -1, NumEnd = -1;
        if (FindNumberAt(LineText, ByteIdx, NumStart, NumEnd))
        {
            std::string Token(LineText.data() + NumStart, NumEnd - NumStart);
            // strip underscores for parsing; Luau allows them as visual separators
            std::string Clean;
            Clean.reserve(Token.size());
            for (char C : Token) if (C != '_') Clean.push_back(C);

            BeginTranslucentTooltip();
            ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.6f, 1.0f), "(number)");
            ImGui::Separator();
            ImGui::Text("Literal: %s", Token.c_str());

            // Best-effort parse for hex/binary/decimal.
            if (Clean.size() > 2 && Clean[0] == '0' && (Clean[1] == 'x' || Clean[1] == 'X'))
            {
                unsigned long long V = std::strtoull(Clean.c_str() + 2, nullptr, 16);
                ImGui::Text("Decimal: %llu", V);
            }
            else if (Clean.size() > 2 && Clean[0] == '0' && (Clean[1] == 'b' || Clean[1] == 'B'))
            {
                unsigned long long V = std::strtoull(Clean.c_str() + 2, nullptr, 2);
                ImGui::Text("Decimal: %llu  Hex: 0x%llX", V, V);
            }
            else
            {
                char* EndPtr = nullptr;
                const double V = std::strtod(Clean.c_str(), &EndPtr);
                if (EndPtr != Clean.c_str())
                {
                    if (V == (long long)V && std::abs(V) < 1e15)
                    {
                        ImGui::Text("Hex: 0x%llX", (long long)V);
                    }
                }
            }
            EndTranslucentTooltip();
            return;
        }
    }

    void FLuaEditorTool::DrawDebuggerHoverDuringPauseTooltip(const std::string& Word)
    {
        Lua::FLuaDebugger& Debugger = Lua::FLuaDebugger::Get();
        if (!Debugger.IsPaused()) return;
        if (Word.empty()) return;

        const TVector<Lua::FStackFrame>& Stack = Debugger.GetCallStack();
        if (Stack.empty()) return;

        const int Frame = std::clamp(DebuggerSelectedFrame, 0, (int)Stack.size() - 1);
        const Lua::FStackFrame& F = Stack[Frame];

        auto FindIn = [&](const TVector<Lua::FStackVariable>& Vars) -> const Lua::FStackVariable*
        {
            for (const Lua::FStackVariable& V : Vars)
            {
                if (FStringView(V.Name.c_str(), V.Name.size()) == FStringView(Word.c_str(), Word.size()))
                {
                    return &V;
                }
            }
            return nullptr;
        };

        const Lua::FStackVariable* Hit = FindIn(F.Locals);
        const char* Origin = "local";
        if (Hit == nullptr) { Hit = FindIn(F.Upvalues); Origin = "upvalue"; }
        if (Hit == nullptr) return;

        BeginTranslucentTooltip();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "(paused %s) %s", Origin, Hit->Name.c_str());
        ImGui::Separator();
        ImGui::Text("type:  %s", Hit->TypeName.c_str());
        ImGui::Text("value: %s", Hit->Value.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("Frame [%d]. %s", Frame, F.FunctionName.c_str());
        EndTranslucentTooltip();
    }

    void FLuaEditorTool::OnHoverIdentifier(const std::string& Word, const std::string& DottedPath)
    {
        // While paused, prefer showing the live value of the identifier under
        // the cursor; that's the most useful single piece of information at
        // a breakpoint. We try three forms in priority order:
        //   1. Dotted path (`MyThing.bThing`): eval against the paused frame
        //      env. Shows the resolved value of the member, not the parent
        //      table. Also folds in "{ N fields }" preview for tables/userdata.
        //   2. Bare word in the cached snapshot (Locals / Upvalues lists)
        //      cheap and covers the common case where the user wrote a single
        //      identifier.
        if (IsDebuggerPausedHere())
        {
            Lua::FLuaDebugger& Debugger = Lua::FLuaDebugger::Get();
            const TVector<Lua::FStackFrame>& Stack = Debugger.GetCallStack();
            if (!Stack.empty())
            {
                const int Frame = std::clamp(DebuggerSelectedFrame, 0, (int)Stack.size() - 1);

                // Prefer dotted-path eval. `obj.field` resolves the field's
                // value, not the parent's. We re-eval here rather than scan
                // the snapshot because nested fields aren't in it.
                if (!DottedPath.empty() && DottedPath != Word)
                {
                    FString Out, Type;
                    bool bExpandable = false;
                    const bool bOk = Debugger.EvaluateInPausedFrameWithExpandable(
                        Frame, FStringView(DottedPath.c_str(), DottedPath.size()),
                        Out, Type, bExpandable);
                    if (bOk && Type != "error")
                    {
                        BeginTranslucentTooltip();
                        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                            "(paused) %s", DottedPath.c_str());
                        ImGui::Separator();
                        ImGui::Text("type:  %s", Type.c_str());
                        ImGui::PushTextWrapPos(420.0f);
                        ImGui::Text("value: %s", Out.c_str());
                        ImGui::PopTextWrapPos();

                        if (bExpandable)
                        {
                            TVector<Lua::FChildEntry> Children;
                            Debugger.EnumerateChildrenInPausedFrame(
                                Frame, FStringView(DottedPath.c_str(), DottedPath.size()),
                                Children, 16);
                            if (!Children.empty())
                            {
                                ImGui::Spacing();
                                ImGui::TextDisabled("Fields:");
                                if (ImGui::BeginTable("##hover_fields", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
                                {
                                    for (size_t I = 0; I < Children.size() && I < 8; ++I)
                                    {
                                        const Lua::FChildEntry& C = Children[I];
                                        ImGui::TableNextRow();
                                        ImGui::TableNextColumn();
                                        ImGui::TextUnformatted(C.Key.c_str());
                                        ImGui::TableNextColumn();
                                        ImGui::TextDisabled("%s", C.Value.c_str());
                                    }
                                    ImGui::EndTable();
                                }
                                if (Children.size() > 8)
                                {
                                    ImGui::TextDisabled("...and %zu more", Children.size() - 8);
                                }
                            }
                        }
                        EndTranslucentTooltip();
                        return;
                    }
                }

                // Bare-word fallback against the cached snapshot for plain
                // identifiers. fast path, no Lua VM call.
                const Lua::FStackFrame& F = Stack[Frame];
                auto Has = [&](const TVector<Lua::FStackVariable>& Vars)
                {
                    for (const Lua::FStackVariable& V : Vars)
                    {
                        if (FStringView(V.Name.c_str(), V.Name.size()) == FStringView(Word.c_str(), Word.size()))
                            return true;
                    }
                    return false;
                };
                if (Has(F.Locals) || Has(F.Upvalues))
                {
                    DrawDebuggerHoverDuringPauseTooltip(Word);
                    return;
                }
            }
        }

        // Locals (parsed from the buffer) take priority over globals; the
        // cursor reads what the user wrote in this file, not whatever happens
        // to be in the live VM under the same name.
        if (Word == DottedPath)
        {
            const FString WordKey(Word.c_str(), Word.size());
            auto LItr = Locals.find(WordKey);
            if (LItr != Locals.end())
            {
                const FLocalDecl& L = LItr->second;
                BeginTranslucentTooltip();
                if (!L.TypeAnnotation.empty())
                {
                    ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                        "(local) %s: %s", Word.c_str(), L.TypeAnnotation.c_str());
                }
                else if (!L.ValueHint.empty())
                {
                    ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                        "(local) %s: %s", Word.c_str(), L.ValueHint.c_str());
                    ImGui::TextDisabled("inferred from initializer");
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                        "(local) %s", Word.c_str());
                }
                if (!L.OriginName.empty())
                {
                    ImGui::TextDisabled("inherited from `%s`", L.OriginName.c_str());
                }
                if (L.Line >= 0)
                {
                    ImGui::Separator();
                    ImGui::TextDisabled("declared on line %d", L.Line + 1);
                }
                EndTranslucentTooltip();
                return;
            }
        }

        // Try the full dotted path first ("Engine.VFS.ReadFile") so a hover
        // on a method shows method info rather than a top-level "ReadFile"
        // collision. Fall back to the bare word for unqualified globals
        // (locals like "self", or top-level functions).
        const FString FullKey(DottedPath.c_str(), DottedPath.size());
        auto Itr = SymbolByPath.find(FullKey);
        if (Itr == SymbolByPath.end() && Word != DottedPath)
        {
            const FString WordKey(Word.c_str(), Word.size());
            Itr = SymbolByPath.find(WordKey);
        }
        if (Itr == SymbolByPath.end())
        {
            // Last fallback: keyword tooltip: at least lets the user
            // confirm what something like `continue` or `repeat` is.
            for (const char* K : kLuauKeywords)
            {
                if (Word == K)
                {
                    BeginTranslucentTooltip();
                    ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.45f, 1.0f), "(keyword) %s", K);
                    EndTranslucentTooltip();
                    return;
                }
            }
            return;
        }

        const Lua::FLuaSymbol& Symbol = Itr->second;

        BeginTranslucentTooltip();
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

        EndTranslucentTooltip();
    }

    namespace
    {
        // suggestions, suggestionKinds, and suggestionDetails are parallel
        // arrays; sort the suggestion strings via an index permutation so
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
        const FString OwnerPath = ResolveOwnerPath(Line, static_cast<int>(State.searchTermStartIndex));

        const std::string& Term = State.searchTerm;
        const FString TermLower = [&]
        {
            FString Out;
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
            FString ResolvedType;
            auto Itr = SymbolsByPath.find(OwnerPath);
            if (Itr != SymbolsByPath.end())
            {
                ResolvedType = OwnerPath;
            }
            else if (OwnerPath.find('.') == FString::npos)
            {
                auto LocalItr = Locals.find(OwnerPath);
                if (LocalItr != Locals.end() && !LocalItr->second.TypeAnnotation.empty())
                {
                    ResolvedType = LocalItr->second.TypeAnnotation;
                    Itr = SymbolsByPath.find(ResolvedType);
                }
                else if (OwnerPath == "self")
                {
                    const int CurLine = static_cast<int>(State.line);
                    for (int LineIdx = CurLine - 1; LineIdx >= 0; --LineIdx)
                    {
                        const std::string Prev = CodeEditor.GetLineText(LineIdx);
                        const size_t FuncPos = Prev.find("function ");
                        if (FuncPos == std::string::npos) continue;

                        size_t Start = FuncPos + 9;
                        while (Start < Prev.size() && (Prev[Start] == ' ' || Prev[Start] == '\t')) ++Start;
                        size_t End = Start;
                        while (End < Prev.size() && IsIdentChar(Prev[End])) ++End;
                        if (End == Start || End >= Prev.size() || Prev[End] != ':') continue;

                        const FString Ident(Prev.data() + Start, Prev.data() + End);
                        auto SelfLocal = Locals.find(Ident);
                        if (SelfLocal != Locals.end() && !SelfLocal->second.TypeAnnotation.empty())
                        {
                            ResolvedType = SelfLocal->second.TypeAnnotation;
                            Itr = SymbolsByPath.find(ResolvedType);
                            break;
                        }
                    }
                }
            }

            // Methods. Skip underscore-prefixed children; those are
            // metadata slots on the base table (e.g. `_Shape`, `__index`)
            // and shouldn't pollute the suggestion list.
            if (Itr != SymbolsByPath.end())
            {
                for (const Lua::FLuaSymbol& Symbol : Itr->second)
                {
                    if (!Symbol.Name.empty() && Symbol.Name[0] == '_') continue;
                    if (MatchesPrefix(FStringView(Symbol.Name.c_str(), Symbol.Name.size())))
                    {
                        PushSymbol(Symbol);
                    }
                }
            }

            // Per-instance fields. By convention, an entry in the resolved
            // type's `_Shape` table publishes a field name + a type-string
            // value the engine writes onto every instance at attach time.
            // Single source of truth lives in the .luau stdlib itself
            // this branch just re-routes those entries through the normal
            // suggestion path with a 'v' kind badge.
            if (!ResolvedType.empty())
            {
                FString ShapePath = ResolvedType;
                ShapePath += "._Shape";
                auto ShapeItr = SymbolsByPath.find(ShapePath);
                if (ShapeItr != SymbolsByPath.end())
                {
                    for (const Lua::FLuaSymbol& Field : ShapeItr->second)
                    {
                        if (!MatchesPrefix(FStringView(Field.Name.c_str(), Field.Name.size()))) continue;
                        State.suggestions.emplace_back(Field.Name.c_str(), Field.Name.size());
                        State.suggestionKinds.push_back('v');
                        State.suggestionDetails.emplace_back(
                            Field.ValuePreview.empty()
                                ? std::string("field")
                                : std::string(Field.ValuePreview.c_str(), Field.ValuePreview.size()));
                    }
                }
            }

            // Co-sort the three parallel arrays by suggestion name.
            CoSortSuggestions(State);
            return;
        }

        // Top-level: keywords + engine globals + identifiers in the buffer.
        eastl::hash_set<FString> Seen;
        auto SeenInsert = [&](const char* Data, size_t Size) -> bool
        {
            const FString Key(Data, Size);
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

        // Identifiers harvested from the current buffer; picks up locals,
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
        constexpr ImU32 Red          = IM_COL32(220, 60, 60, 255);
        constexpr ImU32 Translucent  = IM_COL32(220, 60, 60, 40);
        constexpr ImU32 Gray         = IM_COL32(150, 150, 150, 255);
        constexpr ImU32 GrayFill     = IM_COL32(150, 150, 150, 30);
        constexpr ImU32 Cyan         = IM_COL32(80, 200, 220, 255);
        constexpr ImU32 CyanFill     = IM_COL32(80, 200, 220, 40);
        constexpr ImU32 Magenta      = IM_COL32(220, 120, 220, 255);
        constexpr ImU32 MagentaFill  = IM_COL32(220, 120, 220, 40);
        constexpr ImU32 Cobalt       = IM_COL32(100, 140, 230, 255);
        constexpr ImU32 CobaltFill   = IM_COL32(100, 140, 230, 40);

        Lua::FLuaDebugger& Debugger = Lua::FLuaDebugger::Get();
        const FStringView SrcView(VirtualPath.c_str(), VirtualPath.size());

        for (int Line : Breakpoints)
        {
            const Lua::FBreakpointSettings* S = Debugger.GetBreakpointSettings(SrcView, Line);

            // Marker color and tooltip vary by mode so the gutter encodes
            // the breakpoint's behavior at a glance.
            ImU32 Col = Red;
            ImU32 Fill = Translucent;
            const char* Title = "Breakpoint";
            char TooltipBuf[256];
            std::snprintf(TooltipBuf, sizeof(TooltipBuf), "Breakpoint - paused on hit");

            if (S != nullptr)
            {
                if (!S->bEnabled)
                {
                    Col = Gray; Fill = GrayFill;
                    Title = "Breakpoint (disabled)";
                    std::snprintf(TooltipBuf, sizeof(TooltipBuf), "Disabled. Hits: %u", S->HitCount);
                }
                else if (!S->LogMessage.empty())
                {
                    Col = Magenta; Fill = MagentaFill;
                    Title = "Log point";
                    std::snprintf(TooltipBuf, sizeof(TooltipBuf), "Log point: %s\nHits: %u", S->LogMessage.c_str(), S->HitCount);
                }
                else if (!S->Condition.empty())
                {
                    Col = Cyan; Fill = CyanFill;
                    Title = "Conditional breakpoint";
                    std::snprintf(TooltipBuf, sizeof(TooltipBuf), "When: %s\nHits: %u", S->Condition.c_str(), S->HitCount);
                }
                else if (S->IgnoreCount > 0)
                {
                    Col = Cobalt; Fill = CobaltFill;
                    Title = "Breakpoint (hit count)";
                    std::snprintf(TooltipBuf, sizeof(TooltipBuf), "Skips first %u hits. Hits: %u", S->IgnoreCount, S->HitCount);
                }
            }

            CodeEditor.AddMarker(Line, Col, Fill, Title, TooltipBuf);
        }

        // Bookmarks: a faint blue stripe so they don't fight breakpoint reds.
        const ImU32 Blue     = IM_COL32(100, 180, 255, 255);
        const ImU32 BlueFill = IM_COL32(100, 180, 255, 28);
        for (int Line : Bookmarks)
        {
            // Skip if already breakpoint-marked; let the breakpoint color win.
            if (Breakpoints.count(Line)) continue;
            CodeEditor.AddMarker(Line, Blue, BlueFill, "Bookmark", "Bookmark (F2 to navigate, Ctrl+F2 to toggle)");
        }

        // Yellow PC arrow on the line we're paused at; only if the
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

        // Compile-error stripe. Drawn last so it dominates whatever else is
        // on the line (a stale breakpoint on a syntax-broken line is the
        // less useful piece of information). Line is 1-based from Luau.
        if (bHasCompileError && CompileErrorLine >= 1 && CompileErrorLine <= CodeEditor.GetLineCount())
        {
            const ImU32 ErrorCol  = IM_COL32(255, 80, 80, 255);
            const ImU32 ErrorFill = IM_COL32(255, 80, 80, 70);
            char Tip[512];
            std::snprintf(Tip, sizeof(Tip), "Compile error: %s", CompileErrorMessage.c_str());
            CodeEditor.AddMarker(CompileErrorLine - 1, ErrorCol, ErrorFill, "Compile error", Tip);
        }
    }

    bool FLuaEditorTool::IsDebuggerPausedHere() const
    {
        Lua::FLuaDebugger& Debugger = Lua::FLuaDebugger::Get();
        if (!Debugger.IsPaused())
        {
            return false;
        }

        // Tolerant path match. Luau's chunkname can carry a sigil prefix
        // (`@`, `=`) and the virtual path used to open the editor may or
        // may not have a leading `/`. Compare suffix-style with case
        // folding so any of those variations still pair.
        auto Normalize = [](FStringView S)
        {
            while (!S.empty() && (S[0] == '@' || S[0] == '=' || S[0] == '/' || S[0] == '\\'))
            {
                S = S.substr(1);
            }
            return S;
        };
        FStringView A = Normalize(Debugger.GetPausedSource());
        FStringView B = Normalize(FStringView(VirtualPath.c_str(), VirtualPath.size()));

        auto IEqual = [](FStringView X, FStringView Y)
        {
            if (X.size() != Y.size()) return false;
            for (size_t I = 0; I < X.size(); ++I)
            {
                char Xc = X[I]; if (Xc == '\\') Xc = '/';
                char Yc = Y[I]; if (Yc == '\\') Yc = '/';
                if (Xc >= 'A' && Xc <= 'Z') Xc = (char)(Xc + 32);
                if (Yc >= 'A' && Yc <= 'Z') Yc = (char)(Yc + 32);
                if (Xc != Yc) return false;
            }
            return true;
        };

        if (IEqual(A, B))
        {
            return true;
        }
        if (A.size() > B.size() && IEqual(A.substr(A.size() - B.size()), B))
        {
            return true;
        }
        if (B.size() > A.size() && IEqual(B.substr(B.size() - A.size()), A))
        {
            return true;
        }
        return false;
    }


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
        if (ImGui::Button(LE_ICON_REFRESH " Reload"))
        {
            LoadFromDisk();
        }
        ImGuiX::TextTooltip("Discard buffer changes and reload from disk.");

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        ImGui::BeginDisabled(!CodeEditor.CanUndo());
        if (ImGui::Button(LE_ICON_UNDO_VARIANT " Undo"))
        {
            CodeEditor.Undo();
        }
        ImGuiX::TextTooltip("Undo last change (Ctrl+Z).");
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!CodeEditor.CanRedo());
        if (ImGui::Button(LE_ICON_REDO_VARIANT " Redo"))
        {
            CodeEditor.Redo();
        }
        ImGuiX::TextTooltip("Redo last undone change (Ctrl+Y).");
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        if (ImGui::Button(LE_ICON_MAGNIFY " Find"))
        {
            CodeEditor.OpenFindReplaceWindow();
        }
        ImGuiX::TextTooltip("Open the find/replace bar (Ctrl+F).");

        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_FORMAT_LINE_SPACING " Goto"))
        {
            bRequestOpenGoto = true;
        }
        ImGuiX::TextTooltip("Jump to a specific line number (Ctrl+G).");

        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_CODE_BRACES " Snippets"))
        {
            ImGui::OpenPopup("##lua_snippets");
        }
        ImGuiX::TextTooltip("Insert a code snippet at the cursor.");
        DrawSnippetsPopup();

        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_AUTO_FIX " Format"))
        {
            ImGui::OpenPopup("##lua_format");
        }
        ImGuiX::TextTooltip("Whitespace and case transforms.");
        DrawFormatPopup();

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        if (ImGui::Button(LE_ICON_REFRESH " Symbols"))
        {
            RebuildSymbolIndex();
            ImGuiX::Notifications::NotifySuccess("Re-harvested {0} Lua symbols.", (int)AllSymbols.size());
        }
        ImGuiX::TextTooltip("Re-walk the live Lua VM to refresh autocomplete suggestions.\nUse after registering new globals from C++.");

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // Outline toggle. Reflects state via active-button styling.
        ImGui::PushStyleColor(ImGuiCol_Button, bShowOutline ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
        if (ImGui::Button(LE_ICON_FORMAT_LIST_BULLETED " Outline"))
        {
            bShowOutline = !bShowOutline;
            if (bShowOutline)
            {
                RebuildDocumentOutline();
            }
        }
        ImGui::PopStyleColor();
        ImGuiX::TextTooltip("Toggle the right-side outline panel (Ctrl+\\).\nLists functions and locals in this file.");

        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_BOOKMARK " Bookmarks"))
        {
            ImGui::OpenPopup("##lua_bookmarks");
        }
        if (ImGui::BeginPopup("##lua_bookmarks"))
        {
            if (Bookmarks.empty())
            {
                ImGui::TextDisabled("No bookmarks set.");
                ImGui::TextDisabled("Press Ctrl+F2 to add one at the cursor.");
            }
            else
            {
                eastl::vector<int> Sorted(Bookmarks.begin(), Bookmarks.end());
                std::sort(Sorted.begin(), Sorted.end());
                for (int Line : Sorted)
                {
                    char Label[64];
                    std::snprintf(Label, sizeof(Label), LE_ICON_BOOKMARK " Line %d##bm%d", Line + 1, Line);
                    if (ImGui::Selectable(Label))
                    {
                        CodeEditor.SetCursor(Line, 0);
                        CodeEditor.ScrollToLine(Line, TextEditor::Scroll::alignMiddle);
                    }
                }
                ImGui::Separator();
                if (ImGui::Button("Clear all##bm", ImVec2(-1, 0)))
                {
                    Bookmarks.clear();
                    RefreshBreakpointMarkers();
                }
            }
            ImGui::EndPopup();
        }
        ImGuiX::TextTooltip("Bookmarks. Ctrl+F2 toggles, F2/Shift+F2 cycle.");

        ImGui::SameLine();
        ImGui::BeginDisabled(!IsDebuggerPausedHere());
        if (ImGui::Button(LE_ICON_DEBUG_STEP_OVER " Run to cursor"))
        {
            RunToCursor();
        }
        ImGui::EndDisabled();
        ImGuiX::TextTooltip("Continue execution until the cursor's line is reached (Ctrl+F10).");

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
        if (ImGui::Button(LE_ICON_HELP_CIRCLE " Help"))
        {
            ImGui::OpenPopup("##lua_help");
        }
        ImGuiX::TextTooltip("Quick Luau reference and editor shortcuts.");
        DrawHelpPopup();

        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_COG " Settings"))
        {
            ImGui::OpenPopup("##lua_editor_settings");
        }
        DrawSettingsPopup();

        if (bRequestOpenGoto)
        {
            ImGui::OpenPopup("##lua_goto_line");
            bRequestOpenGoto = false;
            GotoLineBuffer = CodeEditor.GetCurrentCursorPosition().line + 1;
        }
        DrawGotoLinePopup();
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
        ImGuiX::TextTooltip("Tip: Ctrl+wheel over the editor adjusts this live.");

        if (ImGui::SliderFloat("Line spacing", &EditorLineSpacing, 1.0f, 2.0f, "%.2f")) bDirty = true;
        if (ImGui::SliderInt("Tab size", &EditorTabSize, 1, 8)) bDirty = true;
        if (ImGui::Checkbox("Show line numbers",      &bEditorShowLineNumbers)) bDirty = true;
        if (ImGui::Checkbox("Show whitespace",        &bEditorShowWhitespace))  bDirty = true;
        if (ImGui::Checkbox("Show scrollbar minimap", &bEditorShowMiniMap))     bDirty = true;
        if (ImGui::Checkbox("Read-only",              &bEditorReadOnly))        bDirty = true;

        ImGui::Spacing();
        ImGui::TextDisabled("Editing");
        ImGui::Separator();
        if (ImGui::Checkbox("Auto-indent",          &bAutoIndent))           bDirty = true;
        if (ImGui::Checkbox("Match brackets",       &bShowMatchingBrackets)) bDirty = true;
        if (ImGui::Checkbox("Auto-close pairs",     &bCompletePairedGlyphs)) bDirty = true;
        if (ImGui::Checkbox("Insert spaces on Tab", &bInsertSpacesOnTabs))   bDirty = true;
        ImGuiX::TextTooltip("When on, pressing Tab inserts spaces instead of a tab character.");
        ImGui::Checkbox("Trim trailing whitespace on save", &bTrimTrailingOnSave);

        ImGui::Spacing();
        ImGui::TextDisabled("Autocomplete");
        ImGui::Separator();
        if (ImGui::Checkbox("Trigger while typing", &bAutoTriggerCompletion)) bDirty = true;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::SliderInt("Trigger delay (ms)", &AutoTriggerDelayMs, 0, 1000)) bDirty = true;
        ImGuiX::TextTooltip("Delay between the last keystroke and the suggestion popup.\nCtrl+Space always triggers it manually.");

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
        ImGui::Separator();

        if (ImGui::Button("Persist as default", ImVec2(-1, 0)))
        {
            GConfig->Set<float>(kKeyFontScale,       EditorFontScale);
            GConfig->Set<int32>(kKeyTabSize,         EditorTabSize);
            GConfig->Set<float>(kKeyLineSpacing,     EditorLineSpacing);
            GConfig->Set<bool>(kKeyShowWhitespace,   bEditorShowWhitespace);
            GConfig->Set<bool>(kKeyShowLineNumbers,  bEditorShowLineNumbers);
            GConfig->Set<bool>(kKeyShowMiniMap,      bEditorShowMiniMap);
            GConfig->Set<bool>(kKeyAutoIndent,       bAutoIndent);
            GConfig->Set<bool>(kKeyMatchBrackets,    bShowMatchingBrackets);
            GConfig->Set<bool>(kKeyCompletePairs,    bCompletePairedGlyphs);
            GConfig->Set<bool>(kKeyInsertSpaces,     bInsertSpacesOnTabs);
            GConfig->Set<bool>(kKeyTrimOnSave,       bTrimTrailingOnSave);
            GConfig->Set<bool>(kKeyAutoTrigger,      bAutoTriggerCompletion);
            GConfig->Set<int32>(kKeyTriggerDelayMs,  AutoTriggerDelayMs);
            GConfig->Set<std::string>(kKeyPalette,   std::string(EditorPalette == EPalette::Dark ? "Dark" : "Light"));
            ImGuiX::Notifications::NotifySuccess("Lua editor settings saved.");
        }

        if (ImGui::Button("Reset to defaults", ImVec2(-1, 0)))
        {
            EditorFontScale = 1.25f;
            EditorTabSize = 4;
            EditorLineSpacing = 1.0f;
            bEditorShowWhitespace = false;
            bEditorShowLineNumbers = true;
            bEditorShowMiniMap = true;
            bEditorReadOnly = false;
            bAutoIndent = true;
            bShowMatchingBrackets = true;
            bCompletePairedGlyphs = true;
            bInsertSpacesOnTabs = false;
            bTrimTrailingOnSave = false;
            bAutoTriggerCompletion = true;
            AutoTriggerDelayMs = 100;
            EditorPalette = EPalette::Dark;
            bDirty = true;
        }

        if (bDirty)
        {
            ApplyEditorSettings();
        }

        ImGui::EndPopup();
    }

    void FLuaEditorTool::DrawSnippetsPopup()
    {
        if (!ImGui::BeginPopup("##lua_snippets"))
        {
            return;
        }

        ImGui::TextDisabled("Insert at cursor");
        ImGui::Separator();
        for (const FLuaSnippet& Snip : kLuaSnippets)
        {
            if (ImGui::MenuItem(Snip.Label))
            {
                InsertSnippet(Snip.Body);
            }
        }
        ImGui::EndPopup();
    }

    void FLuaEditorTool::InsertSnippet(const char* Snippet)
    {
        if (Snippet == nullptr || *Snippet == '\0')
        {
            return;
        }
        // ReplaceTextInCurrentCursor inserts at the cursor (or replaces a
        // selection), participates in the editor's undo/redo, and triggers
        // the change callback so the dirty flag updates correctly.
        CodeEditor.ReplaceTextInCurrentCursor(std::string_view(Snippet));
        CodeEditor.SetFocus();
    }

    void FLuaEditorTool::DrawFormatPopup()
    {
        if (!ImGui::BeginPopup("##lua_format"))
        {
            return;
        }

        ImGui::TextDisabled("Document");
        ImGui::Separator();
        if (ImGui::MenuItem("Strip trailing whitespace"))
        {
            CodeEditor.StripTrailingWhitespaces();
        }
        if (ImGui::MenuItem("Tabs to spaces"))
        {
            CodeEditor.TabsToSpaces();
        }
        if (ImGui::MenuItem("Spaces to tabs"))
        {
            CodeEditor.SpacesToTabs();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Selection");
        ImGui::Separator();
        const bool bHasSel = CodeEditor.AnyCursorHasSelection();
        ImGui::BeginDisabled(!bHasSel);
        if (ImGui::MenuItem("Indent",          "Tab"))
        {
            CodeEditor.IndentLines();
        }
        if (ImGui::MenuItem("Deindent",        "Shift+Tab"))
        {
            CodeEditor.DeindentLines();
        }
        if (ImGui::MenuItem("Move up",         "Alt+Up"))
        {
            CodeEditor.MoveUpLines();
        }
        if (ImGui::MenuItem("Move down",       "Alt+Down"))
        {
            CodeEditor.MoveDownLines();
        }
        if (ImGui::MenuItem("Toggle comments", "Ctrl+/"))
        {
            CodeEditor.ToggleComments();
        }
        if (ImGui::MenuItem("To upper case"))
        {
            CodeEditor.SelectionToUpperCase();
        }
        if (ImGui::MenuItem("To lower case"))
        {
            CodeEditor.SelectionToLowerCase();
        }
        ImGui::EndDisabled();

        ImGui::EndPopup();
    }

    void FLuaEditorTool::DrawGotoLinePopup()
    {
        if (!ImGui::BeginPopup("##lua_goto_line"))
        {
            return;
        }

        ImGui::TextDisabled("Goto line (1 - %d)", CodeEditor.GetLineCount());
        ImGui::Separator();
        ImGui::SetNextItemWidth(160.0f);
        ImGui::SetKeyboardFocusHere();
        const bool bSubmit = ImGui::InputInt("##lua_goto_input", &GotoLineBuffer, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::SameLine();
        const bool bGo = ImGui::Button("Go");

        if (bSubmit || bGo)
        {
            const int Target = std::max(1, std::min(CodeEditor.GetLineCount(), GotoLineBuffer)) - 1;
            CodeEditor.SetCursor(Target, 0);
            CodeEditor.ScrollToLine(Target, TextEditor::Scroll::alignMiddle);
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void FLuaEditorTool::DrawHelpPopup()
    {
        if (!ImGui::BeginPopup("##lua_help"))
        {
            return;
        }

        ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "Lua / Luau Quick Reference");
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Editor shortcuts", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginTable("##help_keys", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
            {
                auto Row = [&](const char* Key, const char* Desc)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.45f, 1.0f), "%s", Key);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(Desc);
                };
                Row("Ctrl+S",        "Save");
                Row("Ctrl+F",        "Find / replace");
                Row("Ctrl+G",        "Goto line");
                Row("Ctrl+Z / Y",    "Undo / redo");
                Row("Ctrl+/",        "Toggle line comments");
                Row("Ctrl+Space",    "Trigger autocomplete");
                Row("Ctrl+Wheel",    "Zoom font");
                Row("Tab / Shift+Tab","Indent / deindent selection");
                Row("Alt+Up / Down", "Move line(s) up/down");
                Row("F5 / F10 / F11","Continue / step over / step into (when paused)");
                Row("Right-click gutter", "Toggle breakpoint");
                ImGui::EndTable();
            }
        }

        if (ImGui::CollapsingHeader("Loops"))
        {
            ImGui::TextWrapped(
                "for i = 1, n do ... end\n"
                "for k, v in pairs(t) do ... end\n"
                "for i, v in ipairs(t) do ... end\n"
                "while cond do ... end\n"
                "repeat ... until cond");
        }
        if (ImGui::CollapsingHeader("Functions"))
        {
            ImGui::TextWrapped(
                "function name(a, b) return a + b end\n"
                "local function name(...) end\n"
                "local f = function() end\n"
                "Call:  f(1, 2)\n"
                "Method:  obj:method(args)  -- 'self' implicit");
        }
        if (ImGui::CollapsingHeader("Tables"))
        {
            ImGui::TextWrapped(
                "local t = { 1, 2, 3 }            -- array part\n"
                "local d = { name = \"x\", n = 1 }  -- record part\n"
                "t[#t + 1] = value                -- push\n"
                "table.insert / table.remove / table.concat");
        }
        if (ImGui::CollapsingHeader("Engine bindings"))
        {
            ImGui::TextWrapped(
                "Hover any identifier in the editor for kind / type / signature.");
        }
        if (ImGui::CollapsingHeader("Luau extras"))
        {
            ImGui::TextWrapped(
                "continue                          -- skip to next loop iter\n"
                "string interpolation: `Hello {name}`  (backticks)\n"
                "type Vec2 = { x: number, y: number }\n"
                "compound assigns: +=  -=  *=  /=  ..= ");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Hit Ctrl+Space anywhere to see live engine globals.");
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
        
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), LE_ICON_PAUSE_CIRCLE " Paused at line %d", Debugger.GetPausedLineZeroBased() + 1);
        ImGui::SameLine(0, 16);

        if (ImGui::Button(LE_ICON_PLAY " Continue"))
        {
            Debugger.RequestContinue();
        }
        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_DEBUG_STEP_OVER " Over"))
        {
            Debugger.RequestStepOver();
        }
        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_DEBUG_STEP_INTO " Into"))
        {
            Debugger.RequestStepInto();
        }
        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_DEBUG_STEP_OUT " Out"))
        {
            Debugger.RequestStepOut();
        }
        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_STOP " Stop"))
        {
            Debugger.RequestStop();
        }

        // Keyboard shortcuts only fire when this editor is the focused tool
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

        ImGui::SameLine(0, 16);
        ImGui::Checkbox("Inline values", &bShowInlineValuesWhilePaused);
        ImGuiX::TextTooltip("Toggle ghost-text \"name = value\" annotations\nfor locals visible in the current frame.");

        ImGui::Separator();

        // split: call stack | locals/upvalues | watches
        const ImVec2 Avail = ImGui::GetContentRegionAvail();
        const float Spacing = ImGui::GetStyle().ItemSpacing.x;
        const float CallStackW = (Avail.x - Spacing * 2.0f) * 0.30f;
        const float LocalsW    = (Avail.x - Spacing * 2.0f) * 0.40f;
        const float WatchW     = Avail.x - CallStackW - LocalsW - Spacing * 2.0f;

        const TVector<Lua::FStackFrame>& Stack = Debugger.GetCallStack();
        if (DebuggerSelectedFrame >= (int)Stack.size())
        {
            DebuggerSelectedFrame = 0;
        }

        if (ImGui::BeginChild("##lua_inline_callstack", ImVec2(CallStackW, Avail.y), ImGuiChildFlags_Borders))
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

                // Show only the file basename in the inline panel; full
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

        if (ImGui::BeginChild("##lua_inline_locals", ImVec2(LocalsW, Avail.y), ImGuiChildFlags_Borders))
        {
            ImGui::TextDisabled("Locals / Upvalues");
            ImGui::Separator();

            if (DebuggerSelectedFrame >= 0 && DebuggerSelectedFrame < (int)Stack.size())
            {
                const Lua::FStackFrame& Frame = Stack[DebuggerSelectedFrame];

                auto DrawTable = [&](const TVector<Lua::FStackVariable>& Vars, const char* Header)
                {
                    if (Vars.empty())
                    {
                        return;
                    }
                    ImGui::SeparatorText(Header);
                    if (ImGui::BeginTable(Header, 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
                    {
                        ImGui::TableSetupColumn("Name");
                        ImGui::TableSetupColumn("Value");
                        ImGui::TableSetupColumn("Type");
                        ImGui::TableHeadersRow();
                        for (const Lua::FStackVariable& V : Vars)
                        {
                            const bool bExpandable = (V.TypeName == "table") || (V.TypeName == "userdata");
                            DrawExpandableValueRow(
                                FString(V.Name.c_str(), V.Name.size()),
                                V.Name.c_str(),
                                V.Value.c_str(),
                                V.TypeName.c_str(),
                                bExpandable,
                                DebuggerSelectedFrame);
                        }
                        ImGui::EndTable();
                    }
                };

                DrawTable(Frame.Locals,   "Locals");
                DrawTable(Frame.Upvalues, "Upvalues");
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        if (ImGui::BeginChild("##lua_inline_watches", ImVec2(WatchW, Avail.y), ImGuiChildFlags_Borders))
        {
            DrawDebuggerWatchSection();
        }
        ImGui::EndChild();

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void FLuaEditorTool::DrawDebuggerWatchSection()
    {
        if (ImGui::BeginTabBar("##wsh_tabs"))
        {
            if (ImGui::BeginTabItem("Watch"))
            {
                ImGui::TextDisabled("Watch / Eval");
        ImGui::SameLine(0, 12);
        if (ImGui::SmallButton("Refresh##wsh"))
        {
            for (FWatchEntry& W : Watches)
            {
                W.bDirty = true;
            }
            RefreshWatchValues();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##wsh"))
        {
            Watches.clear();
        }
        ImGui::Separator();

        // Add row.
        ImGui::SetNextItemWidth(-60.0f);
        const bool bSubmit = ImGui::InputTextWithHint(
            "##watch_input", "Lua expression e.g. self.Health + 5",
            WatchInputBuffer, sizeof(WatchInputBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        const bool bAdd = ImGui::Button("Add##wsh");
        if ((bSubmit || bAdd) && WatchInputBuffer[0] != '\0')
        {
            FWatchEntry W;
            W.Expression.assign(WatchInputBuffer);
            W.bDirty = true;
            Watches.push_back(eastl::move(W));
            WatchInputBuffer[0] = '\0';
            RefreshWatchValues();
        }

        if (Watches.empty())
        {
            ImGui::TextDisabled("(no watches)");
            ImGui::TextDisabled("Type a Lua expression and press Enter.");
        }
        else if (ImGui::BeginTable("##watch_table", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
        {
            ImGui::TableSetupColumn("Expression");
            ImGui::TableSetupColumn("Value");
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("##rm", ImGuiTableColumnFlags_WidthFixed, 24.0f);
            ImGui::TableHeadersRow();
            for (size_t I = 0; I < Watches.size(); )
            {
                FWatchEntry& W = Watches[I];
                ImGui::PushID((int)I);

                const bool bExpandable = (W.LastType == "table") || (W.LastType == "userdata");
                const bool bError = (W.LastType == "error");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                bool bOpen = false;
                if (bExpandable)
                {
                    bOpen = ImGui::TreeNodeEx(W.Expression.c_str(),
                        ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick);
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", W.Expression.c_str());
                }

                ImGui::TableNextColumn();
                if (bError)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", W.LastValue.c_str());
                }
                else
                {
                    ImGui::TextUnformatted(W.LastValue.c_str());
                }

                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", W.LastType.c_str());

                ImGui::TableNextColumn();
                bool bRemove = false;
                if (ImGui::SmallButton("X##rm"))
                {
                    bRemove = true;
                }

                if (bExpandable && bOpen)
                {
                    TVector<Lua::FChildEntry> Children;
                    Lua::FLuaDebugger::Get().EnumerateChildrenInPausedFrame(
                        DebuggerSelectedFrame,
                        FStringView(W.Expression.c_str(), W.Expression.size()),
                        Children, 256);
                    for (const Lua::FChildEntry& C : Children)
                    {
                        FString ChildPath = W.Expression;
                        ChildPath.append(C.AccessSuffix.c_str(), C.AccessSuffix.size());
                        DrawExpandableValueRow(ChildPath, C.Key.c_str(), C.Value.c_str(),
                            C.TypeName.c_str(), C.bIsExpandable, DebuggerSelectedFrame);
                    }
                    if (Children.empty())
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("(empty)");
                    }
                    ImGui::TreePop();
                }

                ImGui::PopID();

                if (bRemove)
                {
                    Watches.erase(Watches.begin() + I);
                }
                else
                {
                    ++I;
                }
            }
            ImGui::EndTable();
        }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("History"))
            {
                DrawDebuggerBreakHistorySection();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }

    void FLuaEditorTool::DrawDebuggerBreakHistorySection()
    {
        Lua::FLuaDebugger& Debugger = Lua::FLuaDebugger::Get();
        ImGui::TextDisabled("Recent breaks");
        ImGui::SameLine(0, 8);
        if (ImGui::SmallButton("Clear##bh"))
        {
            Debugger.ClearBreakHistory();
        }
        ImGui::Separator();

        const TVector<Lua::FBreakHistoryEntry>& History = Debugger.GetBreakHistory();
        if (History.empty())
        {
            ImGui::TextDisabled("(empty)");
            return;
        }

        if (ImGui::BeginTable("##bh_table", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Line", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();

            int RowIndex = 0;
            for (const Lua::FBreakHistoryEntry& E : History)
            {
                ImGui::PushID(RowIndex++);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                FStringView Src(E.Source.c_str(), E.Source.size());
                FStringView Base = VFS::FileName(Src);
                char Label[256];
                std::snprintf(Label, sizeof(Label), "%.*s :: %s", int(Base.size()), Base.data(),
                    E.FunctionName.empty() ? "?" : E.FunctionName.c_str());
                if (ImGui::Selectable(Label, false, ImGuiSelectableFlags_SpanAllColumns))
                {
                    if (FStringView(E.Source.c_str(), E.Source.size()) == FStringView(VirtualPath.c_str(), VirtualPath.size()))
                    {
                        CodeEditor.SetCursor(E.Line, 0);
                        CodeEditor.ScrollToLine(E.Line, TextEditor::Scroll::alignMiddle);
                    }
                    else if (ToolContext != nullptr)
                    {
                        ToolContext->OpenFileEditor(FStringView(E.Source.c_str(), E.Source.size()));
                    }
                }
                ImGui::TableNextColumn();
                ImGui::Text("%d", E.Line + 1);
                ImGui::TableNextColumn();
                ImGui::TextColored(E.bWasLogPoint ? ImVec4(0.86f, 0.5f, 0.86f, 1.0f) : ImVec4(0.95f, 0.4f, 0.4f, 1.0f),
                    "%s", E.bWasLogPoint ? "log" : "break");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    void FLuaEditorTool::DrawExpandableValueRow(const FString& Path,
                                                 const char* Key,
                                                 const char* Value,
                                                 const char* TypeName,
                                                 bool bIsExpandable,
                                                 int Frame)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        const bool bError = (TypeName != nullptr && std::strcmp(TypeName, "error") == 0);

        bool bOpen = false;
        if (bIsExpandable)
        {
            ImGui::PushID(Path.c_str());
            bOpen = ImGui::TreeNodeEx(Key, ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick);
            ImGui::PopID();
        }
        else
        {
            ImGui::TextUnformatted(Key);
        }

        ImGui::TableNextColumn();
        if (bError)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", Value ? Value : "");
        }
        else
        {
            ImGui::TextUnformatted(Value ? Value : "");
        }

        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", TypeName ? TypeName : "");

        if (bIsExpandable && bOpen)
        {
            TVector<Lua::FChildEntry> Children;
            Lua::FLuaDebugger::Get().EnumerateChildrenInPausedFrame(
                Frame, FStringView(Path.c_str(), Path.size()), Children, 256);

            for (const Lua::FChildEntry& C : Children)
            {
                FString ChildPath = Path;
                ChildPath.append(C.AccessSuffix.c_str(), C.AccessSuffix.size());
                DrawExpandableValueRow(
                    ChildPath, C.Key.c_str(), C.Value.c_str(), C.TypeName.c_str(),
                    C.bIsExpandable, Frame);
            }
            if (Children.empty())
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("(empty)");
            }
            ImGui::TreePop();
        }
    }

    void FLuaEditorTool::RefreshWatchValues()
    {
        Lua::FLuaDebugger& Debugger = Lua::FLuaDebugger::Get();
        if (!Debugger.IsPaused())
        {
            return;
        }

        for (FWatchEntry& W : Watches)
        {
            if (!W.bDirty)
            {
                continue;
            }
            FString Out, Type;
            Debugger.EvaluateInPausedFrame(DebuggerSelectedFrame,
                FStringView(W.Expression.c_str(), W.Expression.size()),
                Out, Type);
            W.LastValue.assign(Out.c_str(), Out.size());
            W.LastType.assign(Type.c_str(), Type.size());
            W.bDirty = false;
        }
    }

    void FLuaEditorTool::HandleEditorShortcuts()
    {
        const ImGuiIO& Io = ImGui::GetIO();
        
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
        {
            const int CurLine = CodeEditor.GetCurrentCursorPosition().line;
            if (Io.KeyCtrl)
            {
                ToggleBookmark(CurLine);
            }
            else
            {
                NavigateBookmark(!Io.KeyShift);
            }
        }

        if (Io.KeyCtrl)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_G, false))
            {
                bRequestOpenGoto = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F10, false))
            {
                RunToCursor();
            }
            // Toggle outline panel (Ctrl+\). Cheap accelerator to peek at the
            // file structure without opening the toolbar popup.
            if (ImGui::IsKeyPressed(ImGuiKey_Backslash, false))
            {
                bShowOutline = !bShowOutline;
            }
        }
    }

    void FLuaEditorTool::DrawInlineValueOverlay()
    {
        Lua::FLuaDebugger& Debugger = Lua::FLuaDebugger::Get();
        const TVector<Lua::FStackFrame>& Stack = Debugger.GetCallStack();
        if (Stack.empty())
        {
            return;
        }
        const int Frame = std::clamp(DebuggerSelectedFrame, 0, (int)Stack.size() - 1);
        const Lua::FStackFrame& F = Stack[Frame];

        // Build a name -> value/type lookup for this frame. Locals shadow
        // upvalues by name when both are present.
        THashMap<FString, const Lua::FStackVariable*> ByName;
        for (const Lua::FStackVariable& V : F.Upvalues)
        {
            ByName[FString(V.Name.c_str(), V.Name.size())] = &V;
        }
        for (const Lua::FStackVariable& V : F.Locals)
        {
            ByName[FString(V.Name.c_str(), V.Name.size())] = &V;
        }
        if (ByName.empty())
        {
            return;
        }

        ImDrawList* DrawList = ImGui::GetWindowDrawList();
        const ImU32 GhostColor = IM_COL32(180, 180, 120, 200);
        const float LineHeight = CodeEditor.GetLineHeight();
        const float GlyphWidth = CodeEditor.GetGlyphWidth();
        if (LineHeight <= 0.0f || GlyphWidth <= 0.0f)
        {
            return;
        }

        const int FirstVisible = CodeEditor.GetFirstVisibleLine();
        const int LastVisible  = std::min(CodeEditor.GetLastVisibleLine(), CodeEditor.GetLineCount() - 1);

        // For each visible line that already references a known local, draw
        // an end-of-line ghost annotation "  name = value". One per line so
        // dense expressions don't drown the gutter.
        for (int LineIdx = FirstVisible; LineIdx <= LastVisible; ++LineIdx)
        {
            const std::string LineText = CodeEditor.GetLineText(LineIdx);
            const int N = (int)LineText.size();
            if (N == 0)
            {
                continue;
            }

            // Find first identifier on this line that matches a known local.
            const Lua::FStackVariable* MatchVar = nullptr;
            int Col = 0;
            int LastIdentCol = 0;
            int I = 0;
            while (I < N)
            {
                const char C = LineText[I];
                if (C == '-' && I + 1 < N && LineText[I + 1] == '-')
                {
                    break; // line comment
                }
                if ((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || C == '_')
                {
                    int Start = I;
                    while (I < N && IsIdentChar(LineText[I]))
                    {
                        ++I;
                    }
                    FString Ident(LineText.data() + Start, I - Start);
                    auto It = ByName.find(Ident);
                    if (It != ByName.end())
                    {
                        MatchVar = It->second;
                        Col = Start;
                        break;
                    }
                    LastIdentCol = I;
                    continue;
                }
                ++I;
            }
            if (MatchVar == nullptr) continue;

            // Position annotation just past the line's last visible glyph.
            // Compute the trimmed length so we don't draw inside trailing
            // whitespace.
            int LastNonWs = N;
            while (LastNonWs > 0 && (LineText[LastNonWs - 1] == ' ' || LineText[LastNonWs - 1] == '\t')) --LastNonWs;

            // Use visible column rather than byte index so tabs are accounted for.
            const int TabSize = CodeEditor.GetTabSize();
            int Visible = 0;
            for (int K = 0; K < LastNonWs; ++K)
            {
                Visible += (LineText[K] == '\t') ? (TabSize - (Visible % TabSize)) : 1;
            }

            char Buf[256];
            std::snprintf(Buf, sizeof(Buf), "  %s = %.80s",
                MatchVar->Name.c_str(), MatchVar->Value.c_str());

            const ImVec2 Pos = CodeEditor.GetScreenPosForCoordinate(LineIdx, Visible);
            DrawList->AddText(Pos, GhostColor, Buf);
            (void)Col; (void)LastIdentCol;
        }
    }

    void FLuaEditorTool::RebuildDocumentOutline()
    {
        DocumentOutline.clear();
        const int LineCount = CodeEditor.GetLineCount();

        bool bInExportsBlock = false;
        for (int LineIdx = 0; LineIdx < LineCount; ++LineIdx)
        {
            const std::string Line = CodeEditor.GetLineText(LineIdx);
            const int N = (int)Line.size();
            int I = 0;
            while (I < N && (Line[I] == ' ' || Line[I] == '\t')) ++I;
            if (I >= N) continue;
            if (I + 1 < N && Line[I] == '-' && Line[I + 1] == '-')
            {
                // Comment header: --- region or -- == note ==. Treat any
                // line of the form "-- ===" or "-- @region" as a marker.
                if (I + 4 < N && (Line[I + 2] == '=' || Line[I + 2] == '#'))
                {
                    FOutlineItem Item;
                    Item.Kind = 'c';
                    Item.Line = LineIdx;
                    int Cstart = I + 2;
                    while (Cstart < N && (Line[Cstart] == ' ' || Line[Cstart] == '=' || Line[Cstart] == '#')) ++Cstart;
                    int Cend = N;
                    while (Cend > Cstart && (Line[Cend - 1] == ' ' || Line[Cend - 1] == '=' || Line[Cend - 1] == '#')) --Cend;
                    Item.Name.assign(Line.data() + Cstart, Cend - Cstart);
                    Item.Detail.assign("region");
                    DocumentOutline.push_back(eastl::move(Item));
                }
                continue;
            }

            // function NAME(...)
            // local function NAME(...)
            // function OBJ:METHOD(...)
            const int Indent = I;
            int J = I;
            bool bIsLocal = false;
            const char* kLocal = "local";
            const int kLocalLen = 5;
            if (J + kLocalLen + 1 <= N && std::memcmp(Line.data() + J, kLocal, kLocalLen) == 0
                && (Line[J + kLocalLen] == ' ' || Line[J + kLocalLen] == '\t'))
            {
                bIsLocal = true;
                J += kLocalLen;
                while (J < N && (Line[J] == ' ' || Line[J] == '\t')) ++J;
            }

            const char* kFunction = "function";
            const int kFunctionLen = 8;
            if (J + kFunctionLen <= N && std::memcmp(Line.data() + J, kFunction, kFunctionLen) == 0
                && (J + kFunctionLen >= N || !IsIdentChar(Line[J + kFunctionLen])))
            {
                int K = J + kFunctionLen;
                while (K < N && (Line[K] == ' ' || Line[K] == '\t')) ++K;

                // Read until '(' as the qualified name.
                int NameStart = K;
                while (K < N && Line[K] != '(' && Line[K] != ' ') ++K;
                int NameEnd = K;
                if (NameEnd > NameStart)
                {
                    int ParamEnd = K;
                    while (ParamEnd < N && Line[ParamEnd] != ')') ++ParamEnd;
                    int ParamStart = K;

                    FOutlineItem Item;
                    Item.Kind = 'f';
                    Item.Line = LineIdx;
                    Item.Indent = Indent;
                    Item.Name.assign(Line.data() + NameStart, NameEnd - NameStart);
                    if (ParamEnd > ParamStart)
                    {
                        Item.Detail.assign(Line.data() + ParamStart, ParamEnd - ParamStart + 1);
                    }
                    if (bIsLocal) Item.Detail.append(" [local]");
                    DocumentOutline.push_back(eastl::move(Item));
                    continue;
                }
            }

            // local NAME (not a function form)
            if (bIsLocal)
            {
                int K = J;
                int NameStart = K;
                while (K < N && IsIdentChar(Line[K])) ++K;
                int NameEnd = K;
                if (NameEnd > NameStart)
                {
                    FOutlineItem Item;
                    Item.Kind = 'l';
                    Item.Line = LineIdx;
                    Item.Indent = Indent;
                    Item.Name.assign(Line.data() + NameStart, NameEnd - NameStart);

                    // Trim trailing whitespace and capture initializer hint.
                    int Init = K;
                    while (Init < N && (Line[Init] == ' ' || Line[Init] == '\t')) ++Init;
                    if (Init < N && Line[Init] == ':')
                    {
                        int TStart = Init + 1;
                        while (TStart < N && (Line[TStart] == ' ' || Line[TStart] == '\t')) ++TStart;
                        int TEnd = TStart;
                        while (TEnd < N && Line[TEnd] != '=' && Line[TEnd] != ',' && Line[TEnd] != ')') ++TEnd;
                        while (TEnd > TStart && (Line[TEnd - 1] == ' ' || Line[TEnd - 1] == '\t')) --TEnd;
                        if (TEnd > TStart)
                        {
                            Item.Detail.append(": ");
                            Item.Detail.append(Line.data() + TStart, TEnd - TStart);
                        }
                    }
                    DocumentOutline.push_back(eastl::move(Item));
                    continue;
                }
            }

            // Capture event handler-style top-level functions written as
            // `OnXxx = function(...)` for export-table entries; rough but
            // useful for users who define exports inline.
            int IdentStart = I;
            int K = I;
            while (K < N && IsIdentChar(Line[K])) ++K;
            if (K > IdentStart)
            {
                int Tail = K;
                while (Tail < N && (Line[Tail] == ' ' || Line[Tail] == '\t')) ++Tail;
                if (Tail < N && Line[Tail] == '=')
                {
                    // X = function(... pattern
                    int Eq = Tail + 1;
                    while (Eq < N && (Line[Eq] == ' ' || Line[Eq] == '\t')) ++Eq;
                    if (Eq + kFunctionLen <= N
                        && std::memcmp(Line.data() + Eq, kFunction, kFunctionLen) == 0
                        && (Eq + kFunctionLen == N || Line[Eq + kFunctionLen] == '(' || Line[Eq + kFunctionLen] == ' '))
                    {
                        FOutlineItem Item;
                        Item.Kind = bInExportsBlock ? 'e' : 'f';
                        Item.Line = LineIdx;
                        Item.Indent = Indent;
                        Item.Name.assign(Line.data() + IdentStart, K - IdentStart);
                        Item.Detail.assign("function");
                        DocumentOutline.push_back(eastl::move(Item));
                        continue;
                    }
                }
            }
        }
    }

    void FLuaEditorTool::DrawOutlinePanel()
    {
        const ImVec2 Avail = ImGui::GetContentRegionAvail();
        if (!ImGui::BeginChild("##lua_outline", ImVec2(Avail.x, Avail.y), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding))
        {
            ImGui::EndChild();
            return;
        }

        ImGui::TextDisabled(LE_ICON_FORMAT_LIST_BULLETED " Outline");
        ImGui::SameLine(0, 8);
        if (ImGui::SmallButton("Refresh##ol"))
        {
            RebuildDocumentOutline();
        }
        ImGui::Separator();

        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##ol_filter", LE_ICON_MAGNIFY " filter", OutlineFilterBuffer, sizeof(OutlineFilterBuffer));
        ImGui::Separator();

        if (DocumentOutline.empty())
        {
            ImGui::TextDisabled("(no symbols found)");
        }
        else
        {
            const FStringView Filter(OutlineFilterBuffer, std::strlen(OutlineFilterBuffer));
            for (const FOutlineItem& Item : DocumentOutline)
            {
                if (!Filter.empty())
                {
                    // Case-insensitive substring filter on the name.
                    FString Lower(Item.Name.c_str(), Item.Name.size());
                    for (char& C : Lower) C = (char)std::tolower((unsigned char)C);
                    FString FLow(Filter.data(), Filter.size());
                    for (char& C : FLow) C = (char)std::tolower((unsigned char)C);
                    if (Lower.find(FLow) == FString::npos) continue;
                }

                ImVec4 KindColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                const char* Glyph = "*";
                switch (Item.Kind)
                {
                case 'f': KindColor = ImVec4(0.86f, 0.71f, 0.35f, 1.0f); Glyph = "fn"; break;
                case 'l': KindColor = ImVec4(0.55f, 0.85f, 0.6f,  1.0f); Glyph = "lo"; break;
                case 'e': KindColor = ImVec4(0.65f, 0.85f, 1.0f,  1.0f); Glyph = "ex"; break;
                case 'c': KindColor = ImVec4(0.6f,  0.6f,  0.7f,  1.0f); Glyph = LE_ICON_FORMAT_LIST_BULLETED; break;
                }

                ImGui::PushID(Item.Line);
                ImGui::TextColored(KindColor, "%s", Glyph);
                ImGui::SameLine();
                char Label[256];
                std::snprintf(Label, sizeof(Label), "%s##ol%d", Item.Name.c_str(), Item.Line);
                if (ImGui::Selectable(Label, false))
                {
                    CodeEditor.SetCursor(Item.Line, 0);
                    CodeEditor.ScrollToLine(Item.Line, TextEditor::Scroll::alignMiddle);
                    CodeEditor.SetFocus();
                }
                if (!Item.Detail.empty() && ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::TextDisabled("Line %d", Item.Line + 1);
                    ImGui::TextUnformatted(Item.Detail.c_str());
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
        }

        ImGui::EndChild();
    }

    void FLuaEditorTool::DrawBreakpointSettingsPopup()
    {
        if (RequestedBreakpointSettingsLine < 0) return;

        const int Line = RequestedBreakpointSettingsLine;
        if (!ImGui::IsPopupOpen("##lua_bp_settings"))
        {
            ImGui::OpenPopup("##lua_bp_settings");
        }

        if (!ImGui::BeginPopup("##lua_bp_settings"))
        {
            return;
        }

        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), LE_ICON_BUG " Configure breakpoint - line %d", Line + 1);
        ImGui::Separator();

        ImGui::Checkbox("Enabled", &bBpEnabled);
        ImGuiX::TextTooltip("Disabled breakpoints stay in the file but don't pause execution.");

        ImGui::Spacing();
        ImGui::TextDisabled("Condition (Lua expression, pause only when truthy)");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##bp_cond", "e.g. self.Health < 10", BpConditionBuffer, sizeof(BpConditionBuffer));

        ImGui::Spacing();
        ImGui::TextDisabled("Log message (log point: logs and continues)");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##bp_log", "e.g. health=<braces>self.Health<braces> ...", BpLogMessageBuffer, sizeof(BpLogMessageBuffer));
        // Tooltip uses a runtime-formatted string. TextTooltip's compile-time
        // format validation rejects `{expr}` placeholders in literals, so we
        // build the message at runtime instead.
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Use {expr} to splice evaluated values into the message.");
            ImGui::TextUnformatted("Leave empty to use as a normal/conditional breakpoint.");
            ImGui::EndTooltip();
        }

        ImGui::Spacing();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("Skip first N hits", &BpIgnoreCount);
        if (BpIgnoreCount < 0) BpIgnoreCount = 0;

        // Live-stats display for the user.
        const Lua::FBreakpointSettings* Live = Lua::FLuaDebugger::Get().GetBreakpointSettings(
            FStringView(VirtualPath.c_str(), VirtualPath.size()), Line);
        if (Live != nullptr)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("Hit count this session: %u", Live->HitCount);
        }

        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::Button("Apply", ImVec2(120, 0)))
        {
            Lua::FLuaDebugger& D = Lua::FLuaDebugger::Get();
            const FStringView Path(VirtualPath.c_str(), VirtualPath.size());
            D.SetBreakpointEnabled(Path, Line, bBpEnabled);
            D.SetBreakpointCondition(Path, Line, FStringView(BpConditionBuffer, std::strlen(BpConditionBuffer)));
            D.SetBreakpointLogMessage(Path, Line, FStringView(BpLogMessageBuffer, std::strlen(BpLogMessageBuffer)));
            D.SetBreakpointIgnoreCount(Path, Line, (uint32)std::max(0, BpIgnoreCount));
            RefreshBreakpointMarkers();
            RequestedBreakpointSettingsLine = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            RequestedBreakpointSettingsLine = -1;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();

        // Popup got closed by clicking outside. clear so we don't keep
        // opening it on subsequent frames.
        if (!ImGui::IsPopupOpen("##lua_bp_settings"))
        {
            RequestedBreakpointSettingsLine = -1;
        }
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

            // Selection length (chars) when there is one.
            if (CodeEditor.AnyCursorHasSelection())
            {
                const TextEditor::CursorSelection Sel = CodeEditor.GetMainCursorSelection();
                const std::string SelText = CodeEditor.GetSectionText(Sel.start.line, Sel.start.column, Sel.end.line, Sel.end.column);
                const int SelLines = (Sel.end.line - Sel.start.line) + 1;
                ImGui::SameLine(0, 12);
                ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.45f, 1.0f), "(sel %zu chars / %d lines)", SelText.size(), SelLines);
            }

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

            ImGui::SameLine(0, 20);
            ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.85f, 1.0f), "%s : %d",
                bInsertSpacesOnTabs ? "Spaces" : "Tabs", EditorTabSize);
            ImGuiX::TextTooltip("Indent mode and tab size. Toggle in Settings.");

            if (!Breakpoints.empty())
            {
                ImGui::SameLine(0, 20);
                ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), LE_ICON_BUG " %zu", Breakpoints.size());
                ImGuiX::TextTooltip("Active breakpoints in this file.");
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

            if (bHasCompileError)
            {
                ImGui::SameLine(0, 20);
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                    "Compile error (Ln %d): %s",
                    CompileErrorLine,
                    CompileErrorMessage.c_str());
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }


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
