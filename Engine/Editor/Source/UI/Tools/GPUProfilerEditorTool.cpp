#include "GPUProfilerEditorTool.h"

#include "implot.h"
#include "Core/Console/ConsoleVariable.h"
#include "Renderer/GPUProfiler/GPUProfiler.h"
#include "Paths/Paths.h"
#include "Platform/Filesystem/FileHelper.h"
#include "Platform/Process/PlatformProcess.h"

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
            "GPU profiling is gated on the r.GPUProfiler.Enabled CVar, toggleable from the gear menu. "
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
            "In C++: FGPUProfileScope Scope(CmdBuffer, \"YourPassName\");, scopes nest naturally.");
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
            ImGui::Text("Barriers: %u buffer  |  %u image  |  %u total",
                Frame->NumBufferBarriers,
                Frame->NumImageBarriers,
                Frame->NumBufferBarriers + Frame->NumImageBarriers);
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
        if (ImGui::CollapsingHeader("Barriers", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawBarriers();
        }

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

    static const char* BarrierPhaseName(EGPUBarrierPhase Phase)
    {
        switch (Phase)
        {
        case EGPUBarrierPhase::Pass:                return "Pass";
        case EGPUBarrierPhase::RestoreInitialState: return "Restore";
        case EGPUBarrierPhase::Copy:                return "Copy";
        case EGPUBarrierPhase::Clear:               return "Clear";
        default:                                    return "Other";
        }
    }

    static ImVec4 BarrierPhaseColor(EGPUBarrierPhase Phase)
    {
        switch (Phase)
        {
        case EGPUBarrierPhase::RestoreInitialState: return ImVec4(1.00f, 0.55f, 0.25f, 1.0f); // orange: the suspect set
        case EGPUBarrierPhase::Copy:                return ImVec4(0.55f, 0.80f, 1.00f, 1.0f);
        case EGPUBarrierPhase::Clear:               return ImVec4(0.80f, 0.70f, 1.00f, 1.0f);
        default:                                    return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        }
    }

    // Render the whole resolved frame's barrier set as plain text, independent of the UI filters,
    // for export to file or clipboard. Pipe-delimited so it pastes cleanly into a spreadsheet.
    static FString BuildBarrierExportText(const FGPUProfileFrame* Frame)
    {
        FString Out;
        char Line[512];

        auto Appendf = [&](const char* Fmt, auto... Args)
        {
            snprintf(Line, sizeof(Line), Fmt, Args...);
            Out += Line;
        };

        uint32 NumPass = 0, NumRestore = 0, NumCopy = 0, NumOther = 0;
        uint32 NumRedundant = 0, NumUnscoped = 0, NumImage = 0, NumBuffer = 0;
        for (const FGPUBarrierRecord& B : Frame->Barriers)
        {
            switch (B.Phase)
            {
            case EGPUBarrierPhase::Pass:                ++NumPass;    break;
            case EGPUBarrierPhase::RestoreInitialState: ++NumRestore; break;
            case EGPUBarrierPhase::Copy:                ++NumCopy;    break;
            default:                                    ++NumOther;   break;
            }
            if (B.bRedundant)     ++NumRedundant;
            if (B.ScopeIndex < 0) ++NumUnscoped;
            if (B.bImage)         ++NumImage; else ++NumBuffer;
        }

        Appendf("Lumina GPU Barrier Export\n");
        Appendf("Frame #%llu    GPU Total: %.3f ms\n",
            (unsigned long long)Frame->FrameNumber, Frame->TotalTimeMs);
        Appendf("Captured %u barriers (%u image / %u buffer)\n",
            (uint32)Frame->Barriers.size(), NumImage, NumBuffer);
        Appendf("By phase: %u Pass, %u Restore, %u Copy, %u Other\n",
            NumPass, NumRestore, NumCopy, NumOther);
        Appendf("Redundant (before==after): %u    Unscoped: %u    Dropped: %u\n",
            NumRedundant, NumUnscoped, Frame->NumDroppedBarriers);

        // Per-resource aggregation, sorted by barrier count.
        struct FAgg { uint32 Count = 0; uint32 Redundant = 0; bool bImage = false; };
        TVector<eastl::pair<FFixedString, FAgg>> Aggregates;
        for (const FGPUBarrierRecord& B : Frame->Barriers)
        {
            bool bFound = false;
            for (auto& Pair : Aggregates)
            {
                if (Pair.first == B.ResourceName)
                {
                    ++Pair.second.Count;
                    if (B.bRedundant) { ++Pair.second.Redundant; }
                    bFound = true;
                    break;
                }
            }
            if (!bFound)
            {
                FAgg Agg; Agg.Count = 1; Agg.Redundant = B.bRedundant ? 1 : 0; Agg.bImage = B.bImage;
                Aggregates.push_back({ B.ResourceName, Agg });
            }
        }
        eastl::sort(Aggregates.begin(), Aggregates.end(),
            [](const auto& A, const auto& B) { return A.second.Count > B.second.Count; });

        Out += "\n== Per-resource (count | type | redundant | resource) ==\n";
        for (const auto& Pair : Aggregates)
        {
            Appendf("%4u | %s | %4u | %s\n",
                Pair.second.Count, Pair.second.bImage ? "img" : "buf",
                Pair.second.Redundant, Pair.first.c_str());
        }

        Out += "\n== All barriers (resource | type | transition | sub | scope | phase | redundant) ==\n";
        uint32 Index = 0;
        for (const FGPUBarrierRecord& B : Frame->Barriers)
        {
            char Sub[32];
            if (B.bImage && !B.bEntireResource)
            {
                snprintf(Sub, sizeof(Sub), "m%u+%u s%u", B.Mip, B.NumMips, B.ArraySlice);
            }
            else
            {
                snprintf(Sub, sizeof(Sub), "%s", B.bImage ? "all" : "-");
            }

            const char* ScopeName = "<unscoped>";
            if (B.ScopeIndex >= 0 && B.ScopeIndex < (int32)Frame->Scopes.size())
            {
                ScopeName = Frame->Scopes[B.ScopeIndex].Name.c_str();
            }

            Appendf("%4u | %s | %s | %s -> %s | %s | %s | %s | %s\n",
                Index++,
                B.ResourceName.c_str(),
                B.bImage ? "img" : "buf",
                B.Before.c_str(), B.After.c_str(),
                Sub,
                ScopeName,
                BarrierPhaseName(B.Phase),
                B.bRedundant ? "redundant" : "");
        }

        return Out;
    }

    void FGPUProfilerEditorTool::DrawBarriers()
    {
        const FGPUProfileFrame* Frame = FGPUProfiler::Get().GetLatestResolvedFrame();
        if (Frame == nullptr)
        {
            ImGui::TextDisabled("No resolved frame.");
            return;
        }

        // Summary: split the frame total into the categories that matter for cutting waste.
        uint32 NumPass = 0, NumRestore = 0, NumCopy = 0, NumOther = 0;
        uint32 NumRedundant = 0, NumUnscoped = 0, NumImage = 0, NumBuffer = 0;
        for (const FGPUBarrierRecord& B : Frame->Barriers)
        {
            switch (B.Phase)
            {
            case EGPUBarrierPhase::Pass:                ++NumPass;    break;
            case EGPUBarrierPhase::RestoreInitialState: ++NumRestore; break;
            case EGPUBarrierPhase::Copy:                ++NumCopy;    break;
            default:                                    ++NumOther;   break;
            }
            if (B.bRedundant)      ++NumRedundant;
            if (B.ScopeIndex < 0)  ++NumUnscoped;
            if (B.bImage)          ++NumImage; else ++NumBuffer;
        }

        const uint32 Total = (uint32)Frame->Barriers.size();
        ImGui::Text("Captured %u barriers  (%u image / %u buffer)", Total, NumImage, NumBuffer);
        ImGui::Text("By phase:  ");
        ImGui::SameLine(); ImGui::TextColored(BarrierPhaseColor(EGPUBarrierPhase::Pass),    "%u Pass", NumPass);
        ImGui::SameLine(); ImGui::TextColored(BarrierPhaseColor(EGPUBarrierPhase::RestoreInitialState), "  %u Restore", NumRestore);
        if (NumCopy)  { ImGui::SameLine(); ImGui::TextColored(BarrierPhaseColor(EGPUBarrierPhase::Copy), "  %u Copy", NumCopy); }
        if (NumOther) { ImGui::SameLine(); ImGui::Text("  %u Other", NumOther); }
        ImGui::SameLine(); ImGui::TextDisabled("  |  %u redundant (before==after)", NumRedundant);

        if (NumRestore > 0)
        {
            ImGui::TextColored(BarrierPhaseColor(EGPUBarrierPhase::RestoreInitialState),
                LE_ICON_INFORMATION " %u barriers are end-of-frame keep-initial-state restores "
                "(invisible in the scope tree). These are the first place to look for waste.", NumRestore);
        }
        if (Frame->NumDroppedBarriers > 0)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                "Capture truncated: %u barriers dropped (raise MaxBarrierRecordsPerFrame).", Frame->NumDroppedBarriers);
        }

        // Export the full (unfiltered) frame as text.
        const bool bHaveBarriers = !Frame->Barriers.empty();
        if (!bHaveBarriers) { ImGui::BeginDisabled(); }

        if (ImGui::Button(LE_ICON_EXPORT " Export to File"))
        {
            const FString Text = BuildBarrierExportText(Frame);

            FFixedString Dir = Paths::Combine(Paths::GetEngineDirectory(), "Saved", "BarrierExports");
            Paths::CreateDirectories(FStringView(Dir.data(), Dir.size()));

            char FileName[64];
            snprintf(FileName, sizeof(FileName), "barriers_frame_%llu.txt",
                (unsigned long long)Frame->FrameNumber);
            FFixedString FullPath = Paths::Combine(FStringView(Dir.data(), Dir.size()), FileName);

            if (FileHelper::SaveStringToFile(FStringView(Text.c_str(), Text.size()),
                                             FStringView(FullPath.data(), FullPath.size())))
            {
                BarrierExportStatus = FString("Saved: ") + FullPath.c_str();
                LOG_INFO("Exported {0} GPU barriers to {1}", (uint32)Frame->Barriers.size(), FullPath.c_str());
                Platform::ShowFileInExplorer(UTF8_TO_TCHAR(FullPath.c_str()));
            }
            else
            {
                BarrierExportStatus = FString("Failed to write: ") + FullPath.c_str();
                LOG_ERROR("Failed to export GPU barriers to {0}", FullPath.c_str());
            }
        }

        ImGui::SameLine();
        if (ImGui::Button(LE_ICON_CONTENT_COPY " Copy to Clipboard"))
        {
            const FString Text = BuildBarrierExportText(Frame);
            ImGui::SetClipboardText(Text.c_str());
            char StatusBuf[64];
            snprintf(StatusBuf, sizeof(StatusBuf), "Copied %u barriers to clipboard.", (uint32)Frame->Barriers.size());
            BarrierExportStatus = StatusBuf;
        }

        if (!bHaveBarriers) { ImGui::EndDisabled(); }

        if (!BarrierExportStatus.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", BarrierExportStatus.c_str());
        }

        ImGui::Separator();

        // Filters.
        ImGui::Checkbox("Image", &bShowImageBarriers);     ImGui::SameLine();
        ImGui::Checkbox("Buffer", &bShowBufferBarriers);   ImGui::SameLine();
        ImGui::Checkbox("Restores", &bShowRestoreBarriers);ImGui::SameLine();
        ImGui::Checkbox("Redundant only", &bShowRedundantOnly); ImGui::SameLine();
        ImGui::Checkbox("Group by resource", &bGroupBarriersByResource);

        auto Passes = [&](const FGPUBarrierRecord& B) -> bool
        {
            if (B.bImage && !bShowImageBarriers)   return false;
            if (!B.bImage && !bShowBufferBarriers) return false;
            if (B.Phase == EGPUBarrierPhase::RestoreInitialState && !bShowRestoreBarriers) return false;
            if (bShowRedundantOnly && !B.bRedundant) return false;
            return true;
        };

        if (Frame->Barriers.empty())
        {
            ImGui::TextDisabled("No barriers captured this frame.");
            return;
        }

        if (bGroupBarriersByResource)
        {
            // Aggregate per resource so ping-pong / over-transitioning resources stand out.
            struct FAgg { uint32 Count = 0; uint32 Redundant = 0; bool bImage = false; };
            TVector<eastl::pair<FFixedString, FAgg>> Aggregates;
            for (const FGPUBarrierRecord& B : Frame->Barriers)
            {
                if (!Passes(B)) { continue; }
                bool bFound = false;
                for (auto& Pair : Aggregates)
                {
                    if (Pair.first == B.ResourceName)
                    {
                        ++Pair.second.Count;
                        if (B.bRedundant) { ++Pair.second.Redundant; }
                        bFound = true;
                        break;
                    }
                }
                if (!bFound)
                {
                    FAgg Agg; Agg.Count = 1; Agg.Redundant = B.bRedundant ? 1 : 0; Agg.bImage = B.bImage;
                    Aggregates.push_back({ B.ResourceName, Agg });
                }
            }
            eastl::sort(Aggregates.begin(), Aggregates.end(),
                [](const auto& A, const auto& B) { return A.second.Count > B.second.Count; });

            if (ImGui::BeginTable("##BarrierAgg", 4,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 260.0f)))
            {
                ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Type",     ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Barriers", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Redundant",ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                for (const auto& Pair : Aggregates)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(Pair.first.c_str());
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(Pair.second.bImage ? "img" : "buf");
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%u", Pair.second.Count);
                    ImGui::TableSetColumnIndex(3);
                    if (Pair.second.Redundant) ImGui::Text("%u", Pair.second.Redundant);
                    else                       ImGui::TextDisabled("-");
                }
                ImGui::EndTable();
            }
            return;
        }

        if (ImGui::BeginTable("##BarrierList", 6,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 320.0f)))
        {
            ImGui::TableSetupColumn("Resource",   ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type",       ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Transition", ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableSetupColumn("Sub",        ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Scope",      ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Phase",      ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            char SubBuf[32];
            char TransBuf[160];
            for (const FGPUBarrierRecord& B : Frame->Barriers)
            {
                if (!Passes(B)) { continue; }

                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(B.ResourceName.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(B.bImage ? "img" : "buf");

                ImGui::TableSetColumnIndex(2);
                snprintf(TransBuf, sizeof(TransBuf), "%s -> %s", B.Before.c_str(), B.After.c_str());
                if (B.bRedundant)
                {
                    ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f), "%s", TransBuf);
                }
                else
                {
                    ImGui::TextUnformatted(TransBuf);
                }

                ImGui::TableSetColumnIndex(3);
                if (B.bImage && !B.bEntireResource)
                {
                    snprintf(SubBuf, sizeof(SubBuf), "m%u+%u s%u", B.Mip, B.NumMips, B.ArraySlice);
                    ImGui::TextUnformatted(SubBuf);
                }
                else
                {
                    ImGui::TextDisabled(B.bImage ? "all" : "-");
                }

                ImGui::TableSetColumnIndex(4);
                if (B.ScopeIndex >= 0 && B.ScopeIndex < (int32)Frame->Scopes.size())
                {
                    ImGui::TextUnformatted(Frame->Scopes[B.ScopeIndex].Name.c_str());
                }
                else
                {
                    ImGui::TextDisabled("<unscoped>");
                }

                ImGui::TableSetColumnIndex(5);
                ImGui::TextColored(BarrierPhaseColor(B.Phase), "%s", BarrierPhaseName(B.Phase));
            }
            ImGui::EndTable();
        }
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

        if (!ImGui::BeginTable("##GPUScopes", 5,
                ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
        {
            return;
        }

        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("% Frame", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Barriers (buf/img)", ImGuiTableColumnFlags_WidthFixed, 120.0f);
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
                    if (Scope.NumBufferBarriers || Scope.NumImageBarriers)
                    {
                        ImGui::Text("%u / %u", Scope.NumBufferBarriers, Scope.NumImageBarriers);
                    }
                    else
                    {
                        ImGui::TextDisabled("-");
                    }

                    ImGui::TableSetColumnIndex(4);
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
