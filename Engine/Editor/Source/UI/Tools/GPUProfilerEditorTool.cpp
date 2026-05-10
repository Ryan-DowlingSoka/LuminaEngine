#include "GPUProfilerEditorTool.h"

#include "implot.h"
#include "Core/Console/ConsoleVariable.h"
#include "Renderer/GPUProfiler/GPUProfiler.h"

namespace Lumina
{
    void FGPUProfilerEditorTool::OnInitialize()
    {
        CreateToolWindow("GPU Profiler", [&] (bool bIsFocused)
        {
            DrawProfilerWindow(bIsFocused);
        });
    }

    void FGPUProfilerEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
    }

    void FGPUProfilerEditorTool::DrawToolMenu(const FUpdateContext& UpdateContext)
    {
        FConsoleVariable* CVar = FConsoleRegistry::Get().Find("r.GPUProfiler.Enabled");
        if (CVar != nullptr)
        {
            bool bEnabled = eastl::get<bool>(*CVar->ValuePtr);
            if (ImGui::MenuItem(LE_ICON_POWER " Profiling Enabled", nullptr, bEnabled))
            {
                FConsoleRegistry::Get().SetAs<bool>("r.GPUProfiler.Enabled", !bEnabled);
            }
        }

        ImGui::Separator();
        ImGui::MenuItem("Expand All", nullptr, &bExpandAll);
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat("Min ms filter", &FilterMinTimeMs, 0.005f, 0.0f, 50.0f, "%.3f");
    }

    void FGPUProfilerEditorTool::DrawHelpMenu()
    {
        DrawHelpTextRow("Enable",
            "GPU profiling is gated on the r.GPUProfiler.Enabled CVar — toggleable from the gear menu. "
            "It costs frames so leave it off when not investigating.");
        DrawHelpTextRow("Reading the tree",
            "Each node is a GPU scope pushed via FGPUProfileScope. Time shown is GPU time in ms; "
            "use Min ms filter to hide noise from sub-millisecond passes.");
        DrawHelpTextRow("Frame Time Graph",
            "Rolling history of total frame GPU time. Spikes correlate with ms surges in the tree below.");
        DrawHelpTextRow("Pipeline Stats",
            "Vertex / fragment / compute invocations from the active backend's query pool. "
            "Available only when the backend reports pipeline statistics.");
        DrawHelpTextRow("Adding scopes",
            "In C++: FGPUProfileScope Scope(CmdBuffer, \"YourPassName\"); — scopes nest naturally.");
    }

    void FGPUProfilerEditorTool::DrawProfilerWindow(bool bIsFocused)
    {
        FGPUProfiler& Profiler = FGPUProfiler::Get();

        FConsoleVariable* CVar = FConsoleRegistry::Get().Find("r.GPUProfiler.Enabled");
        bool bEnabled = (CVar != nullptr) ? eastl::get<bool>(*CVar->ValuePtr) : false;

        // Header / toggle row.
        if (ImGui::Checkbox("Enable GPU Profiling", &bEnabled))
        {
            if (CVar != nullptr)
            {
                FConsoleRegistry::Get().SetAs<bool>("r.GPUProfiler.Enabled", bEnabled);
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(CVar: r.GPUProfiler.Enabled)");

        const FGPUProfileFrame* Frame = Profiler.GetLatestResolvedFrame();

        if (!bEnabled)
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f),
                LE_ICON_INFORMATION " GPU profiling is disabled. Enable it above to begin collecting timestamps.");
            return;
        }

        // Stats summary
        ImGui::Spacing();
        ImGui::Separator();
        if (Frame != nullptr)
        {
            ImGui::Text("Frame #%llu  |  GPU Total: %.3f ms  |  Scopes: %u",
                (unsigned long long)Frame->FrameNumber,
                Frame->TotalTimeMs,
                (uint32)Frame->Scopes.size());
        }
        else
        {
            ImGui::TextDisabled("Waiting for first resolved frame...");
        }
        ImGui::Separator();

        DrawFrameTimeGraph();

        ImGui::Spacing();
        ImGui::TextUnformatted("Scopes (this frame):");
        ImGui::Separator();
        DrawScopeTree();

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Pipeline Statistics", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawPipelineStats();
        }

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Profiler Diagnostics"))
        {
            DrawDiagnostics();
        }
    }

    static void FormatCount(char* Buffer, size_t BufferSize, uint64 Value)
    {
        if (Value >= 1000000000ull)
        {
            snprintf(Buffer, BufferSize, "%.2f B", (double)Value / 1000000000.0);
        }
        else if (Value >= 1000000ull)
        {
            snprintf(Buffer, BufferSize, "%.2f M", (double)Value / 1000000.0);
        }
        else if (Value >= 1000ull)
        {
            snprintf(Buffer, BufferSize, "%.2f K", (double)Value / 1000.0);
        }
        else
        {
            snprintf(Buffer, BufferSize, "%llu", (unsigned long long)Value);
        }
    }

    void FGPUProfilerEditorTool::DrawPipelineStats()
    {
        const FGPUProfileFrame* Frame = FGPUProfiler::Get().GetLatestResolvedFrame();
        if (Frame == nullptr)
        {
            ImGui::TextDisabled("No resolved frame.");
            return;
        }

        const FPipelineStats& Totals = Frame->TotalStats;

        char Buf[32];
        auto StatRow = [&](const char* Label, uint64 Value)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(Label);
            FormatCount(Buf, sizeof(Buf), Value);
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(Buf);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%llu", (unsigned long long)Value);
        };

        if (ImGui::BeginTable("##FrameStats", 3,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Counter", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value",   ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Exact",   ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableHeadersRow();

            StatRow("IA Vertices",          Totals.InputAssemblyVertices);
            StatRow("IA Primitives",        Totals.InputAssemblyPrimitives);
            StatRow("VS Invocations",       Totals.VertexShaderInvocations);
            StatRow("Clipping Invocations", Totals.ClippingInvocations);
            StatRow("Clipping Primitives",  Totals.ClippingPrimitives);
            StatRow("FS Invocations",       Totals.FragmentShaderInvocations);
            StatRow("CS Invocations",       Totals.ComputeShaderInvocations);

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Per top-level scope:");

        if (!ImGui::BeginTable("##ScopeStats", 8,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                ImVec2(0.0f, 180.0f)))
        {
            return;
        }

        ImGui::TableSetupColumn("Scope",    ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("IA Vtx",   ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("IA Prim",  ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("VS",       ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Clip Inv", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Clip Prim",ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("FS",       ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("CS",       ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        auto CountCell = [&](int32 Col, uint64 Value)
        {
            FormatCount(Buf, sizeof(Buf), Value);
            ImGui::TableSetColumnIndex(Col);
            ImGui::TextUnformatted(Buf);
        };

        for (const FGPUProfileScope& Scope : Frame->Scopes)
        {
            if (Scope.ParentIndex >= 0 || Scope.StatsQueryIndex < 0)
            {
                continue;
            }

            ImGui::TableNextRow();
            const ImVec4 NameColor(Scope.Color.R, Scope.Color.G, Scope.Color.B, 1.0f);
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Text, NameColor);
            ImGui::TextUnformatted(Scope.Name.c_str());
            ImGui::PopStyleColor();

            const FPipelineStats& S = Scope.ResolvedStats;
            CountCell(1, S.InputAssemblyVertices);
            CountCell(2, S.InputAssemblyPrimitives);
            CountCell(3, S.VertexShaderInvocations);
            CountCell(4, S.ClippingInvocations);
            CountCell(5, S.ClippingPrimitives);
            CountCell(6, S.FragmentShaderInvocations);
            CountCell(7, S.ComputeShaderInvocations);
        }

        ImGui::EndTable();
    }

    void FGPUProfilerEditorTool::DrawDiagnostics()
    {
        FGPUProfiler& Profiler = FGPUProfiler::Get();

        ImGui::Text("Frame Counter:        %llu", (unsigned long long)Profiler.GetFrameCounter());
        ImGui::Text("Recording Slot:       %u",   Profiler.GetRecordingSlot());
        ImGui::Text("Latest Resolved Slot: %d",   Profiler.GetLatestResolvedSlot());
        ImGui::Text("History Samples:      %d",   (int)Profiler.GetFrameTimeHistory().size());

        ImGui::Spacing();

        if (ImGui::BeginTable("##GPUSlots", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Slot");
            ImGui::TableSetupColumn("State");
            ImGui::TableSetupColumn("Scopes");
            ImGui::TableSetupColumn("Pool");
            ImGui::TableSetupColumn("Frame#");
            ImGui::TableHeadersRow();

            const char* StateNames[] = { "Idle", "Recording", "Submitted", "Resolved" };
            for (uint32 i = 0; i < FGPUProfiler::MaxFramesInFlight; ++i)
            {
                const FGPUProfileFrame& Slot = Profiler.GetSlot(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%u", i);
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(StateNames[(int)Slot.State]);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%u", (uint32)Slot.Scopes.size());
                ImGui::TableSetColumnIndex(3); ImGui::Text("%u", (uint32)Slot.QueryPool.size());
                ImGui::TableSetColumnIndex(4); ImGui::Text("%llu", (unsigned long long)Slot.FrameNumber);
            }
            ImGui::EndTable();
        }
    }

    void FGPUProfilerEditorTool::DrawFrameTimeGraph()
    {
        const TVector<float>& History = FGPUProfiler::Get().GetFrameTimeHistory();
        if (History.empty())
        {
            return;
        }

        float MaxValue = 0.0f;
        float Sum = 0.0f;
        for (float V : History)
        {
            if (V > MaxValue)
            {
                MaxValue = V;
            }
            Sum += V;
        }
        const float Average = Sum / (float)History.size();

        ImGui::Text("GPU Frame Time  -  Avg: %.3f ms   Max: %.3f ms   Samples: %d",
            Average, MaxValue, (int)History.size());

        if (ImPlot::BeginPlot("##GPUFrameTime", ImVec2(-1, GraphHeight), ImPlotFlags_NoTitle | ImPlotFlags_NoMouseText))
        {
            ImPlot::SetupAxes(nullptr, "ms",
                ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoMenus,
                ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, eastl::max(MaxValue * 1.1f, 1.0f), ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, (double)History.size(), ImGuiCond_Always);

            ImPlot::SetNextLineStyle(ImVec4(0.40f, 0.85f, 1.0f, 1.0f), 1.5f);
            ImPlot::SetNextFillStyle(ImVec4(0.40f, 0.85f, 1.0f, 0.20f), 1.0f);
            ImPlot::PlotShaded("GPU ms", History.data(), (int)History.size(), 0.0);
            ImPlot::PlotLine("GPU ms", History.data(), (int)History.size());

            ImPlot::EndPlot();
        }
    }

    void FGPUProfilerEditorTool::DrawScopeTree()
    {
        const FGPUProfileFrame* Frame = FGPUProfiler::Get().GetLatestResolvedFrame();
        if (Frame == nullptr || Frame->Scopes.empty())
        {
            ImGui::TextDisabled("No scopes recorded.");
            return;
        }

        const float TableTotal = eastl::max(Frame->TotalTimeMs, 0.0001f);

        if (!ImGui::BeginTable("##GPUScopes", 4,
                ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
        {
            return;
        }

        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("% Frame", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Bar", ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Build child lists for hierarchical iteration.
        TVector<TVector<int32>> Children;
        Children.resize(Frame->Scopes.size());
        TVector<int32> Roots;
        for (int32 i = 0; i < (int32)Frame->Scopes.size(); ++i)
        {
            const FGPUProfileScope& Scope = Frame->Scopes[i];
            if (Scope.ParentIndex < 0)
            {
                Roots.push_back(i);
            }
            else
            {
                Children[Scope.ParentIndex].push_back(i);
            }
        }

        auto SortByTimeDesc = [&](TVector<int32>& Indices)
        {
            eastl::sort(Indices.begin(), Indices.end(), [&](int32 A, int32 B)
            {
                return Frame->Scopes[A].ResolvedTimeMs > Frame->Scopes[B].ResolvedTimeMs;
            });
        };
        SortByTimeDesc(Roots);
        for (TVector<int32>& List : Children)
        {
            SortByTimeDesc(List);
        }

        struct FStackEntry
        {
            int32 ScopeIndex;
            int32 NextChild;
            bool  bOpen;
        };

        TVector<FStackEntry> Stack;

        for (int32 RootIdx : Roots)
        {
            Stack.clear();
            Stack.push_back({RootIdx, 0, false});

            while (!Stack.empty())
            {
                FStackEntry& Top = Stack.back();
                const FGPUProfileScope& Scope = Frame->Scopes[Top.ScopeIndex];
                const TVector<int32>& Kids = Children[Top.ScopeIndex];

                if (Top.NextChild == 0)
                {
                    if (Scope.ResolvedTimeMs < FilterMinTimeMs && Kids.empty())
                    {
                        Stack.pop_back();
                        continue;
                    }

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);

                    ImGui::PushID(Top.ScopeIndex);
                    ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen;
                    if (Kids.empty())
                    {
                        Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                    }
                    if (bExpandAll)
                    {
                        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                    }

                    const ImVec4 NameColor(Scope.Color.R, Scope.Color.G, Scope.Color.B, 1.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, NameColor);
                    Top.bOpen = ImGui::TreeNodeEx(Scope.Name.c_str(), Flags);
                    ImGui::PopStyleColor();

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.3f", Scope.ResolvedTimeMs);

                    ImGui::TableSetColumnIndex(2);
                    const float Pct = (Scope.ResolvedTimeMs / TableTotal) * 100.0f;
                    ImGui::Text("%5.1f%%", Pct);

                    ImGui::TableSetColumnIndex(3);
                    const float Frac = eastl::min(eastl::max(Scope.ResolvedTimeMs / TableTotal, 0.0f), 1.0f);
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, NameColor);
                    ImGui::ProgressBar(Frac, ImVec2(-FLT_MIN, 0.0f), "");
                    ImGui::PopStyleColor();

                    ImGui::PopID();

                    if (!Top.bOpen || Kids.empty())
                    {
                        Stack.pop_back();
                        continue;
                    }
                }

                if (Top.NextChild < (int32)Kids.size())
                {
                    int32 ChildIdx = Kids[Top.NextChild++];
                    Stack.push_back({ChildIdx, 0, false});
                }
                else
                {
                    if (Top.bOpen && !Kids.empty())
                    {
                        ImGui::TreePop();
                    }
                    Stack.pop_back();
                }
            }
        }

        ImGui::EndTable();
    }
}
