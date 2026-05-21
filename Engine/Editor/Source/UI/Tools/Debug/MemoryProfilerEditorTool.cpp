#include "MemoryProfilerEditorTool.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <EASTL/sort.h>
#include "Core/Engine/Engine.h"
#include "Memory/Memory.h"
#include "Memory/MemoryTracking.h"
#include "Platform/Process/PlatformProcess.h"
#include "Renderer/RHIGlobals.h"
#include "Renderer/RenderContext.h"
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
#if LUMINA_MEMORY_TRACKING
        // Base tracking is always on; just stop the heavy per-alloc call-stack capture
        // so it doesn't keep walking stacks after the window is closed.
        Memory::SetCaptureCallstacks(false);
#endif
    }

    void FMemoryProfilerEditorTool::DrawHelpMenu()
    {
        DrawHelpTextRow("Finding a leak",
            "Click Set Baseline at a known-good moment, let the suspect run, then watch the "
            "Delta column. The category that keeps climbing is your leak (the table is sorted "
            "by growth). Then tick Capture call stacks and read Top Call Sites for the exact line.");
        DrawHelpTextRow("Categories",
            "A category is just the name passed to LUMINA_MEMORY_SCOPE(\"...\") around a region "
            "of code. Anything not inside a scope is attributed to Default.");
        DrawHelpTextRow("Cost",
            "Category tracking is always on in Debug/Development and compiled out entirely in "
            "Shipping. Call-stack capture is a heavier, separate toggle, switched off when this "
            "window closes.");
        DrawHelpTextRow("Scope",
            "Tracks CPU allocations routed through Memory::Malloc (engine-wide, including EASTL "
            "and global new). GPU/VMA memory is not here -- see Renderer Info.");
    }

    void FMemoryProfilerEditorTool::DrawWindow(bool bIsFocused)
    {
#if !LUMINA_MEMORY_TRACKING
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
            LE_ICON_ALERT " Memory tracking is compiled out in Shipping builds.");
        return;
#else
        // Refresh the snapshot on a timer so the numbers are readable, not a blur.
        RefreshTimer += GEngine->GetDeltaTime();
        if (RefreshTimer >= 0.4f || Categories.empty())
        {
            RefreshTimer = 0.0f;
            Categories.resize(256);
            const uint32 N = Memory::GetCategoryStats(Categories.data(), (uint32)Categories.size());
            Categories.resize(N);
        }

        DrawControls();
        ImGui::Separator();
        DrawSummary();
        ImGui::Separator();
        ImGui::Spacing();

        const bool bCapturing = Memory::IsCapturingCallstacks();

        // Categories take the upper region; call sites (when capturing) get the lower third.
        const float Avail      = ImGui::GetContentRegionAvail().y;
        const float TableHeight = bCapturing ? Avail * 0.55f : Avail;
        DrawCategoryTable(TableHeight);

        if (bCapturing)
        {
            ImGui::Spacing();
            DrawCallSites();
        }
#endif
    }

#if LUMINA_MEMORY_TRACKING

    void FMemoryProfilerEditorTool::DrawControls()
    {
        if (ImGui::Button(LE_ICON_FLAG " Set Baseline"))
        {
            Baseline = Categories;
            bHasBaseline = true;
        }
        ImGuiX::TextTooltip("Snapshot current live bytes per category. The Delta column then shows "
                            "growth since this moment -- the anchor for leak hunting.");

        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_FLAG_OUTLINE " Clear Baseline"))
        {
            bHasBaseline = false;
        }

        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_REFRESH " Reset"))
        {
            Memory::ResetTracking();
            Baseline.clear();
            bHasBaseline = false;
        }
        ImGuiX::TextTooltip("Zero all counters and start a fresh capture.");

        ImGui::SameLine();
        ImGui::Dummy(ImVec2(16, 0));
        ImGui::SameLine();

        bool bCapture = Memory::IsCapturingCallstacks();
        if (ImGui::Checkbox("Capture call stacks", &bCapture))
        {
            Memory::SetCaptureCallstacks(bCapture);
        }
        ImGuiX::TextTooltip("Record the stack of every allocation so Top Call Sites can name the "
                            "exact leaking line. Heavier -- turn on once a category is confirmed leaking.");
    }

    void FMemoryProfilerEditorTool::DrawSummary()
    {
        // The tracker is the authoritative CPU number -- it counts every byte through
        // Memory::Malloc independently of rpmalloc's internal statistics. "Untracked
        // residency" is whatever the OS reports for the process beyond that (CRT malloc
        // from 3rd-party C libs, the executable image, thread stacks, GPU host-visible
        // mappings, driver overhead). GPU device memory is a separate pool, reported below.
        const size_t Process   = Platform::GetProcessMemoryUsageBytes();
        const size_t Tracked   = Memory::GetTrackedLiveBytes();
        const size_t Untracked = (Process > Tracked) ? (Process - Tracked) : 0;
        const uint64 GPU       = GRenderContext ? GRenderContext->GetAllocatedMemory() : 0;

        auto Row = [](const char* Label, const FString& Value, ImVec4 Color, const char* Tip = nullptr)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(Label);
            if (Tip && ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", Tip); }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(Color, "%s", Value.c_str());
        };

        if (ImGui::BeginTable("Coverage", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 230);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 220);

            Row("Process (RSS)", ImGuiX::FormatSize(Process), ImVec4(1, 1, 1, 1),
                "Total resident memory for the process, as the OS sees it.");
            Row("   CPU tracked (our allocator)",
                FString().sprintf("%s  (%llu allocs)", ImGuiX::FormatSize(Tracked).c_str(),
                    (unsigned long long)Memory::GetTrackedLiveCount()),
                ImVec4(0.4f, 1.0f, 0.6f, 1.0f),
                "Live CPU bytes through Memory::Malloc / our global new, counted by the tracker. Authoritative.");
            Row("   untracked residency", ImGuiX::FormatSize(Untracked), ImVec4(0.9f, 0.5f, 0.5f, 1.0f),
                "Process minus tracked: CRT malloc from 3rd-party C libs, executable image, thread stacks, "
                "GPU host-visible mappings, driver overhead.");

            ImGui::TableNextRow();   // spacer
            Row("GPU device memory (VMA)", ImGuiX::FormatSize((size_t)GPU), ImVec4(1.0f, 0.7f, 0.4f, 1.0f),
                "vkAllocateMemory via VMA (VRAM + host-visible). A separate pool from the CPU heap; "
                "depending on heap type and OS it may not be counted in Process RSS above.");

            ImGui::EndTable();
        }

        const uint64 Overflow = Memory::GetTrackingOverflowCount();
        if (Overflow > 0)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                LE_ICON_ALERT " %llu allocations untracked (ledger full)", (unsigned long long)Overflow);
        }
    }

    void FMemoryProfilerEditorTool::DrawCategoryTable(float Height)
    {
        auto FindBaseline = [this](const char* Name) -> const Memory::FMemoryCategoryStats*
        {
            if (bHasBaseline)
            {
                for (const Memory::FMemoryCategoryStats& B : Baseline)
                {
                    if (std::strcmp(B.Name, Name) == 0)
                    {
                        return &B;
                    }
                }
            }
            return nullptr;
        };

        struct FRow
        {
            const Memory::FMemoryCategoryStats* S;
            int64                               DeltaBytes;
            int64                               DeltaCount;
        };

        TVector<FRow> Rows;
        Rows.reserve(Categories.size());
        for (const Memory::FMemoryCategoryStats& S : Categories)
        {
            const Memory::FMemoryCategoryStats* B = FindBaseline(S.Name);
            FRow Row;
            Row.S          = &S;
            Row.DeltaBytes = B ? (int64)S.LiveBytes - (int64)B->LiveBytes : 0;
            Row.DeltaCount = B ? (int64)S.LiveCount - (int64)B->LiveCount : 0;
            Rows.push_back(Row);
        }

        // Sort so the worst grower (or biggest live, with no baseline) is on top.
        eastl::sort(Rows.begin(), Rows.end(), [this](const FRow& A, const FRow& B)
        {
            if (bHasBaseline) { return A.DeltaBytes > B.DeltaBytes; }
            return A.S->LiveBytes > B.S->LiveBytes;
        });

        // Leak hint sits just above the table.
        if (bHasBaseline && !Rows.empty() && Rows[0].DeltaBytes > 0)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                LE_ICON_ALERT " Growing fastest: %s  (+%s)",
                Rows[0].S->Name, ImGuiX::FormatSize((size_t)Rows[0].DeltaBytes).c_str());
        }
        else if (!bHasBaseline)
        {
            ImGui::TextDisabled(LE_ICON_INFORMATION " Set a baseline, then watch the Delta column.");
        }
        else
        {
            ImGui::TextDisabled(LE_ICON_INFORMATION " No category has grown since the baseline.");
        }
        ImGui::Spacing();

        const ImGuiTableFlags Flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                    | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

        if (ImGui::BeginTable("Categories", 6, Flags, ImVec2(0, Height)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Category",   ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Live",       ImGuiTableColumnFlags_WidthFixed, 110);
            ImGui::TableSetupColumn("Allocs",     ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Delta",      ImGuiTableColumnFlags_WidthFixed, 120);
            ImGui::TableSetupColumn("Peak",       ImGuiTableColumnFlags_WidthFixed, 110);
            ImGui::TableSetupColumn("Alloc/Free", ImGuiTableColumnFlags_WidthFixed, 150);
            ImGui::TableHeadersRow();

            for (const FRow& Row : Rows)
            {
                const Memory::FMemoryCategoryStats& S = *Row.S;
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(S.Name[0] ? S.Name : "Default");

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(ImGuiX::FormatSize(S.LiveBytes).c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%llu", (unsigned long long)S.LiveCount);

                ImGui::TableSetColumnIndex(3);
                if (!bHasBaseline)
                {
                    ImGui::TextDisabled("-");
                }
                else if (Row.DeltaBytes > 0)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "+%s",
                        ImGuiX::FormatSize((size_t)Row.DeltaBytes).c_str());
                }
                else if (Row.DeltaBytes < 0)
                {
                    ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1.0f), "-%s",
                        ImGuiX::FormatSize((size_t)(-Row.DeltaBytes)).c_str());
                }
                else
                {
                    ImGui::TextDisabled("0");
                }

                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(ImGuiX::FormatSize(S.PeakBytes).c_str());

                ImGui::TableSetColumnIndex(5);
                ImGui::TextDisabled("%llu / %llu",
                    (unsigned long long)S.TotalAllocs, (unsigned long long)S.TotalFrees);
            }

            ImGui::EndTable();
        }
    }

    void FMemoryProfilerEditorTool::DrawCallSites()
    {
        if (!ImGui::CollapsingHeader(LE_ICON_LIST_BOX " Top Call Sites", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        // Ranking mode: live bytes finds leaks/persistent; total allocs finds transient churn
        // (the per-frame allocations to eliminate). They surface very different call sites.
        ImGui::TextUnformatted("Rank by:");
        ImGui::SameLine();
        if (ImGui::RadioButton("Live bytes", !bSortCallSitesByAllocs)) { bSortCallSitesByAllocs = false; }
        ImGui::SameLine();
        if (ImGui::RadioButton("Total allocs (churn)", bSortCallSitesByAllocs)) { bSortCallSitesByAllocs = true; }

        const Memory::ECallSiteSort Sort = bSortCallSitesByAllocs
            ? Memory::ECallSiteSort::TotalAllocs : Memory::ECallSiteSort::LiveBytes;

        static constexpr uint32 kMaxSites = 64;
        Memory::FCallSiteStat Sites[kMaxSites];
        const uint32 NumSites = Memory::GetTopCallSites(Sites, kMaxSites, Sort);

        if (NumSites == 0)
        {
            ImGui::TextDisabled("No call sites captured yet -- give it a few frames.");
            return;
        }

        char SymBuf[512];

        // One-click dump of every shown site (with full symbolized stacks) to the clipboard,
        // for pasting into a review. Builds into a growable string since it can be large.
        ImGui::SameLine();
        if (ImGui::SmallButton(LE_ICON_CONTENT_COPY " Copy all"))
        {
            FString Report;
            char Line[640];
            std::snprintf(Line, sizeof(Line),
                "=== Memory: top %u call sites by %s (tracked live %s, %llu allocs) ===\n",
                NumSites, bSortCallSitesByAllocs ? "total allocs" : "live bytes",
                ImGuiX::FormatSize(Memory::GetTrackedLiveBytes()).c_str(),
                (unsigned long long)Memory::GetTrackedLiveCount());
            Report += Line;

            for (uint32 i = 0; i < NumSites; ++i)
            {
                const Memory::FCallSiteStat& S = Sites[i];
                std::snprintf(Line, sizeof(Line),
                    "\n[%u] live %s (%llu live) | %llu total allocs | [%s]\n",
                    i + 1,
                    ImGuiX::FormatSize(S.LiveBytes).c_str(),
                    (unsigned long long)S.LiveCount,
                    (unsigned long long)S.TotalAllocs,
                    S.CatName[0] ? S.CatName : "Default");
                Report += Line;

                for (uint32 f = 0; f < S.FrameCount; ++f)
                {
                    Memory::ResolveSymbol(S.Frames[f], SymBuf, sizeof(SymBuf));
                    Report += "    ";
                    Report += SymBuf;
                    Report += "\n";
                }
            }
            ImGui::SetClipboardText(Report.c_str());
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%u sites)", NumSites);

        for (uint32 i = 0; i < NumSites; ++i)
        {
            const Memory::FCallSiteStat& Site = Sites[i];

            SymBuf[0] = '\0';
            if (Site.FrameCount > 0)
            {
                Memory::ResolveSymbol(Site.Frames[0], SymBuf, sizeof(SymBuf));
            }

            ImGui::PushID((int)i);
            const bool bOpen = ImGui::TreeNode("site", "%s live (%llu allocs) - %s [%s]",
                ImGuiX::FormatSize(Site.LiveBytes).c_str(),
                (unsigned long long)Site.TotalAllocs,
                SymBuf[0] ? SymBuf : "(no frames)",
                Site.CatName[0] ? Site.CatName : "Default");

            if (bOpen)
            {
                if (ImGui::SmallButton(LE_ICON_CONTENT_COPY " Copy stack"))
                {
                    char Clip[8192];
                    int Off = std::snprintf(Clip, sizeof(Clip), "live %s (%llu live, %llu total allocs) [%s]\n",
                        ImGuiX::FormatSize(Site.LiveBytes).c_str(),
                        (unsigned long long)Site.LiveCount,
                        (unsigned long long)Site.TotalAllocs,
                        Site.CatName[0] ? Site.CatName : "Default");

                    for (uint32 f = 0; f < Site.FrameCount && Off > 0 && Off < (int)sizeof(Clip); ++f)
                    {
                        Memory::ResolveSymbol(Site.Frames[f], SymBuf, sizeof(SymBuf));
                        const int N = std::snprintf(Clip + Off, sizeof(Clip) - Off, "  %s\n", SymBuf);
                        if (N < 0) { break; }
                        Off += N;
                    }
                    ImGui::SetClipboardText(Clip);
                }

                for (uint32 f = 0; f < Site.FrameCount; ++f)
                {
                    Memory::ResolveSymbol(Site.Frames[f], SymBuf, sizeof(SymBuf));
                    ImGui::TextUnformatted(SymBuf);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

#else // !LUMINA_MEMORY_TRACKING

    void FMemoryProfilerEditorTool::DrawControls() {}
    void FMemoryProfilerEditorTool::DrawSummary() {}
    void FMemoryProfilerEditorTool::DrawCategoryTable(float) {}
    void FMemoryProfilerEditorTool::DrawCallSites() {}

#endif
}
