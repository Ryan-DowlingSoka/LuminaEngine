#include "GameplayInsightsEditorTool.h"

#include <cfloat>
#include <EASTL/algorithm.h>

#include "imgui.h"
#include "Core/Profiler/GameplayProfiler.h"
#include "Core/UpdateStage.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"
#include "World/Entity/Systems/SystemAccess.h"
#include "World/WorldManager.h"
#include "World/WorldContext.h"

namespace Lumina
{
    namespace
    {
        const char* StageName(uint8 Stage)
        {
            static const char* Names[] = { "FrameStart", "PrePhysics", "DuringPhysics", "PostPhysics", "FrameEnd", "Paused" };
            return Stage < (uint8)EUpdateStage::Max ? Names[Stage] : "?";
        }

        // Distinct, stable tint per stage so a span/row is readable at a glance.
        ImU32 StageColor(uint8 Stage)
        {
            switch ((EUpdateStage)Stage)
            {
                case EUpdateStage::FrameStart:    return IM_COL32( 92, 148, 220, 255);
                case EUpdateStage::PrePhysics:    return IM_COL32( 96, 188, 132, 255);
                case EUpdateStage::DuringPhysics: return IM_COL32(208, 168,  72, 255);
                case EUpdateStage::PostPhysics:   return IM_COL32(196, 120,  92, 255);
                case EUpdateStage::FrameEnd:      return IM_COL32(168, 124, 208, 255);
                case EUpdateStage::Paused:        return IM_COL32(132, 132, 140, 255);
                default:                          return IM_COL32(120, 120, 128, 255);
            }
        }

        // Comma-joined display names for a set of access ids (entt::type_hash), resolved via the runtime registry.
        FString AccessList(const TVector<uint32>& Ids)
        {
            FString Out;
            for (uint32 Id : Ids)
            {
                const char* Name = GetAccessTypeName(Id);
                if (!Out.empty())
                {
                    Out += ", ";
                }
                Out += Name ? Name : "<unknown>";
            }
            return Out;
        }
    }

    void FGameplayInsightsEditorTool::OnInitialize()
    {
        // Record while this window exists; zero cost once it closes.
        FGameplayProfiler::Get().SetEnabled(true);
        CreateToolWindow("Gameplay Insights", [&] (bool bIsFocused) { DrawWindow(bIsFocused); });
    }

    void FGameplayInsightsEditorTool::OnDeinitialize(const FUpdateContext&)
    {
        FGameplayProfiler::Get().SetEnabled(false);
    }

    CWorld* FGameplayInsightsEditorTool::ResolveWorld() const
    {
        if (GWorldManager == nullptr)
        {
            return nullptr;
        }

        CWorld* Editor = nullptr;
        for (const TUniquePtr<FWorldContext>& Context : GWorldManager->GetContexts())
        {
            if (!Context || !Context->World.IsValid())
            {
                continue;
            }
            // Prefer a live gameplay world (where every stage actually ticks); fall back to the editor world.
            if (Context->Type == EWorldType::Game || Context->Type == EWorldType::Simulation)
            {
                return Context->World.Get();
            }
            if (Context->Type == EWorldType::Editor)
            {
                Editor = Context->World.Get();
            }
        }
        return Editor;
    }

    void FGameplayInsightsEditorTool::RefreshSchedule()
    {
        if (CWorld* World = ResolveWorld())
        {
            World->GetSystemSchedule(Schedule);
        }
        else
        {
            Schedule.clear();
        }
    }

    void FGameplayInsightsEditorTool::DrawWindow(bool)
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

        if (!bFrozen)
        {
            DisplayFrame = Prof.GetLatest();
            RefreshSchedule();
        }

        if (ImGui::BeginTabBar("##insights"))
        {
            if (ImGui::BeginTabItem(LE_ICON_SITEMAP " Schedule"))
            {
                DrawSchedule();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(LE_ICON_TABLE " Stats"))
            {
                DrawStats();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(LE_ICON_INFORMATION " Detail"))
            {
                DrawDetail();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    // ---- Schedule: parallel batches per stage with each system's declared access -----------------------
    void FGameplayInsightsEditorTool::DrawSchedule()
    {
        CWorld* World = ResolveWorld();
        if (World == nullptr || Schedule.empty())
        {
            ImGui::TextDisabled("No active world with registered systems.");
            return;
        }

        ImGui::TextDisabled("Systems within one batch run concurrently; the next batch waits for it. Order is by stage, then priority.");
        ImGui::Spacing();

        int8_t LastStage = -1;
        int    LastBatch = -1;
        for (const FSystemScheduleEntry& E : Schedule)
        {
            if ((int8_t)E.Stage != LastStage)
            {
                LastStage = (int8_t)E.Stage;
                LastBatch = -1;
                ImGui::Spacing();
                ImU32 C = StageColor(E.Stage);
                ImGui::PushStyleColor(ImGuiCol_Text, C);
                ImGui::SeparatorText(StageName(E.Stage));
                ImGui::PopStyleColor();
            }

            if ((int)E.Batch != LastBatch)
            {
                LastBatch = (int)E.Batch;
                if (E.bExclusive || E.BatchSize <= 1)
                {
                    ImGui::TextDisabled("  Batch %u  [serial]", E.Batch);
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.55f, 0.82f, 0.60f, 1.0f), "  Batch %u  (x%u parallel)", E.Batch, E.BatchSize);
                }
            }
            
            const FString NameStr = E.bManaged ? FString("<C# system>") : E.Name.ToString();
            ImGui::PushID(&E);
            const bool bSel = Selected == E.Name && !E.bManaged;
            if (ImGui::Selectable("##row", bSel, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, 0)) && !E.bManaged)
            {
                Selected = E.Name;
            }
            ImGui::SameLine(28.0f);
            ImGui::TextUnformatted(NameStr.c_str());
            ImGui::SameLine(280.0f);
            if (E.bExclusive)
            {
                ImGui::TextDisabled("exclusive");
            }
            else
            {
                const FString W = AccessList(E.Writes);
                const FString R = AccessList(E.Reads);
                if (!W.empty())
                {
                    ImGui::TextColored(ImVec4(0.92f, 0.55f, 0.45f, 1.0f), "W: %s", W.c_str());
                }
                if (!R.empty())
                {
                    if (!W.empty()) ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.55f, 0.72f, 0.92f, 1.0f), "R: %s", R.c_str());
                }
                if (W.empty() && R.empty())
                {
                    ImGui::TextDisabled("(no component access)");
                }
            }
            ImGui::PopID();
        }
    }

    // ---- Stats: aggregate per-scope CPU timings (scripts, C# systems, Profiler.Sample scopes) ----------
    void FGameplayInsightsEditorTool::DrawStats()
    {
        FGameplayProfiler& Prof = FGameplayProfiler::Get();

        const double TotalMs = DisplayFrame.TotalMs;
        constexpr float BudgetMs = 16.667f;
        const ImVec4 HotColor = (TotalMs > BudgetMs) ? ImVec4(0.96f, 0.45f, 0.35f, 1.0f) : ImVec4(0.55f, 0.86f, 0.62f, 1.0f);
        ImGui::TextUnformatted("Gameplay total");
        ImGui::SameLine();
        ImGui::TextColored(HotColor, "%.3f ms", TotalMs);
        ImGui::SameLine();
        ImGui::TextDisabled("(%.0f%% of 16.7 ms)", static_cast<float>(TotalMs / BudgetMs) * 100.0f);

        const TVector<float>& History = Prof.GetFrameTotalHistory();
        if (!History.empty())
        {
            ImGui::PlotLines("##frametotals", History.data(), static_cast<int>(History.size()), 0, nullptr, 0.0f, FLT_MAX, ImVec2(-1.0f, 40.0f));
        }

        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##filter", LE_ICON_MAGNIFY " Filter scopes", Filter, sizeof(Filter));

        const ImGuiTableFlags Flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
            | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;

        if (ImGui::BeginTable("##gpstats", 7, Flags))
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
                    default: Cmp = A->InclusiveMs - B->InclusiveMs; break;
                }
                return Ascending ? (Cmp < 0.0) : (Cmp > 0.0);
            });

            for (const FGameplayProfileEntry* Entry : Rows)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(Entry->Name.c_str());
                ImGui::TableNextColumn(); ImGui::Text("%u", Entry->Calls);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", Entry->InclusiveMs);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", Entry->ExclusiveMs);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", Avg(Entry));
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

    // ---- Detail: full info for the system selected in the Schedule tab ---------------------------------
    void FGameplayInsightsEditorTool::DrawDetail()
    {
        if (Selected.IsNone())
        {
            ImGui::TextDisabled("Select a system in the Schedule tab to inspect it here.");
            return;
        }

        const FSystemScheduleEntry* Entry = nullptr;
        for (const FSystemScheduleEntry& E : Schedule)
        {
            if (E.Name == Selected)
            {
                Entry = &E;
                break;
            }
        }
        if (Entry == nullptr)
        {
            ImGui::TextDisabled("'%s' is no longer scheduled.", Selected.ToString().c_str());
            return;
        }

        ImGui::SeparatorText(Selected.ToString().c_str());
        ImGui::Text("Stage:      %s", StageName(Entry->Stage));
        ImGui::Text("Priority:   %u", Entry->Priority);
        ImGui::Text("Scheduling: %s", Entry->bExclusive ? "Exclusive (runs alone)"
            : (Entry->BatchSize > 1 ? "Parallel (batched)" : "Parallel-capable (alone this frame)"));

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.92f, 0.55f, 0.45f, 1.0f), "Writes");
        const FString W = AccessList(Entry->Writes);
        ImGui::TextUnformatted(Entry->bExclusive ? "(everything, exclusive)" : (W.empty() ? "(none)" : W.c_str()));

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.55f, 0.72f, 0.92f, 1.0f), "Reads");
        const FString R = AccessList(Entry->Reads);
        ImGui::TextUnformatted(Entry->bExclusive ? "(everything, exclusive)" : (R.empty() ? "(none)" : R.c_str()));

        // Aggregate timing for this system, if it shows up in the scope table (C# systems do).
        for (const FGameplayProfileEntry& Stat : DisplayFrame.Entries)
        {
            if (Selected.ToString() == Stat.Name.c_str())
            {
                ImGui::Spacing();
                ImGui::SeparatorText("Timing (last frame)");
                ImGui::Text("Calls: %u   Inclusive: %.3f ms   Self: %.3f ms", Stat.Calls, Stat.InclusiveMs, Stat.ExclusiveMs);
                break;
            }
        }
    }
}
