#include "ScriptsInfoEditorTool.h"

#include "lstate.h"
#include "Scripting/Lua/Scripting.h"
#include "Tools/UI/ImGui/ImGuiX.h"

namespace Lumina
{
    void FScriptsInfoEditorTool::OnInitialize()
    {
        CreateToolWindow("Scripts Info", [this](bool bIsFocused)
        {
            DrawWindow(bIsFocused);
        });
    }

    void FScriptsInfoEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
    }

    void FScriptsInfoEditorTool::DrawWindow(bool bIsFocused)
    {
        auto& Context = Lua::FScriptingContext::Get();
        const int MemoryUsage = Context.GetScriptMemoryUsageBytes();

        ImGui::Text("Loaded Scripts: %zu", Context.GetAllRegisteredScripts().size());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "|");
        ImGui::SameLine();
        ImGui::Text("Memory Usage: %s", ImGuiX::FormatSize(MemoryUsage).c_str());

        ImGui::Separator();

        static ImGuiTextFilter SearchFilter;
        ImGui::SetNextItemWidth(300.0f);
        SearchFilter.Draw();

        ImGui::Separator();

        constexpr ImGuiTableFlags TableFlags = ImGuiTableFlags_BordersInner |
                                               ImGuiTableFlags_BordersOuter |
                                               ImGuiTableFlags_RowBg        |
                                               ImGuiTableFlags_SizingStretchProp;

        #if LUAI_GCMETRICS
        const GCMetrics* Metrics = Context.GetGCMetrics();
        if (Metrics)
        {
            auto BytesToMB = [](size_t Bytes) -> float
            {
                return static_cast<float>(Bytes) / (1024.f * 1024.f);
            };

            auto MSTime = [](double Seconds) -> float
            {
                return static_cast<float>(Seconds * 1000.0);
            };

            auto Row = [&](const char* Label, float Last, float Curr, const char* Unit)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(Label);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f %s", Last, Unit);
                ImGui::TableSetColumnIndex(2);

                float Delta = Curr - Last;
                if (Delta > 0.f)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.4f, 0.4f, 1.f));
                }
                else if (Delta < 0.f)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.f, 0.4f, 1.f));
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
                }

                ImGui::Text("%.3f %s", Curr, Unit);
                ImGui::PopStyleColor();
            };

            auto SectionHeader = [&](const char* Label)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.7f, 0.2f, 1.f));
                ImGui::TextUnformatted(Label);
                ImGui::PopStyleColor();
            };

            ImGui::Text("Completed Cycles: %llu", Metrics->completedcycles);
            ImGui::Text("Explicit Time Acc: %.3f ms", MSTime(Metrics->stepexplicittimeacc));
            ImGui::Text("Assist Time Acc:   %.3f ms", MSTime(Metrics->stepassisttimeacc));
            ImGui::Spacing();

            if (ImGui::BeginTable("GCMetricsTable", 3, TableFlags))
            {
                ImGui::TableSetupColumn("Metric",       ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Last Cycle",   ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Curr Cycle",   ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                const GCCycleMetrics& Last = Metrics->lastcycle;
                const GCCycleMetrics& Curr = Metrics->currcycle;

                SectionHeader("Heap");
                Row("Start Size",         BytesToMB(Last.starttotalsizebytes),      BytesToMB(Curr.starttotalsizebytes),      "MB");
                Row("Heap Trigger Size",  BytesToMB(Last.heaptriggersizebytes),     BytesToMB(Curr.heaptriggersizebytes),     "MB");
                Row("Atomic Start Size",  BytesToMB(Last.atomicstarttotalsizebytes),BytesToMB(Curr.atomicstarttotalsizebytes),"MB");
                Row("End Size",           BytesToMB(Last.endtotalsizebytes),        BytesToMB(Curr.endtotalsizebytes),        "MB");

                SectionHeader("Timing");
                Row("Pause Time",         MSTime(Last.pausetime),       MSTime(Curr.pausetime),       "ms");
                Row("Total Duration",     MSTime(Last.endtimestamp   - Last.starttimestamp), MSTime(Curr.endtimestamp - Curr.starttimestamp), "ms");

                SectionHeader("Mark");
                Row("Mark Time",          MSTime(Last.marktime),              MSTime(Curr.marktime),              "ms");
                Row("Mark Assist Time",   MSTime(Last.markassisttime),        MSTime(Curr.markassisttime),        "ms");
                Row("Mark Max Explicit",  MSTime(Last.markmaxexplicittime),   MSTime(Curr.markmaxexplicittime),   "ms");
                Row("Mark Explicit Steps",static_cast<float>(Last.markexplicitsteps), static_cast<float>(Curr.markexplicitsteps), "");
                Row("Mark Work",          static_cast<float>(Last.markwork),  static_cast<float>(Curr.markwork),  "");

                SectionHeader("Atomic");
                Row("Atomic Time",        MSTime(Last.atomictime),      MSTime(Curr.atomictime),      "ms");
                Row("  Upval",            MSTime(Last.atomictimeupval), MSTime(Curr.atomictimeupval), "ms");
                Row("  Weak",             MSTime(Last.atomictimeweak),  MSTime(Curr.atomictimeweak),  "ms");
                Row("  Gray",             MSTime(Last.atomictimegray),  MSTime(Curr.atomictimegray),  "ms");
                Row("  Clear",            MSTime(Last.atomictimeclear), MSTime(Curr.atomictimeclear), "ms");

                SectionHeader("Sweep");
                Row("Sweep Time",         MSTime(Last.sweeptime),             MSTime(Curr.sweeptime),             "ms");
                Row("Sweep Assist Time",  MSTime(Last.sweepassisttime),       MSTime(Curr.sweepassisttime),       "ms");
                Row("Sweep Max Explicit", MSTime(Last.sweepmaxexplicittime),  MSTime(Curr.sweepmaxexplicittime),  "ms");
                Row("Sweep Explicit Steps",static_cast<float>(Last.sweepexplicitsteps),static_cast<float>(Curr.sweepexplicitsteps),"");
                Row("Sweep Work",         static_cast<float>(Last.sweepwork), static_cast<float>(Curr.sweepwork), "");

                SectionHeader("Work");
                Row("Assist Work",        static_cast<float>(Last.assistwork),       static_cast<float>(Curr.assistwork),       "");
                Row("Explicit Work",      static_cast<float>(Last.explicitwork),     static_cast<float>(Curr.explicitwork),     "");
                Row("Propagate Work",     static_cast<float>(Last.propagatework),    static_cast<float>(Curr.propagatework),    "");
                Row("Propagate Again",    static_cast<float>(Last.propagateagainwork),static_cast<float>(Curr.propagateagainwork),"");

                ImGui::EndTable();
            }
        }
        #endif

        if (ImGui::BeginTable("ScriptsTable", 3, TableFlags))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Script Name", ImGuiTableColumnFlags_WidthFixed, 250.0f);
            ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableHeadersRow();

            ImGui::EndTable();
        }
    }
}
