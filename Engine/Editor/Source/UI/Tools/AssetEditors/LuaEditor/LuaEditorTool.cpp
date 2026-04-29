#include "LuaEditorTool.h"

#include "Config/Config.h"
#include "FileSystem/FileSystem.h"
#include "Log/Log.h"
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

        // Toggle a breakpoint by right-clicking the line number gutter.
        CodeEditor.SetLineNumberContextMenuCallback([this](int Line)
        {
            if (ImGui::MenuItem(Breakpoints.count(Line) ? "Remove breakpoint" : "Add breakpoint"))
            {
                ToggleBreakpoint(Line);
            }
            if (ImGui::MenuItem("Clear all breakpoints", nullptr, false, !Breakpoints.empty()))
            {
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

        CreateToolWindow("LuaEditor", [this](bool bFocused)
        {
            DrawToolbar();
            ImGui::Separator();

            const ImVec2 Avail = ImGui::GetContentRegionAvail();
            const float StatusBarHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
            const ImVec2 EditorSize(Avail.x, std::max(32.0f, Avail.y - StatusBarHeight));

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
        auto Itr = Breakpoints.find(Line);
        if (Itr != Breakpoints.end())
        {
            Breakpoints.erase(Itr);
        }
        else
        {
            Breakpoints.insert(Line);
        }
        RefreshBreakpointMarkers();
    }

    // ---------------------------------------------------------- autocomplete ---

    void FLuaEditorTool::RebuildSymbolIndex()
    {
        AllSymbols.clear();
        TopLevelSymbols.clear();
        MembersByPath.clear();
        TableNames.clear();

        Lua::FScriptingContext::Get().HarvestGlobalSymbols(AllSymbols);

        for (const Lua::FLuaSymbol& Symbol : AllSymbols)
        {
            const eastl::string Name(Symbol.Name.c_str(), Symbol.Name.size());
            const eastl::string Path(Symbol.Path.c_str(), Symbol.Path.size());

            if (Symbol.Parent.empty())
            {
                TopLevelSymbols.push_back(Name);
                if (Symbol.Kind == Lua::ELuaSymbolKind::Table)
                {
                    TableNames.insert(Path);
                }
            }
            else
            {
                const eastl::string Parent(Symbol.Parent.c_str(), Symbol.Parent.size());
                MembersByPath[Parent].push_back(Name);
                if (Symbol.Kind == Lua::ELuaSymbolKind::Table)
                {
                    TableNames.insert(Path);
                }
            }
        }
    }

    void FLuaEditorTool::OnAutoCompleteRequest(TextEditor::AutoCompleteState& State)
    {
        State.suggestions.clear();

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

        auto MatchesPrefix = [&](const eastl::string& Candidate)
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

        // Member-access path: "Engine.VFS." or "Engine.VFS:". Only suggest
        // children of the resolved owner; nothing else makes sense in context.
        if (!OwnerPath.empty())
        {
            auto Itr = MembersByPath.find(OwnerPath);
            if (Itr != MembersByPath.end())
            {
                for (const eastl::string& Member : Itr->second)
                {
                    if (MatchesPrefix(Member))
                    {
                        State.suggestions.emplace_back(Member.c_str(), Member.size());
                    }
                }
            }
            std::sort(State.suggestions.begin(), State.suggestions.end());
            return;
        }

        // Top-level: keywords + engine globals + identifiers in the buffer.
        eastl::hash_set<eastl::string> Seen;
        auto Push = [&](const eastl::string& Suggestion)
        {
            if (!MatchesPrefix(Suggestion)) return;
            if (Seen.find(Suggestion) != Seen.end()) return;
            Seen.insert(Suggestion);
            State.suggestions.emplace_back(Suggestion.c_str(), Suggestion.size());
        };

        for (const char* Keyword : kLuauKeywords)
        {
            Push(eastl::string(Keyword));
        }
        for (const eastl::string& Top : TopLevelSymbols)
        {
            Push(Top);
        }

        // Identifiers harvested from the current buffer — picks up locals,
        // function names, etc. that aren't in the engine global table.
        CodeEditor.IterateIdentifiers([&](const std::string& Identifier)
        {
            // Don't shadow-suggest the word the user is currently typing.
            if (Identifier == Term) return;
            Push(eastl::string(Identifier.c_str(), Identifier.size()));
        });

        std::sort(State.suggestions.begin(), State.suggestions.end());
    }

    void FLuaEditorTool::RefreshBreakpointMarkers()
    {
        // TextEditor::AddMarker is additive; clear-and-readd to keep state in
        // sync with our breakpoint set.
        CodeEditor.ClearMarkers();
        const ImU32 Red       = IM_COL32(220, 60, 60, 255);
        const ImU32 Translucent = IM_COL32(220, 60, 60, 40);
        for (int Line : Breakpoints)
        {
            CodeEditor.AddMarker(Line, Red, Translucent, "Breakpoint", "Breakpoint (debugger not yet wired)");
        }
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
                        Itr = Breakpoints.erase(Itr);
                    }
                    else
                    {
                        ++Itr;
                    }
                }

                if (ImGui::Button("Clear all", ImVec2(-1, 0)))
                {
                    Breakpoints.clear();
                }
            }

            // Always refresh markers; cheap and stays in sync with edits above.
            RefreshBreakpointMarkers();
            ImGui::EndPopup();
        }
        ImGuiX::TextTooltip("Manage breakpoints. Runtime debugger hookup is a follow-up.");

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
