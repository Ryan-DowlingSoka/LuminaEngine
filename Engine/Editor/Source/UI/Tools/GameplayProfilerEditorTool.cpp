#include "GameplayProfilerEditorTool.h"

#include <cfloat>
#include <EASTL/algorithm.h>

#include "imgui.h"
#include "Core/Profiler/GameplayProfiler.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"

namespace Lumina
{
    void FGameplayProfilerEditorTool::OnInitialize()
    {
        // Record while this window exists; zero cost once it closes.
        FGameplayProfiler::Get().SetEnabled(true);
        CreateToolWindow("Gameplay Profiler", [&] (bool bIsFocused) { DrawWindow(bIsFocused); });
    }

    void FGameplayProfilerEditorTool::OnDeinitialize(const FUpdateContext&)
    {
        FGameplayProfiler::Get().SetEnabled(false);
    }

    void FGameplayProfilerEditorTool::DrawWindow(bool)
    {
        FGameplayProfiler& Prof = FGameplayProfiler::Get();

        // ---- Controls -----------------------------------------------------------------------------
        ImGui::Checkbox("Freeze", &bFrozen);
        if (bFrozen)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.30f, 1.0f), LE_ICON_PAUSE " FROZEN");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ++DrawTicks;
        const char Spinner[] = { '|', '/', '-', '\\' };
        ImGui::TextColored(ImVec4(0.45f, 0.80f, 0.55f, 1.0f), "live %c", Spinner[(DrawTicks / 6) % 4]);
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##filter", LE_ICON_MAGNIFY " Filter scopes", Filter, sizeof(Filter));

        if (!bFrozen)
        {
            DisplayFrame = Prof.GetLatest();
        }

        // ---- Header: total gameplay time vs frame budget + history graph -------------------------
        const double TotalMs = DisplayFrame.TotalMs;
        constexpr float BudgetMs = 16.667f;
        const float Pct = static_cast<float>(TotalMs / BudgetMs) * 100.0f;

        ImGui::Separator();
        ImGui::TextUnformatted("Gameplay");
        ImGui::SameLine();
        const ImVec4 HotColor = (TotalMs > BudgetMs) ? ImVec4(0.96f, 0.45f, 0.35f, 1.0f) : ImVec4(0.55f, 0.86f, 0.62f, 1.0f);
        ImGui::TextColored(HotColor, "%.3f ms", TotalMs);
        ImGui::SameLine();
        ImGui::TextDisabled("(%.0f%% of a 16.7 ms frame)", Pct);

        const TVector<float>& History = Prof.GetFrameTotalHistory();
        if (!History.empty())
        {
            ImGui::PlotLines("##frametotals", History.data(), static_cast<int>(History.size()), 0, nullptr, 0.0f, FLT_MAX, ImVec2(-1.0f, 46.0f));
        }

        ImGui::Separator();

        // ---- Table --------------------------------------------------------------------------------
        const ImGuiTableFlags Flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
            | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;

        if (ImGui::BeginTable("##gpprof", 7, Flags))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Scope",    ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("Calls",    ImGuiTableColumnFlags_WidthFixed,  52.0f);
            ImGui::TableSetupColumn("Total ms", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_DefaultSort, 72.0f);
            ImGui::TableSetupColumn("Self ms",  ImGuiTableColumnFlags_WidthFixed,  72.0f);
            ImGui::TableSetupColumn("Avg ms",   ImGuiTableColumnFlags_WidthFixed,  72.0f);
            ImGui::TableSetupColumn("%",        ImGuiTableColumnFlags_WidthFixed,  48.0f);
            ImGui::TableSetupColumn("History",  ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort, 1.4f);
            ImGui::TableHeadersRow();

            TVector<const FGameplayProfileEntry*> Rows;
            Rows.reserve(DisplayFrame.Entries.size());
            for (const FGameplayProfileEntry& Entry : DisplayFrame.Entries)
            {
                if (Filter[0] == '\0' || Entry.Name.find(Filter) != FFixedString::npos)
                {
                    Rows.push_back(&Entry);
                }
            }

            int  SortCol = 2;
            bool Ascending = false;
            if (ImGuiTableSortSpecs* Specs = ImGui::TableGetSortSpecs())
            {
                if (Specs->SpecsCount > 0)
                {
                    SortCol   = Specs->Specs[0].ColumnIndex;
                    Ascending = Specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
                }
            }

            auto Avg = [](const FGameplayProfileEntry* E) { return E->Calls ? E->InclusiveMs / E->Calls : 0.0; };
            eastl::sort(Rows.begin(), Rows.end(), [&](const FGameplayProfileEntry* A, const FGameplayProfileEntry* B)
            {
                double Cmp;
                switch (SortCol)
                {
                    case 0:  Cmp = static_cast<double>(A->Name.compare(B->Name)); break;
                    case 1:  Cmp = static_cast<double>(A->Calls) - static_cast<double>(B->Calls); break;
                    case 3:  Cmp = A->ExclusiveMs - B->ExclusiveMs; break;
                    case 4:  Cmp = Avg(A) - Avg(B); break;
                    default: Cmp = A->InclusiveMs - B->InclusiveMs; break;   // Total / %
                }
                return Ascending ? (Cmp < 0.0) : (Cmp > 0.0);
            });

            for (const FGameplayProfileEntry* Entry : Rows)
            {
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Entry->Name.c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%u", Entry->Calls);

                ImGui::TableNextColumn();
                ImGui::Text("%.3f", Entry->InclusiveMs);

                ImGui::TableNextColumn();
                ImGui::Text("%.3f", Entry->ExclusiveMs);

                ImGui::TableNextColumn();
                ImGui::Text("%.3f", Avg(Entry));

                ImGui::TableNextColumn();
                const float RowPct = (TotalMs > 0.0) ? static_cast<float>(Entry->InclusiveMs / TotalMs * 100.0) : 0.0f;
                ImGui::Text("%.0f%%", RowPct);

                ImGui::TableNextColumn();
                if (const TVector<float>* EntryHistory = Prof.GetEntryHistory(Entry->Hash))
                {
                    if (!EntryHistory->empty())
                    {
                        ImGui::PushID(Entry);
                        ImGui::PlotLines("##h", EntryHistory->data(), static_cast<int>(EntryHistory->size()), 0, nullptr, 0.0f, FLT_MAX, ImVec2(-1.0f, 18.0f));
                        ImGui::PopID();
                    }
                }
            }

            ImGui::EndTable();
        }

        if (DisplayFrame.Entries.empty())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("No gameplay scopes yet. Enter Play to capture script/system updates, or");
            ImGui::TextDisabled("instrument C# with  using (Profiler.Sample(\"Name\")) { ... }");
        }
    }
}
