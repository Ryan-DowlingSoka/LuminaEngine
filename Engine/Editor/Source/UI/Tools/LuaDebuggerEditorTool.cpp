#include "LuaDebuggerEditorTool.h"

#include "EditorToolContext.h"
#include "Scripting/Lua/Debugger/LuaDebugger.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"
#include "Tools/UI/ImGui/ImGuiX.h"

#include <imgui.h>

namespace Lumina
{
    void FLuaDebuggerEditorTool::OnInitialize()
    {
        CreateToolWindow("Lua Debugger", [this](bool bIsFocused)
        {
            DrawWindow(bIsFocused);
        });
    }

    void FLuaDebuggerEditorTool::OnDeinitialize(const FUpdateContext& /*UpdateContext*/)
    {
    }

    void FLuaDebuggerEditorTool::DrawHelpMenu()
    {
        DrawHelpTextRow("Pause / Resume",
            "Hits when the VM trips a breakpoint, an error, or an explicit assert. F5 resumes; "
            "Stop ignores remaining steps.");
        DrawHelpTextRow("Stepping",
            "F10 step over (next line, same frame). F11 step into (descend into the next call). "
            "Shift+F11 step out (run until current frame returns).");
        DrawHelpTextRow("Call Stack",
            "Click a frame to scope the Locals/Upvalues view. The Lua editor jumps to that frame's source line.");
        DrawHelpTextRow("Watches",
            "Add expressions in the Lua editor's Watch panel — they re-evaluate against the selected frame's "
            "environment on every pause.");
        DrawHelpTextRow("Breakpoints",
            "Click the gutter in the Lua editor to toggle. Right-click 'Configure...' for conditional, "
            "log-only, and hit-count breakpoints.");
        DrawHelpTextRow("Inline Values",
            "While paused, the Lua editor draws each visible local's current value at end-of-line. "
            "Toggle from the editor toolbar.");
    }

    void FLuaDebuggerEditorTool::Update(const FUpdateContext& /*UpdateContext*/)
    {
        const bool bPausedNow = Lua::FLuaDebugger::Get().IsPaused();
        bJustEnteredPause = bPausedNow && !bWasPaused;
        bWasPaused = bPausedNow;

        // When a fresh break lands, hop into the source file so the user
        // sees the highlighted line without manually opening the script.
        if (bJustEnteredPause)
        {
            const FStringView Source = Lua::FLuaDebugger::Get().GetPausedSource();
            if (!Source.empty() && ToolContext != nullptr)
            {
                ToolContext->OpenFileEditor(Source);
            }
            SelectedFrame = 0;
        }
    }

    void FLuaDebuggerEditorTool::DrawWindow(bool /*bIsFocused*/)
    {
        DrawToolbar();
        ImGui::Separator();

        const ImVec2 Avail = ImGui::GetContentRegionAvail();
        const float Half = (Avail.x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        if (ImGui::BeginChild("##lua_dbg_callstack", ImVec2(Half, Avail.y * 0.6f), ImGuiChildFlags_Borders))
        {
            DrawCallStack();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        if (ImGui::BeginChild("##lua_dbg_locals", ImVec2(0, Avail.y * 0.6f), ImGuiChildFlags_Borders))
        {
            DrawLocalsAndUpvalues();
        }
        ImGui::EndChild();

        if (ImGui::BeginChild("##lua_dbg_breakpoints", ImVec2(0, 0), ImGuiChildFlags_Borders))
        {
            DrawBreakpointsList();
        }
        ImGui::EndChild();
    }

    void FLuaDebuggerEditorTool::DrawToolbar()
    {
        Lua::FLuaDebugger& Debugger = Lua::FLuaDebugger::Get();
        const bool bPaused = Debugger.IsPaused();

        ImGui::PushStyleColor(ImGuiCol_Text, bPaused ? ImVec4(1.0f, 0.7f, 0.2f, 1.0f) : ImVec4(0.55f, 0.85f, 0.55f, 1.0f));
        ImGui::Text("%s", bPaused ? LE_ICON_PAUSE_CIRCLE " PAUSED" : LE_ICON_PLAY_CIRCLE " RUNNING");
        ImGui::PopStyleColor();

        if (bPaused)
        {
            ImGui::SameLine(0, 16);
            const FStringView Source = Debugger.GetPausedSource();
            ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f),
                "%.*s : line %d",
                int(Source.size()), Source.data(),
                Debugger.GetPausedLineZeroBased() + 1);
        }

        ImGui::Spacing();

        // Buttons disabled while running — gray them out so the affordance
        // is obvious. Clicks are still no-ops if disabled state is wrong.
        ImGui::BeginDisabled(!bPaused);

        if (ImGui::Button(LE_ICON_PLAY " Continue (F5)"))                Debugger.RequestContinue();
        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_DEBUG_STEP_OVER " Step Over (F10)"))   Debugger.RequestStepOver();
        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_DEBUG_STEP_INTO " Step Into (F11)"))   Debugger.RequestStepInto();
        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_DEBUG_STEP_OUT " Step Out"))           Debugger.RequestStepOut();
        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_STOP " Stop"))                          Debugger.RequestStop();

        ImGui::EndDisabled();

        // Global debugger shortcuts (F5 = Continue, etc.); only fire when this panel
        // is focused so they don't steal input from the code editor.
        if (bPaused)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_F5,  false)) Debugger.RequestContinue();
            if (ImGui::IsKeyPressed(ImGuiKey_F10, false)) Debugger.RequestStepOver();
            if (ImGui::IsKeyPressed(ImGuiKey_F11, false))
            {
                if (ImGui::GetIO().KeyShift) Debugger.RequestStepOut();
                else                          Debugger.RequestStepInto();
            }
        }
    }

    void FLuaDebuggerEditorTool::DrawCallStack()
    {
        ImGui::TextDisabled("Call Stack");
        ImGui::Separator();

        const TVector<Lua::FStackFrame>& Stack = Lua::FLuaDebugger::Get().GetCallStack();
        if (Stack.empty())
        {
            ImGui::TextDisabled("(no active call stack)");
            return;
        }

        if (SelectedFrame >= (int)Stack.size())
        {
            SelectedFrame = 0;
        }

        for (int I = 0; I < (int)Stack.size(); ++I)
        {
            const Lua::FStackFrame& Frame = Stack[I];
            char Label[256];
            std::snprintf(Label, sizeof(Label), "[%d] %s — %s:%d##frame%d",
                I,
                Frame.FunctionName.empty() ? "?" : Frame.FunctionName.c_str(),
                Frame.Source.empty()       ? "?" : Frame.Source.c_str(),
                Frame.Line + 1,
                I);

            if (ImGui::Selectable(Label, SelectedFrame == I))
            {
                SelectedFrame = I;

                // Jump the editor to the selected frame's source location.
                if (ToolContext != nullptr && !Frame.Source.empty())
                {
                    ToolContext->OpenFileEditor(FStringView(Frame.Source.c_str(), Frame.Source.size()));
                }
            }
        }
    }

    void FLuaDebuggerEditorTool::DrawLocalsAndUpvalues()
    {
        ImGui::TextDisabled("Locals / Upvalues");
        ImGui::Separator();

        const TVector<Lua::FStackFrame>& Stack = Lua::FLuaDebugger::Get().GetCallStack();
        if (Stack.empty() || SelectedFrame < 0 || SelectedFrame >= (int)Stack.size())
        {
            ImGui::TextDisabled("(no frame selected)");
            return;
        }

        const Lua::FStackFrame& Frame = Stack[SelectedFrame];

        auto DrawTable = [&](const TVector<Lua::FStackVariable>& Vars, const char* Header)
        {
            if (Vars.empty())
            {
                return;
            }
            ImGui::SeparatorText(Header);
            if (ImGui::BeginTable(Header, 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
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

    void FLuaDebuggerEditorTool::DrawBreakpointsList()
    {
        ImGui::TextDisabled("Breakpoints");
        ImGui::Separator();
        ImGui::TextDisabled("(Manage breakpoints from the script editor's gutter — right-click a line number.)");
    }
}
