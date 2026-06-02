#include "LuaEditorTool.h"

#include "Config/Config.h"
#include "Core/Delegates/CoreDelegates.h"
#include "Core/Object/ObjectCore.h"
#include "Settings/EditorSettings.h"
#include "FileSystem/FileSystem.h"
#include "Log/Log.h"
#include "Scripting/Lua/Scripting.h"
#include "Scripting/Lua/Debugger/LuaDebugger.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"
#include "Tools/UI/ImGui/ImGuiFonts.h"
#include "Tools/UI/ImGui/ImGuiX.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

#include "UI/Tools/EditorToolContext.h"

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

        // Pull persisted preferences from the developer-settings object.
        const CLuaEditorSettings* Settings = GetDefault<CLuaEditorSettings>();
        EditorFontScale         = Settings->FontScale;
        EditorTabSize           = std::max(1, std::min(8, Settings->TabSize));
        EditorLineSpacing       = Settings->LineSpacing;
        bEditorShowWhitespace   = Settings->bShowWhitespace;
        bEditorShowLineNumbers  = Settings->bShowLineNumbers;
        bEditorShowMiniMap      = Settings->bShowMiniMap;
        bAutoIndent             = Settings->bAutoIndent;
        bShowMatchingBrackets   = Settings->bMatchBrackets;
        bCompletePairedGlyphs   = Settings->bCompletePairs;
        bInsertSpacesOnTabs     = Settings->bInsertSpacesOnTabs;
        bTrimTrailingOnSave     = Settings->bTrimTrailingOnSave;
        bAutoTriggerCompletion  = Settings->bAutoTriggerCompletion;
        AutoTriggerDelayMs      = std::max(0, std::min(2000, Settings->AutoTriggerDelayMs));
        EditorPalette = (Settings->Palette == "Light") ? EPalette::Light : EPalette::Dark;
    }

    void FLuaEditorTool::OnInitialize()
    {
        FAssetEditorTool::OnInitialize();

        CodeEditor.SetLanguage(TextEditor::Language::Luau());
        ApplyEditorSettings();
        
        TypeContext = MakeUnique<FLuaTypeContext>(FStringView(VirtualPath.c_str(), VirtualPath.size()));
        RebuildSymbolIndex();

        LoadFromDisk(); // RefreshAnalysis runs inside on cold load

        Lua::FScriptingContext& SC = Lua::FScriptingContext::Get();
        CompileErrorHandle = SC.OnScriptCompileError.AddLambda([this](FStringView Path, const Lua::FCompileDiagnostic& Diag)
        {
            if (Path == FStringView(VirtualPath.c_str(), VirtualPath.size()))
            {
                ApplyCompileError(Diag.Line, Diag.Message);
            }
        });
        
        CompileSuccessHandle = SC.OnScriptCompileSuccess.AddLambda([this](FStringView Path)
        {
            if (Path == FStringView(VirtualPath.c_str(), VirtualPath.size()))
            {
                ClearCompileError();
            }
        });
        
        ScriptLoadedHandle = SC.OnScriptLoaded.AddLambda([this](FStringView Path)
        {
            if (Path != FStringView(VirtualPath.c_str(), VirtualPath.size()))
            {
                return;
            }
            if (bIgnoreNextReload)
            {
                bIgnoreNextReload = false;
                return;
            }
            bExternalChangePending = true;
        });

        // Retarget our path when the file is renamed/moved so a later save hits the new file.
        FileRenamedHandle = FCoreDelegates::OnContentFileRenamed.AddLambda([this](FStringView Old, FStringView New)
        {
            if (Old != FStringView(VirtualPath.c_str(), VirtualPath.size()))
            {
                return;
            }
            VirtualPath.assign(New.data(), New.size());
            const FStringView ParentView = VFS::Parent(New, true);
            ParentDir.assign(ParentView.data(), ParentView.size());
        });

        // Runtime component type created/removed (or any global change) -> re-harvest symbols. Mark
        // dirty here; Update coalesces bursts into a single rebuild.
        GlobalsChangedHandle = SC.OnGlobalsChanged.AddLambda([this]
        {
            bSymbolsDirty = true;
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
            bBufferDirty = (CodeEditor.GetUndoIndex() != LastSyncedUndoIndex);
            
            const std::string Body = CodeEditor.GetText();
            RefreshAnalysis(FStringView(Body.data(), Body.size()));
        }, /*delay ms*/ 100);

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
            
            DrawFreeFormHoverTooltip();
            
            if (bPausedHere && bShowInlineValuesWhilePaused)
            {
                DrawInlineValueOverlay();
            }

            ImGui::PopFontSize();
            ImGuiX::Font::PopFont();
            
            if (bShowOutline)
            {
                ImGui::SameLine();
                DrawOutlinePanel();
            }
            
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
        Lua::FScriptingContext& SC = Lua::FScriptingContext::Get();
        SC.OnScriptCompileError.Remove(CompileErrorHandle);
        SC.OnScriptCompileSuccess.Remove(CompileSuccessHandle);
        SC.OnScriptLoaded.Remove(ScriptLoadedHandle);
        SC.OnGlobalsChanged.Remove(GlobalsChangedHandle);
        FCoreDelegates::OnContentFileRenamed.Remove(FileRenamedHandle);
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

        // Coalesce global-set changes (e.g. component types created/removed) into one re-harvest.
        if (bSymbolsDirty)
        {
            bSymbolsDirty = false;
            RebuildSymbolIndex();
            RefreshAnalysis();
        }

        if (bExternalChangePending)
        {
            bExternalChangePending = false;
            if (!bBufferDirty)
            {
                LoadFromDisk();
            }
            else
            {
                LOG_WARN("[LuaEditor] '{}' changed on disk but buffer is dirty; ignoring.", VirtualPath.c_str());
            }
        }
        
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

    void FLuaEditorTool::DrawHelpMenu()
    {
        DrawHelpTextRow("API Reference",
            "Tools > Debug > Scripts Info > API Reference lists every class, function and value Lua sees. "
            "Hover a name there for its description; the list refreshes from the live VM.");
        DrawHelpTextRow("Autocomplete",
            "Triggers on '.' / ':' or after a brief pause while typing. Pulls members from the live VM "
            "globals plus locals declared in this buffer. Tab accepts.");
        DrawHelpTextRow("Hover Tooltips",
            "Hover an identifier for its kind, type, and (when known) signature. Locals show their "
            "declared annotation; globals include description text harvested from C++.");
        DrawHelpTextRow("Breakpoints",
            "Click the gutter to toggle. Right-click 'Configure...' for conditional / log-only / hit-count "
            "breakpoints. F5 continues, F10 step over, F11 step into.");
        DrawHelpTextRow("Hot Reload",
            "Saving recompiles immediately. The runtime VM picks up the change on its next instantiation; "
            "stdlib reload is exposed as Engine.ReloadStdlib() in Lua.");
        DrawHelpTextRow("Stdlib",
            "EntityScript, Random, Color, Tween are preloaded as globals, no `require` needed. "
            "Source lives under VFS Stdlib/.");
        DrawHelpTextRow("Bookmarks",
            "F2 toggles a bookmark on the current line. Shift+F2 cycles between them. Session-only.");
        DrawHelpTextRow("Snippets / Format",
            "Ctrl+Shift+P opens snippets; Format pretty-prints the buffer. Both available from the toolbar.");
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
        LastSyncedUndoIndex = CodeEditor.GetUndoIndex();
        CachedBodySize = Body.size();
        bBufferDirty = false;
        
        RefreshAnalysis(FStringView(Body.data(), Body.size()));
        
        bIgnoreNextReload = true;
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
        
        if (Body.size() == LastSyncedText.size()
            && std::memcmp(Body.data(), LastSyncedText.data(), Body.size()) == 0)
        {
            bBufferDirty = false;
            return;
        }

        const std::string_view View(Body.c_str(), Body.size());
        CodeEditor.SetText(View);
        LastSyncedText.assign(Body.c_str(), Body.size());
        LastSyncedUndoIndex = CodeEditor.GetUndoIndex();
        bBufferDirty = false;
        // RefreshAnalysis sets CachedBodySize, runs lint, refreshes markers.
        RefreshAnalysis(FStringView(Body.data(), Body.size()));
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
        TVector<int> Sorted(Bookmarks.begin(), Bookmarks.end());
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
        
        if (TypeContext && !TopLevelSymbols.empty())
        {
            TVector<FString> Names;
            Names.reserve(TopLevelSymbols.size());
            for (const Lua::FLuaSymbol& Top : TopLevelSymbols)
            {
                Names.emplace_back(Top.Name.c_str(), Top.Name.size());
            }
            TypeContext->RegisterEngineSymbols(Names);
        }
    }

    void FLuaEditorTool::RefreshAnalysis()
    {
        const std::string Body = CodeEditor.GetText();
        RefreshAnalysis(FStringView(Body.data(), Body.size()));
    }

    void FLuaEditorTool::RefreshAnalysis(FStringView Body)
    {
        CachedBodySize = Body.size();
        
        constexpr size_t kAnalysisByteCap = 256 * 1024;
        if (Body.size() > kAnalysisByteCap)
        {
            DocumentOutline.clear();
            Locals.clear();
            LintWarnings.clear();
            TypeErrors.clear();
            HoverTypeCache = {};
            HighlightedReferences.clear();
            RefreshBreakpointMarkers();
            return;
        }
        
        AstAnalyzer.Parse(Body);
        RebuildLocalIndex();
        RebuildDocumentOutline();
        AstAnalyzer.RunLint(LintWarnings);
        
        if (!TypeContext)
        {
            TypeContext = MakeUnique<FLuaTypeContext>(
                FStringView(VirtualPath.c_str(), VirtualPath.size()));
        }
        TypeContext->SetSource(Body);

        HoverTypeCache = {};
        
        TypeContext->GetTypeErrors(TypeErrors);

        HighlightedReferences.clear();

        RefreshBreakpointMarkers();
    }

    void FLuaEditorTool::RebuildLocalIndex()
    {
        Locals.clear();
        if (AstAnalyzer.IsValid())
        {
            TVector<FLuaAstLocalEntry> Entries;
            AstAnalyzer.CollectLocals(Entries);
            for (const FLuaAstLocalEntry& E : Entries)
            {
                FLocalDecl D;
                D.Line = E.Line - 1; // legacy zero-based
                if (!E.TypeAnnotation.empty())
                {
                    D.TypeAnnotation.assign(E.TypeAnnotation.c_str(), E.TypeAnnotation.size());
                }
                if (!E.OriginName.empty())
                {
                    D.OriginName.assign(E.OriginName.c_str(), E.OriginName.size());
                    auto It = Locals.find(FString(E.OriginName.c_str(), E.OriginName.size()));
                    if (It != Locals.end() && D.TypeAnnotation.empty())
                    {
                        D.TypeAnnotation = It->second.TypeAnnotation;
                        D.ValueHint      = It->second.ValueHint;
                    }
                }
                Locals[FString(E.Name.c_str(), E.Name.size())] = D;
            }
        }
    }


    void FLuaEditorTool::DrawFreeFormHoverTooltip()
    {
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

        RefreshLineCache();
        if (Line >= static_cast<int>(CachedLines.size())) return;
        const std::string& LineText = CachedLines[Line];

        // Tabs occupy 1..tabSize visual columns; naive Col-as-index skews on tab-leading lines.
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
            // Body excludes quotes; raw source bytes (\n = 2 bytes in source, 1 at runtime).
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
        FString TypeText;
        if (TypeContext && !IsDebuggerPausedHere())
        {
            const TextEditor::CursorPosition HoverPos = CodeEditor.GetCurrentCursorPosition();
            const int Line1 = HoverPos.line + 1;
            const int Col1  = HoverPos.column + 1;
            if (HoverTypeCache.bChecked
                && HoverTypeCache.Line == Line1
                && HoverTypeCache.Column == Col1)
            {
                TypeText = HoverTypeCache.Text;
            }
            else
            {
                FString Resolved;
                if (TypeContext->GetTypeAt(Line1, Col1, Resolved) && Resolved != "any")
                {
                    TypeText = Resolved;
                }
                HoverTypeCache.Line     = Line1;
                HoverTypeCache.Column   = Col1;
                HoverTypeCache.Text     = TypeText;
                HoverTypeCache.bChecked = true;
            }
        }

        // Registered doc (e.g. from .AddComment) for the hovered symbol, if any.
        FString DocText;
        if (TypeContext && !IsDebuggerPausedHere())
        {
            const TextEditor::CursorPosition DocPos = CodeEditor.GetCurrentCursorPosition();
            TypeContext->GetDocAt(DocPos.line + 1, DocPos.column + 1, DocText);
        }

        if (IsDebuggerPausedHere())
        {
            Lua::FLuaDebugger& Debugger = Lua::FLuaDebugger::Get();
            const TVector<Lua::FStackFrame>& Stack = Debugger.GetCallStack();
            if (!Stack.empty())
            {
                const int Frame = std::clamp(DebuggerSelectedFrame, 0, (int)Stack.size() - 1);
                
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
        
        if (Word == DottedPath)
        {
            const FString WordKey(Word.c_str(), Word.size());
            auto LItr = Locals.find(WordKey);
            if (LItr != Locals.end())
            {
                const FLocalDecl& L = LItr->second;
                BeginTranslucentTooltip();
                if (!TypeText.empty())
                {

                    ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "(local) %s: %s", Word.c_str(), TypeText.c_str());
                    ImGui::TextDisabled("inferred type");
                }
                else if (!L.TypeAnnotation.empty())
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
        
        const FString FullKey(DottedPath.c_str(), DottedPath.size());
        auto Itr = SymbolByPath.find(FullKey);
        if (Itr == SymbolByPath.end() && Word != DottedPath)
        {
            const FString WordKey(Word.c_str(), Word.size());
            Itr = SymbolByPath.find(WordKey);
        }
        if (Itr == SymbolByPath.end())
        {
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

            // Not a harvested symbol (e.g. World.Physics:AddForce resolves through the runtime
            // metatable, so it's never harvested) -- but Luau still has its type, and the registry
            // may have a doc comment for it. Surface those instead of an empty hover.
            if (!TypeText.empty() || !DocText.empty())
            {
                BeginTranslucentTooltip();
                if (!DottedPath.empty())
                {
                    ImGui::TextColored(ImVec4(0.86f, 0.71f, 0.35f, 1.0f), "%s", DottedPath.c_str());
                    ImGui::Separator();
                }
                if (!TypeText.empty())
                {
                    ImGui::PushTextWrapPos(420.0f);
                    ImGui::TextDisabled("type:");
                    ImGui::TextUnformatted(TypeText.c_str());
                    ImGui::PopTextWrapPos();
                }
                if (!DocText.empty())
                {
                    if (!TypeText.empty()) ImGui::Spacing();
                    ImGui::PushTextWrapPos(420.0f);
                    ImGui::TextColored(ImVec4(0.78f, 0.85f, 0.72f, 1.0f), "%s", DocText.c_str());
                    ImGui::PopTextWrapPos();
                }
                EndTranslucentTooltip();
            }
            return;
        }

        const Lua::FLuaSymbol& Symbol = Itr->second;

        BeginTranslucentTooltip();
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

        if (!TypeText.empty())
        {
            ImGui::PushTextWrapPos(420.0f);
            ImGui::TextDisabled("inferred type:");
            ImGui::TextUnformatted(TypeText.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
        }

        if (!DocText.empty())
        {
            ImGui::PushTextWrapPos(420.0f);
            ImGui::TextColored(ImVec4(0.78f, 0.85f, 0.72f, 1.0f), "%s", DocText.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
        }

        if (Symbol.Kind == Lua::ELuaSymbolKind::Function)
        {
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
                        if (I > 0)
                        {
                            Out.append(", ");
                        }
                        Out.append(Symbol.ParamNames[I].c_str(), Symbol.ParamNames[I].size());
                    }
                    if (Symbol.bIsVararg)
                    {
                        if (!Symbol.ParamNames.empty())
                        {
                            Out.append(", ");
                        }
                        Out.append("...");
                    }
                }
                else if (Symbol.bIsCFunction)
                {
                    Out.append("...");
                }
                else
                {
                    for (uint8 I = 0; I < Symbol.ParamCount; ++I)
                    {
                        if (I > 0)
                        {
                            Out.append(", ");
                        }
                        char Buf[16];
                        std::snprintf(Buf, sizeof(Buf), "arg%u", unsigned(I + 1));
                        Out.append(Buf);
                    }
                    if (Symbol.bIsVararg)
                    {
                        if (Symbol.ParamCount > 0)
                        {
                            Out.append(", ");
                        }
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

        const std::string& Term = State.searchTerm;
        
        TVector<FLuaTypedCompletion> TypedEntries;
        if (TypeContext)
        {
            const std::string Body = CodeEditor.GetText();
            TypeContext->SetSource(FStringView(Body.data(), Body.size()));
            const int Line1 = static_cast<int>(State.line) + 1;
            const int Col1  = static_cast<int>(State.searchTermEndIndex) + 1;
            TypeContext->Autocomplete(Line1, Col1, TypedEntries);
        }
        
        const std::string Line = CodeEditor.GetLineText(static_cast<int>(State.line));
        const FString OwnerPath = ResolveOwnerPath(Line, static_cast<int>(State.searchTermStartIndex));
        
        auto MatchScore = [&](FStringView Candidate) -> int
        {
            if (Term.empty())
            {
                return 1; // everything is a match while empty
            }
            if (Candidate.size() < Term.size())
            {
                return -1;
            }
            bool bCaseSens = true;
            for (size_t I = 0; I < Term.size(); ++I)
            {
                const char C = Candidate[I];
                const char T = Term[I];
                if (C != T) bCaseSens = false;
                if ((char)std::tolower((unsigned char)C) != (char)std::tolower((unsigned char)T))
                {
                    return -1;
                }
            }
            return bCaseSens ? 2 : 1;
        };
        
        enum ETier : int
        {
            ETier_Local   = 9,
            ETier_Property= 8,
            ETier_Field   = 7,
            ETier_Engine  = 6,
            ETier_Type    = 5,
            ETier_Module  = 5,
            ETier_Keyword = 3,
            ETier_String  = 2,
            ETier_Buffer  = 1,
        };

        struct FCandidate
        {
            std::string Name;
            std::string Detail;
            char        Kind = 'p';
            int         Rank = 0;
        };
        TVector<FCandidate> Candidates;
        Candidates.reserve(64);

        THashSet<FString> Seen;
        auto Add = [&](const char* Name, size_t NameLen, std::string Detail, char Kind, int Tier)
        {
            const FString Key(Name, NameLen);
            if (!Seen.insert(Key).second) return;
            const int Match = MatchScore(FStringView(Name, NameLen));
            if (Match < 0) return;
            
            const int LengthBonus = (NameLen <= 30) ? int(30 - NameLen) : 0;
            const int Rank = Match * 10000 + Tier * 100 + LengthBonus;

            FCandidate C;
            C.Name.assign(Name, NameLen);
            C.Detail = std::move(Detail);
            C.Kind   = Kind;
            C.Rank   = Rank;
            Candidates.push_back(Move(C));
        };
        
        auto TypedTier = [](char K) -> int
        {
            switch (K)
            {
            case 'b': return ETier_Local;
            case 'p': return ETier_Property;
            case 't': return ETier_Type;
            case 'm': return ETier_Module;
            case 'k': return ETier_Keyword;
            case 's': return ETier_String;
            case 'f': return ETier_Property; // typed GeneratedFunction
            default:  return ETier_Property;
            }
        };

        // Member-access path: "Engine.VFS." or "Engine.VFS:". Only suggest
        // children of the resolved owner; nothing else makes sense here.
        if (!OwnerPath.empty())
        {
            for (const FLuaTypedCompletion& C : TypedEntries)
            {
                Add(C.Name.c_str(), C.Name.size(),
                    std::string(C.Detail.c_str(), C.Detail.size()),
                    C.Kind, TypedTier(C.Kind));
            }

            // Resolve the owner expression to a global symbol path: direct
            // hit (`Engine.X`), then via local annotation, then via `self`.
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
                        if (FuncPos == std::string::npos)
                        {
                            continue;
                        }

                        size_t Start = FuncPos + 9;
                        while (Start < Prev.size() && (Prev[Start] == ' ' || Prev[Start] == '\t'))
                        {
                            ++Start;
                        }
                        size_t End = Start;
                        while (End < Prev.size() && IsIdentChar(Prev[End]))
                        {
                            ++End;
                        }
                        if (End == Start || End >= Prev.size() || Prev[End] != ':')
                        {
                            continue;
                        }

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

            // Skip underscore-prefixed members (_Shape, __index are metadata slots, not user API).
            if (Itr != SymbolsByPath.end())
            {
                for (const Lua::FLuaSymbol& Symbol : Itr->second)
                {
                    if (!Symbol.Name.empty() && Symbol.Name[0] == '_') continue;
                    Add(Symbol.Name.c_str(), Symbol.Name.size(),
                        BuildDetail(Symbol), KindBadge(Symbol.Kind), ETier_Engine);
                }
            }

            // _Shape table holds per-instance runtime fields not visible to the type checker.
            if (!ResolvedType.empty())
            {
                FString ShapePath = ResolvedType;
                ShapePath += "._Shape";
                auto ShapeItr = SymbolsByPath.find(ShapePath);
                if (ShapeItr != SymbolsByPath.end())
                {
                    for (const Lua::FLuaSymbol& Field : ShapeItr->second)
                    {
                        std::string Detail = Field.ValuePreview.empty()
                            ? std::string("field")
                            : std::string(Field.ValuePreview.c_str(), Field.ValuePreview.size());
                        Add(Field.Name.c_str(), Field.Name.size(),
                            std::move(Detail), 'v', ETier_Field);
                    }
                }
            }
        }
        else
        {
            // Top-level: typed entries + keywords + engine globals + buffer.
            for (const FLuaTypedCompletion& C : TypedEntries)
            {
                Add(C.Name.c_str(), C.Name.size(),
                    std::string(C.Detail.c_str(), C.Detail.size()),
                    C.Kind, TypedTier(C.Kind));
            }
            for (const char* Keyword : kLuauKeywords)
            {
                Add(Keyword, std::strlen(Keyword), "keyword", 'k', ETier_Keyword);
            }
            for (const Lua::FLuaSymbol& Top : TopLevelSymbols)
            {
                Add(Top.Name.c_str(), Top.Name.size(), BuildDetail(Top), KindBadge(Top.Kind), ETier_Engine);
            }
            CodeEditor.IterateIdentifiers([&](const std::string& Identifier)
            {
                if (Identifier == Term)
                {
                    return;
                }
                Add(Identifier.c_str(), Identifier.size(),
                    "identifier", 'i', ETier_Buffer);
            });
        }
        
        std::sort(Candidates.begin(), Candidates.end(),
            [](const FCandidate& A, const FCandidate& B)
            {
                if (A.Rank != B.Rank)
                {
                    return A.Rank > B.Rank;
                }
                return A.Name < B.Name;
            });

        State.suggestions.reserve(Candidates.size());
        State.suggestionKinds.reserve(Candidates.size());
        State.suggestionDetails.reserve(Candidates.size());
        for (FCandidate& C : Candidates)
        {
            State.suggestions.push_back(std::move(C.Name));
            State.suggestionKinds.push_back(C.Kind);
            State.suggestionDetails.push_back(std::move(C.Detail));
        }
    }

    void FLuaEditorTool::RefreshBreakpointMarkers()
    {
        // AddMarker is additive; clear-and-readd to sync breakpoint set with live PC.
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

        // PC arrow only when debugger source matches this editor's path; last AddMarker call wins per line.
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

        // Compile-error stripe drawn last so it dominates breakpoints on the same line. Line is 1-based from Luau.
        if (bHasCompileError && CompileErrorLine >= 1 && CompileErrorLine <= CodeEditor.GetLineCount())
        {
            const ImU32 ErrorCol  = IM_COL32(255, 80, 80, 255);
            const ImU32 ErrorFill = IM_COL32(255, 80, 80, 70);
            char Tip[512];
            std::snprintf(Tip, sizeof(Tip), "Compile error: %s", CompileErrorMessage.c_str());
            CodeEditor.AddMarker(CompileErrorLine - 1, ErrorCol, ErrorFill, "Compile error", Tip);
        }

        // Type errors: skipped on compile-error line; multiple errors per line collapse into one marker.
        if (!TypeErrors.empty())
        {
            const ImU32 TypeErrCol  = IM_COL32(255, 110, 110, 255);
            const ImU32 TypeErrFill = IM_COL32(255, 110, 110, 55);
            const int LineCount = CodeEditor.GetLineCount();

            // Group by line so each line gets one marker with a merged tip.
            THashMap<int, FString> ByLine;
            for (const FLuaTypeDiagnostic& E : TypeErrors)
            {
                if (E.Line < 1 || E.Line > LineCount) continue;
                if (bHasCompileError && E.Line == CompileErrorLine) continue;

                FString& Tip = ByLine[E.Line];
                if (!Tip.empty()) Tip.append("\n\n");
                Tip.append(E.Message.c_str(), E.Message.size());
            }
            for (auto& [Line, Tip] : ByLine)
            {
                CodeEditor.AddMarker(Line - 1, TypeErrCol, TypeErrFill,
                                     "Type error", Tip.c_str());
            }
        }

        // Lint warnings: skipped on compile-error line and on lines that already have a type error.
        const ImU32 LintCol  = IM_COL32(220, 165, 60, 255);
        const ImU32 LintFill = IM_COL32(220, 165, 60, 35);
        for (const FLuaLintWarning& W : LintWarnings)
        {
            if (W.Line < 1 || W.Line > CodeEditor.GetLineCount()) continue;
            if (bHasCompileError && W.Line == CompileErrorLine) continue;
            // Suppress lint on lines that already have a type error so the
            // gutter doesn't stack two strips on the same row.
            bool bHasTypeError = false;
            for (const FLuaTypeDiagnostic& E : TypeErrors)
            {
                if (E.Line == W.Line) { bHasTypeError = true; break; }
            }
            if (bHasTypeError) continue;

            const int LineIdx = W.Line - 1;
            char Title[128];
            std::snprintf(Title, sizeof(Title), "Lint: %s", W.Name.c_str());
            char Tip[768];
            std::snprintf(Tip, sizeof(Tip), "%s\n\nLuau lint code: %s",
                          W.Message.c_str(), W.Name.c_str());
            CodeEditor.AddMarker(LineIdx, LintCol, LintFill, Title, Tip);
        }

        // Highlight-all-references: teal stripe cleared when toggled off or cursor moves to a different identifier.
        if (!HighlightedReferences.empty())
        {
            const ImU32 RefCol  = IM_COL32(80, 200, 180, 255);
            const ImU32 RefFill = IM_COL32(80, 200, 180, 28);
            for (const FLuaSymbolRef& Ref : HighlightedReferences)
            {
                if (Ref.Line < 1 || Ref.Line > CodeEditor.GetLineCount()) continue;
                CodeEditor.AddMarker(Ref.Line - 1, RefCol, RefFill,
                    "Reference", "Reference to the local under the cursor.");
            }
        }
    }

    bool FLuaEditorTool::IsDebuggerPausedHere() const
    {
        Lua::FLuaDebugger& Debugger = Lua::FLuaDebugger::Get();
        if (!Debugger.IsPaused())
        {
            return false;
        }

        // Strip sigil prefix (@, =) and leading slash before comparing so chunk names and virtual paths pair correctly.
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
        // Frequent actions as compact icon buttons; the rest in the overflow menu.
        // The filename shows in the window tab, so it isn't repeated here.

        // Save tints amber while the buffer is dirty.
        if (bBufferDirty) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.2f, 1.0f));
        if (ImGui::Button(LE_ICON_CONTENT_SAVE)) OnSave();
        if (bBufferDirty) ImGui::PopStyleColor();
        if (bBufferDirty) ImGuiX::TextTooltip("Save - unsaved changes (Ctrl+S).");
        else              ImGuiX::TextTooltip("Save (Ctrl+S).");

        ImGui::SameLine();
        ImGui::BeginDisabled(!CodeEditor.CanUndo());
        if (ImGui::Button(LE_ICON_UNDO_VARIANT)) CodeEditor.Undo();
        ImGui::EndDisabled();
        ImGuiX::TextTooltip("Undo (Ctrl+Z).");

        ImGui::SameLine();
        ImGui::BeginDisabled(!CodeEditor.CanRedo());
        if (ImGui::Button(LE_ICON_REDO_VARIANT)) CodeEditor.Redo();
        ImGui::EndDisabled();
        ImGuiX::TextTooltip("Redo (Ctrl+Y).");

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        if (ImGui::Button(LE_ICON_MAGNIFY)) CodeEditor.OpenFindReplaceWindow();
        ImGuiX::TextTooltip("Find / replace (Ctrl+F).");

        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_FORMAT_LINE_SPACING)) bRequestOpenGoto = true;
        ImGuiX::TextTooltip("Go to line (Ctrl+G).");

        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_AUTO_FIX)) ImGui::OpenPopup("##lua_format");
        ImGuiX::TextTooltip("Format / whitespace transforms.");

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // Outline toggle reflects state via active-button styling.
        ImGui::PushStyleColor(ImGuiCol_Button, bShowOutline ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
        if (ImGui::Button(LE_ICON_FORMAT_LIST_BULLETED))
        {
            bShowOutline = !bShowOutline;
            if (bShowOutline) RebuildDocumentOutline();
        }
        ImGui::PopStyleColor();
        ImGuiX::TextTooltip("Toggle the outline panel (Ctrl+\\).");

        // Problems: icon + count, colored by worst severity.
        const size_t TotalErrors = (bHasCompileError ? 1u : 0u) + TypeErrors.size();
        const size_t TotalWarn   = LintWarnings.size();
        char ProblemsLabel[48];
        if (TotalErrors == 0 && TotalWarn == 0)
            std::snprintf(ProblemsLabel, sizeof(ProblemsLabel), LE_ICON_ALERT_CIRCLE);
        else
            std::snprintf(ProblemsLabel, sizeof(ProblemsLabel), LE_ICON_ALERT_CIRCLE " %zu", TotalErrors + TotalWarn);

        ImGui::SameLine();
        if (TotalErrors > 0)    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 110, 110, 255));
        else if (TotalWarn > 0) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 165, 60, 255));
        if (ImGui::Button(ProblemsLabel)) ImGui::OpenPopup("##lua_problems");
        if (TotalErrors > 0 || TotalWarn > 0) ImGui::PopStyleColor();
        ImGuiX::TextTooltip("Problems: compile / type errors and lint warnings.\nClick to jump to one.");

        // Overflow menu, right-aligned.
        const float OverflowW = ImGui::CalcTextSize(LE_ICON_DOTS_HORIZONTAL).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine();
        const float OverflowX = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - OverflowW;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), OverflowX));
        if (ImGui::Button(LE_ICON_DOTS_HORIZONTAL)) ImGui::OpenPopup("##lua_overflow");
        ImGuiX::TextTooltip("More actions.");

        if (ImGui::BeginPopup("##lua_overflow"))
        {
            if (ImGui::MenuItem(LE_ICON_REFRESH " Reload from disk")) LoadFromDisk();
            if (ImGui::MenuItem(LE_ICON_REFRESH " Refresh symbols"))
            {
                RebuildSymbolIndex();
                ImGuiX::Notifications::NotifySuccess("Re-harvested {0} Lua symbols.", (int)AllSymbols.size());
            }
            ImGui::Separator();
            if (ImGui::MenuItem(LE_ICON_CODE_BRACES " Snippets..."))  bRequestOpenSnippets  = true;
            if (ImGui::MenuItem(LE_ICON_BOOKMARK " Bookmarks..."))    bRequestOpenBookmarks = true;
            ImGui::BeginDisabled(!IsDebuggerPausedHere());
            if (ImGui::MenuItem(LE_ICON_DEBUG_STEP_OVER " Run to cursor")) RunToCursor();
            ImGui::EndDisabled();
            if (ImGui::MenuItem(LE_ICON_BUG " Breakpoints..."))      bRequestOpenBreakpoints = true;
            ImGui::Separator();
            if (ImGui::MenuItem(LE_ICON_HELP_CIRCLE " Help..."))     bRequestOpenHelp     = true;
            if (ImGui::MenuItem(LE_ICON_COG " Settings..."))         bRequestOpenSettings = true;
            ImGui::EndPopup();
        }

        // Deferred opens: OpenPopup must fire at this (root) ID-stack scope,
        // not from inside the overflow menu above.
        if (bRequestOpenGoto)
        {
            ImGui::OpenPopup("##lua_goto_line");
            bRequestOpenGoto = false;
            GotoLineBuffer = CodeEditor.GetCurrentCursorPosition().line + 1;
        }
        if (bRequestOpenSnippets)    { ImGui::OpenPopup("##lua_snippets");        bRequestOpenSnippets    = false; }
        if (bRequestOpenBookmarks)   { ImGui::OpenPopup("##lua_bookmarks");       bRequestOpenBookmarks   = false; }
        if (bRequestOpenBreakpoints) { ImGui::OpenPopup("##lua_breakpoints");     bRequestOpenBreakpoints = false; }
        if (bRequestOpenHelp)        { ImGui::OpenPopup("##lua_help");            bRequestOpenHelp        = false; }
        if (bRequestOpenSettings)    { ImGui::OpenPopup("##lua_editor_settings"); bRequestOpenSettings    = false; }

        // Popup bodies (each no-ops unless its popup is open).
        DrawGotoLinePopup();
        DrawSnippetsPopup();
        DrawFormatPopup();
        DrawProblemsPopup();
        DrawHelpPopup();
        DrawSettingsPopup();

        if (ImGui::BeginPopup("##lua_bookmarks"))
        {
            if (Bookmarks.empty())
            {
                ImGui::TextDisabled("No bookmarks set.");
                ImGui::TextDisabled("Press Ctrl+F2 to add one at the cursor.");
            }
            else
            {
                TVector<int> Sorted(Bookmarks.begin(), Bookmarks.end());
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

        if (ImGui::BeginPopup("##lua_breakpoints"))
        {
            if (Breakpoints.empty())
            {
                ImGui::TextDisabled("No breakpoints set.");
                ImGui::TextDisabled("Right-click a line number to add one.");
            }
            else
            {
                bool bMarkersDirty = false;
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
                        bMarkersDirty = true;
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
                    bMarkersDirty = true;
                }

                if (bMarkersDirty)
                {
                    RefreshBreakpointMarkers();
                }
            }
            ImGui::EndPopup();
        }
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
            CLuaEditorSettings* Settings = GetMutableDefault<CLuaEditorSettings>();
            Settings->FontScale             = EditorFontScale;
            Settings->TabSize               = EditorTabSize;
            Settings->LineSpacing           = EditorLineSpacing;
            Settings->bShowWhitespace       = bEditorShowWhitespace;
            Settings->bShowLineNumbers      = bEditorShowLineNumbers;
            Settings->bShowMiniMap          = bEditorShowMiniMap;
            Settings->bAutoIndent           = bAutoIndent;
            Settings->bMatchBrackets        = bShowMatchingBrackets;
            Settings->bCompletePairs        = bCompletePairedGlyphs;
            Settings->bInsertSpacesOnTabs   = bInsertSpacesOnTabs;
            Settings->bTrimTrailingOnSave   = bTrimTrailingOnSave;
            Settings->bAutoTriggerCompletion = bAutoTriggerCompletion;
            Settings->AutoTriggerDelayMs    = AutoTriggerDelayMs;
            Settings->Palette               = (EditorPalette == EPalette::Dark) ? "Dark" : "Light";
            GConfig->SaveSettings(CLuaEditorSettings::StaticClass());
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
        // ReplaceTextInCurrentCursor participates in undo/redo and triggers the change callback.
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
        if (ImGui::MenuItem("Format document", "Ctrl+Shift+I"))
        {
            FormatDocument();
        }
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

    void FLuaEditorTool::DrawProblemsPopup()
    {
        if (!ImGui::BeginPopup("##lua_problems"))
        {
            return;
        }

        const size_t TotalErrors = (bHasCompileError ? 1u : 0u) + TypeErrors.size();
        const size_t TotalWarn   = LintWarnings.size();

        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.85f, 1.0f),
            LE_ICON_ALERT_CIRCLE " Problems  ");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%zu errors", TotalErrors);
        ImGui::SameLine();
        ImGui::TextDisabled("/");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.86f, 0.71f, 0.35f, 1.0f), "%zu warnings", TotalWarn);

        if (TotalErrors == 0 && TotalWarn == 0)
        {
            ImGui::Separator();
            ImGui::TextDisabled("No problems detected.");
            ImGui::EndPopup();
            return;
        }

        ImGui::Separator();
        if (ImGui::BeginTable("##lua_problems_table", 3,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
                    | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                ImVec2(640.0f, 320.0f)))
        {
            ImGui::TableSetupColumn("Kind",    ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Line",    ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            auto JumpTo = [&](int Line1, int Col1)
            {
                const int LineCount = std::max(1, CodeEditor.GetLineCount());
                const int Target    = std::max(0, std::min(Line1 - 1, LineCount - 1));
                CodeEditor.SetCursor(Target, std::max(0, Col1 - 1));
                CodeEditor.ScrollToLine(Target, TextEditor::Scroll::alignMiddle);
                ImGui::CloseCurrentPopup();
            };

            int RowID = 0;
            if (bHasCompileError)
            {
                ImGui::TableNextRow();
                ImGui::PushID(RowID++);
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "compile");
                ImGui::TableNextColumn();
                ImGui::Text("%d", CompileErrorLine);
                ImGui::TableNextColumn();
                if (ImGui::Selectable(CompileErrorMessage.c_str(), false,
                        ImGuiSelectableFlags_SpanAllColumns))
                {
                    JumpTo(CompileErrorLine, 1);
                }
                ImGui::PopID();
            }

            for (const FLuaTypeDiagnostic& E : TypeErrors)
            {
                ImGui::TableNextRow();
                ImGui::PushID(RowID++);
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "type");
                ImGui::TableNextColumn();
                ImGui::Text("%d", E.Line);
                ImGui::TableNextColumn();
                if (ImGui::Selectable(E.Message.c_str(), false,
                        ImGuiSelectableFlags_SpanAllColumns))
                {
                    JumpTo(E.Line, E.Column);
                }
                ImGui::PopID();
            }

            for (const FLuaLintWarning& W : LintWarnings)
            {
                ImGui::TableNextRow();
                ImGui::PushID(RowID++);
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.86f, 0.71f, 0.35f, 1.0f),
                    "%s", W.Name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%d", W.Line);
                ImGui::TableNextColumn();
                if (ImGui::Selectable(W.Message.c_str(), false,
                        ImGuiSelectableFlags_SpanAllColumns))
                {
                    JumpTo(W.Line, W.Column);
                }
                ImGui::PopID();
            }

            ImGui::EndTable();
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
                Row("F12",                "Go to definition (locals only)");
                Row("Shift+F12",          "Toggle highlight all references");
                Row("Alt+Shift+Right",    "Expand selection to enclosing scope");
                Row("Ctrl+Shift+I",       "Format document");
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

        // Gate shortcuts on focus so F5/F10/F11 don't fire in other panels.
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
            Watches.push_back(Move(W));
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
            Lua::FLuaDebugger::Get().EnumerateChildrenInPausedFrame(Frame, FStringView(Path.c_str(), Path.size()), Children, 256);

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

        // F12: go to definition; Shift+F12: toggle reference highlights (opt-in to avoid flashes).
        if (ImGui::IsKeyPressed(ImGuiKey_F12, false))
        {
            if (Io.KeyShift)
            {
                ToggleHighlightReferencesAtCursor();
            }
            else
            {
                if (!GoToDefinitionAtCursor())
                {
                    ImGuiX::Notifications::NotifyInfo("No local definition under the cursor.");
                }
            }
        }

        // Alt+Shift+Right: expand selection to the next enclosing AST node.
        // Mirrors the JetBrains/VSCode shortcut for smart selection.
        if (Io.KeyAlt && Io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))
        {
            ExpandSelectionToEnclosingNode();
        }

        // Ctrl+Shift+I (mnemonic: "Indent + format"): pretty-print the file.
        if (Io.KeyCtrl && Io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_I, false))
        {
            FormatDocument();
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
        if (F.Locals.empty() && F.Upvalues.empty())
        {
            return;
        }

        // Locals shadow upvalues; check locals first. Linear scan beats a hashmap for typical frame size.
        auto FindByName = [&](FStringView Name) -> const Lua::FStackVariable*
        {
            for (const Lua::FStackVariable& V : F.Locals)
            {
                if (FStringView(V.Name.c_str(), V.Name.size()) == Name) return &V;
            }
            for (const Lua::FStackVariable& V : F.Upvalues)
            {
                if (FStringView(V.Name.c_str(), V.Name.size()) == Name) return &V;
            }
            return nullptr;
        };

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
                    const int Start = I;
                    while (I < N && IsIdentChar(LineText[I]))
                    {
                        ++I;
                    }
                    if (const Lua::FStackVariable* Hit = FindByName(FStringView(LineText.data() + Start, I - Start)))
                    {
                        MatchVar = Hit;
                        break;
                    }
                    continue;
                }
                ++I;
            }
            if (MatchVar == nullptr) continue;


            int LastNonWs = N;
            while (LastNonWs > 0 && (LineText[LastNonWs - 1] == ' ' || LineText[LastNonWs - 1] == '\t'))
            {
                --LastNonWs;
            }

            // Use visible column rather than byte index so tabs are accounted for.
            const int TabSize = CodeEditor.GetTabSize();
            int Visible = 0;
            for (int K = 0; K < LastNonWs; ++K)
            {
                Visible += (LineText[K] == '\t') ? (TabSize - (Visible % TabSize)) : 1;
            }

            char Buf[256];
            std::snprintf(Buf, sizeof(Buf), "  %s = %.80s", MatchVar->Name.c_str(), MatchVar->Value.c_str());

            const ImVec2 Pos = CodeEditor.GetScreenPosForCoordinate(LineIdx, Visible);
            DrawList->AddText(Pos, GhostColor, Buf);
        }
    }

    void FLuaEditorTool::RefreshLineCache()
    {
        const size_t Undo = CodeEditor.GetUndoIndex();
        const int LineCount = CodeEditor.GetLineCount();
        if (Undo == CachedLinesUndoIndex && static_cast<int>(CachedLines.size()) == LineCount)
        {
            return;
        }
        CachedLines.resize(LineCount);
        for (int L = 0; L < LineCount; ++L)
        {
            CachedLines[L] = CodeEditor.GetLineText(L);
        }
        CachedLinesUndoIndex = Undo;
    }

    void FLuaEditorTool::RebuildDocumentOutline()
    {
        DocumentOutline.clear();
        if (!AstAnalyzer.IsValid()) return;

        TVector<FLuaAstOutlineEntry> Entries;
        AstAnalyzer.CollectOutline(Entries);

        DocumentOutline.reserve(Entries.size());
        for (const FLuaAstOutlineEntry& E : Entries)
        {
            FOutlineItem Item;
            switch (E.Kind)
            {
            case FLuaAstOutlineEntry::EKind::Function:
            case FLuaAstOutlineEntry::EKind::LocalFunction:
            case FLuaAstOutlineEntry::EKind::Method:
                Item.Kind = 'f'; break;
            case FLuaAstOutlineEntry::EKind::Field:
                Item.Kind = 'e'; break;
            default:
                Item.Kind = 'l'; break;
            }
            Item.Name.assign(E.Name.c_str(), E.Name.size());
            Item.Detail.assign(E.Detail.c_str(), E.Detail.size());
            Item.Line   = E.Line - 1; // legacy zero-based
            Item.Indent = E.Indent;
            DocumentOutline.push_back(Move(Item));
        }
    }

    void FLuaEditorTool::FormatDocument()
    {
        const std::string Body = CodeEditor.GetText();
        FString Pretty;
        FString Error;
        if (!FLuaAstAnalyzer::Format(FStringView(Body.data(), Body.size()), Pretty, Error))
        {
            ImGuiX::Notifications::NotifyError("Format failed: {0}", Error.c_str());
            return;
        }

        if (Body.size() == Pretty.size() && std::memcmp(Body.data(), Pretty.data(), Body.size()) == 0)
        {
            // Already pretty - skip the SetText round-trip so we don't burn
            // an undo entry or jolt the cursor for a no-op format.
            return;
        }
        
        const TextEditor::CursorPosition Cursor = CodeEditor.GetCurrentCursorPosition();
        const int FirstVisible = CodeEditor.GetFirstVisibleLine();

        CodeEditor.SetText(std::string_view(Pretty.c_str(), Pretty.size()));
        bBufferDirty = (CodeEditor.GetUndoIndex() != LastSyncedUndoIndex);

        const int NewLineCount = std::max(1, CodeEditor.GetLineCount());
        const int RestoredLine = std::max(0, std::min(Cursor.line, NewLineCount - 1));
        CodeEditor.SetCursor(RestoredLine, 0);
        CodeEditor.ScrollToLine(std::max(0, std::min(FirstVisible, NewLineCount - 1)),
                                 TextEditor::Scroll::alignTop);

        RefreshAnalysis(FStringView(Pretty.data(), Pretty.size()));
    }

    bool FLuaEditorTool::GoToDefinitionAtCursor()
    {
        if (!AstAnalyzer.IsValid()) return false;
        const TextEditor::CursorPosition Pos = CodeEditor.GetCurrentCursorPosition();
        FString Name;
        int DeclLine = 0, DeclCol = 0;
        if (!AstAnalyzer.FindLocalDefinition(Pos.line + 1, Pos.column + 1,
                                              &Name, &DeclLine, &DeclCol))
        {
            return false;
        }
        const int Target = std::max(0, DeclLine - 1);
        CodeEditor.SetCursor(Target, std::max(0, DeclCol - 1));
        CodeEditor.ScrollToLine(Target, TextEditor::Scroll::alignMiddle);
        return true;
    }

    void FLuaEditorTool::ToggleHighlightReferencesAtCursor()
    {
        if (!HighlightedReferences.empty())
        {
            HighlightedReferences.clear();
            RefreshBreakpointMarkers();
            return;
        }
        if (!AstAnalyzer.IsValid()) return;
        const TextEditor::CursorPosition Pos = CodeEditor.GetCurrentCursorPosition();
        AstAnalyzer.FindLocalReferences(Pos.line + 1, Pos.column + 1, HighlightedReferences);
        RefreshBreakpointMarkers();
    }

    void FLuaEditorTool::ExpandSelectionToEnclosingNode()
    {
        if (!AstAnalyzer.IsValid()) return;
        const TextEditor::CursorPosition Pos = CodeEditor.GetCurrentCursorPosition();
        const TextEditor::CursorSelection Sel = CodeEditor.GetMainCursorSelection();

        int OutSL, OutSC, OutEL, OutEC;
        const bool bOk = AstAnalyzer.FindEnclosingRange(
            Pos.line + 1, Pos.column + 1,
            Sel.start.line + 1, Sel.start.column + 1,
            Sel.end.line + 1,   Sel.end.column + 1,
            OutSL, OutSC, OutEL, OutEC);
        if (!bOk) return;

        CodeEditor.SelectRegion(OutSL - 1, OutSC - 1, OutEL - 1, OutEC - 1);
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
        if (RequestedBreakpointSettingsLine < 0)
        {
            return;
        }

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
        // Build tooltip at runtime: TextTooltip's compile-time validation rejects {expr} placeholders in literals.
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
        BpIgnoreCount = std::max(BpIgnoreCount, 0);

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
            // CachedBodySize is refreshed on change/save/load to avoid per-frame GetText() copies.
            const size_t Bytes = CachedBodySize;

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

            if (!TypeErrors.empty())
            {
                ImGui::SameLine(0, 20);
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f),
                    LE_ICON_ALERT_CIRCLE " %zu type", TypeErrors.size());
                ImGuiX::TextTooltip("Luau type-checker errors. Click the Problems button to navigate them.");
            }

            if (!LintWarnings.empty())
            {
                ImGui::SameLine(0, 20);
                ImGui::TextColored(ImVec4(0.86f, 0.71f, 0.35f, 1.0f),
                    "%zu lint", LintWarnings.size());
                ImGuiX::TextTooltip("Luau Analysis lint warnings on this file. Hover the gutter strips for details.");
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


}
