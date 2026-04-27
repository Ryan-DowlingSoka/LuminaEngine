#include "MemoryProfilerEditorTool.h"

#include "implot.h"
#include "Core/Engine/Engine.h"
#include "Memory/Memory.h"
#include "Platform/Process/PlatformProcess.h"
#include "Tools/UI/ImGui/ImGuiX.h"

namespace Lumina
{
    void FMemoryProfilerEditorTool::OnInitialize()
    {
        CreateToolWindow("Memory Profiler", [this](bool bIsFocused)
        {
            DrawWindow(bIsFocused);
        });
    }

    void FMemoryProfilerEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
    }

    void FMemoryProfilerEditorTool::DrawWindow(bool bIsFocused)
    {
        UpdateTimer += GEngine->GetDeltaTime();
        if (!bPaused && UpdateTimer >= 1.0f)
        {
            UpdateTimer = 0.0f;

            FMemorySnapshot Snapshot;
            Snapshot.timestamp = ImGui::GetTime();
            Snapshot.processMemory = Platform::GetProcessMemoryUsageBytes();
            Snapshot.currentMapped = Memory::GetCurrentMappedMemory();
            Snapshot.cachedMemory = Memory::GetCachedMemory();
            Snapshot.hugeAllocs = Memory::GetCurrentHugeAllocMemory();

            History.push_back(Snapshot);

            if ((int32)History.size() > MaxHistoryPoints)
            {
                History.erase(History.begin());
            }
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.5f, 1.0f));
        ImGui::Text(LE_ICON_CHART_LINE " Memory Profiler");
        ImGui::PopStyleColor();

        ImGui::SameLine(ImGui::GetWindowWidth() - 230);
        if (ImGui::Button(bPaused ? LE_ICON_PLAY " Resume" : LE_ICON_PAUSE " Pause", ImVec2(100, 0)))
        {
            bPaused = !bPaused;
        }

        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_TRASH_CAN " Clear", ImVec2(100, 0)))
        {
            History.clear();
        }

        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::BeginTabBar("MemoryTabs"))
        {
            if (ImGui::BeginTabItem(LE_ICON_VIEW_DASHBOARD " Overview"))
            {
                DrawOverviewTab();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(LE_ICON_LIST_BOX " Detailed Stats"))
            {
                DrawDetailedTab();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(LE_ICON_CHART_PIE " Distribution"))
            {
                DrawDistributionTab();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }

    void FMemoryProfilerEditorTool::DrawOverviewTab()
    {
        ImGui::BeginGroup();
        {
            const size_t processMemory = Platform::GetProcessMemoryUsageBytes();
            const size_t currentMapped = Memory::GetCurrentMappedMemory();
            const size_t peakMapped = Memory::GetPeakMappedMemory();
            const size_t cachedMemory = Memory::GetCachedMemory();

            const float cardWidth = (ImGui::GetContentRegionAvail().x - 30) / 4.0f;

            ImGui::BeginChild("Card1", ImVec2(cardWidth, 100), true, ImGuiWindowFlags_NoScrollbar);
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                ImGui::Text(LE_ICON_MEMORY " Process Memory");
                ImGui::PopStyleColor();
                ImGui::Spacing();
                ImGui::Text("%s", ImGuiX::FormatSize(processMemory).c_str());
            }
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("Card2", ImVec2(cardWidth, 100), true, ImGuiWindowFlags_NoScrollbar);
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.6f, 1.0f));
                ImGui::Text(LE_ICON_DATABASE " Mapped");
                ImGui::PopStyleColor();
                ImGui::Spacing();
                ImGui::Text("%s", ImGuiX::FormatSize(currentMapped).c_str());
                const float usage = peakMapped > 0 ? (float)currentMapped / peakMapped : 0.0f;
                ImGui::ProgressBar(usage, ImVec2(-1, 0), "");
            }
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("Card3", ImVec2(cardWidth, 100), true, ImGuiWindowFlags_NoScrollbar);
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
                ImGui::Text(LE_ICON_LAYERS " Cached");
                ImGui::PopStyleColor();
                ImGui::Spacing();
                ImGui::Text("%s", ImGuiX::FormatSize(cachedMemory).c_str());
            }
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("Card4", ImVec2(cardWidth, 100), true, ImGuiWindowFlags_NoScrollbar);
            {
                const size_t hugeAllocs = Memory::GetCurrentHugeAllocMemory();
                const size_t peakHuge = Memory::GetPeakHugeAllocMemory();

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
                ImGui::Text(LE_ICON_CUBE " Huge Allocs");
                ImGui::PopStyleColor();
                ImGui::Spacing();
                ImGui::Text("%s", ImGuiX::FormatSize(hugeAllocs).c_str());
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Peak: %s", ImGuiX::FormatSize(peakHuge).c_str());
            }
            ImGui::EndChild();
        }
        ImGui::EndGroup();

        ImGui::Spacing();

        if (History.size() > 1 && ImPlot::BeginPlot("Memory Usage Over Time", ImVec2(-1, 450)))
        {
            ImPlot::SetupAxes("Time (seconds)", "Memory (bytes)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::SetupLegend(ImPlotLocation_NorthWest);

            TVector<double> times;
            TVector<double> processMem, mappedMem, cachedMem, hugeMem;

            const double baseTime = History[0].timestamp;
            for (const FMemorySnapshot& Snap : History)
            {
                times.push_back(Snap.timestamp - baseTime);
                processMem.push_back((double)Snap.processMemory);
                mappedMem.push_back((double)Snap.currentMapped);
                cachedMem.push_back((double)Snap.cachedMemory);
                hugeMem.push_back((double)Snap.hugeAllocs);
            }

            ImPlot::SetNextLineStyle(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), 2.0f);
            ImPlot::PlotLine("Process Memory", times.data(), processMem.data(), (int)times.size());

            ImPlot::SetNextLineStyle(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), 2.0f);
            ImPlot::PlotLine("Mapped Memory", times.data(), mappedMem.data(), (int)times.size());

            ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), 1.5f);
            ImPlot::PlotLine("Cached Memory", times.data(), cachedMem.data(), (int)times.size());

            ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), 1.5f);
            ImPlot::PlotLine("Huge Allocs", times.data(), hugeMem.data(), (int)times.size());

            ImPlot::EndPlot();
        }
        else if (History.size() <= 1)
        {
            ImGui::BeginChild("NoData", ImVec2(-1, 450), true);
            ImGui::SetCursorPosY(200);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            const float textWidth = ImGui::CalcTextSize("Collecting data...").x;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - textWidth) / 2);
            ImGui::Text("Collecting data...");
            ImGui::PopStyleColor();
            ImGui::EndChild();
        }
    }

    void FMemoryProfilerEditorTool::DrawDetailedTab()
    {
        ImGui::BeginChild("DetailedStats", ImVec2(0, 0), false);
        {
            if (ImGui::BeginTable("MemoryStatsDetailed", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Current Value", ImGuiTableColumnFlags_WidthFixed, 150);
                ImGui::TableSetupColumn("Peak Value", ImGuiTableColumnFlags_WidthFixed, 150);
                ImGui::TableHeadersRow();

                auto DetailRow = [](const char* label, size_t current, size_t peak = 0, bool showPeak = true)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", label);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", ImGuiX::FormatSize(current).c_str());

                    ImGui::TableSetColumnIndex(2);
                    if (showPeak)
                    {
                        ImGui::Text("%s", ImGuiX::FormatSize(peak).c_str());
                        if (peak > 0)
                        {
                            const float usage = (float)current / peak * 100.0f;
                            ImGui::SameLine();
                            ImGui::TextColored(
                                usage > 90 ? ImVec4(1, 0.3f, 0.3f, 1) : ImVec4(0.5f, 0.8f, 0.5f, 1),
                                "(%.1f%%)", usage
                            );
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("-");
                    }
                };

                DetailRow("Process Memory", Platform::GetProcessMemoryUsageBytes(), 0, false);
                DetailRow("Current Mapped", Memory::GetCurrentMappedMemory(), Memory::GetPeakMappedMemory());
                DetailRow("Cached (Small/Medium)", Memory::GetCachedMemory(), 0, false);
                DetailRow("Current Huge Allocs", Memory::GetCurrentHugeAllocMemory(), Memory::GetPeakHugeAllocMemory());
                DetailRow("Total Mapped", Memory::GetTotalMappedMemory(), 0, false);
                DetailRow("Total Unmapped", Memory::GetTotalUnmappedMemory(), 0, false);

                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), LE_ICON_CHART_BAR " Memory Efficiency");
            ImGui::Spacing();

            const size_t totalMapped = Memory::GetTotalMappedMemory();
            const size_t totalUnmapped = Memory::GetTotalUnmappedMemory();
            const size_t totalOps = totalMapped + totalUnmapped;

            if (totalOps > 0)
            {
                const float mappedRatio = (float)totalMapped / totalOps;
                const float unmappedRatio = (float)totalUnmapped / totalOps;

                ImGui::Text("Allocation Efficiency:");
                ImGui::SameLine(200);
                ImGui::ProgressBar(mappedRatio, ImVec2(300, 0), FString().sprintf("Mapped: %.1f%%", mappedRatio * 100).c_str());

                ImGui::Text("Deallocation Activity:");
                ImGui::SameLine(200);
                ImGui::ProgressBar(unmappedRatio, ImVec2(300, 0), FString().sprintf("Unmapped: %.1f%%", unmappedRatio * 100).c_str());
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Note: All values reported by rpmalloc_global_statistics");
        }
        ImGui::EndChild();
    }

    void FMemoryProfilerEditorTool::DrawDistributionTab()
    {
        const size_t processMemory = Platform::GetProcessMemoryUsageBytes();
        const size_t currentMapped = Memory::GetCurrentMappedMemory();
        const size_t cachedMemory = Memory::GetCachedMemory();
        const size_t hugeAllocs = Memory::GetCurrentHugeAllocMemory();

        if (ImPlot::BeginPlot("Memory Distribution", ImVec2(-1, 350), ImPlotFlags_Equal))
        {
            const char* labels[] = { "Mapped", "Cached", "Huge Allocs", "Other" };
            const size_t other = processMemory > (currentMapped + cachedMemory + hugeAllocs)
                ? processMemory - (currentMapped + cachedMemory + hugeAllocs)
                : 0;
            const double data[] = {
                (double)currentMapped,
                (double)cachedMemory,
                (double)hugeAllocs,
                (double)other
            };

            ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
            ImPlot::SetupAxesLimits(-1, 1, -1, 1);

            ImPlot::PlotPieChart(labels, data, 4, 0.5, 0.5, 0.4, "%.1f%%", 90);

            ImPlot::EndPlot();
        }

        ImGui::Spacing();

        if (ImPlot::BeginPlot("Memory Comparison", ImVec2(-1, 250)))
        {
            const char* categories[] = { "Mapped", "Peak Mapped", "Cached", "Huge", "Peak Huge" };
            (void)categories;
            const double values[] = {
                (double)currentMapped,
                (double)Memory::GetPeakMappedMemory(),
                (double)cachedMemory,
                (double)hugeAllocs,
                (double)Memory::GetPeakHugeAllocMemory()
            };

            ImPlot::SetupAxes("Category", "Bytes", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::PlotBars("Memory", values, 5, 0.67, 0);
            ImPlot::EndPlot();
        }
    }
}
