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

    void FScriptsInfoEditorTool::DrawHelpMenu()
    {
        DrawHelpTextRow("Runtime",
            "Live VM stats: loaded scripts, memory usage, and (if LUAI_GCMETRICS is on) per-cycle GC timing.");
        DrawHelpTextRow("API Reference",
            "Browse every class, function, table and value the engine has exposed to Lua. "
            "Walks the live VM globals, so anything you add via NewClass/SetFunction shows up automatically.");
        DrawHelpTextRow("Refresh",
            "Click 'Refresh' on the API tab after hot-reloading stdlib or registering new bindings.");
    }

    void FScriptsInfoEditorTool::RebuildApiCache()
    {
        Lua::FScriptingContext::Get().HarvestGlobalSymbols(CachedSymbols);
        LastHarvestTimeSeconds = ImGui::GetTime();
    }

    void FScriptsInfoEditorTool::DrawWindow(bool bIsFocused)
    {
        if (ImGui::BeginTabBar("ScriptsInfoTabs"))
        {
            if (ImGui::BeginTabItem(LE_ICON_CHART_LINE " Runtime"))
            {
                DrawRuntimeTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(LE_ICON_BOOK_OPEN_PAGE_VARIANT " API Reference"))
            {
                DrawApiReferenceTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void FScriptsInfoEditorTool::DrawRuntimeTab()
    {
        auto& Context = Lua::FScriptingContext::Get();
        const int MemoryUsage = Context.GetScriptMemoryUsageBytes();

        ImGui::Text("Loaded Scripts: %zu", Context.GetAllRegisteredScripts().size());
        ImGuiX::HelpMarker("Scripts compiled and registered with the VM. Auto-tracked when assets are loaded.");

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "|");
        ImGui::SameLine();
        ImGui::Text("Memory Usage: %s", ImGuiX::FormatSize(MemoryUsage).c_str());
        ImGuiX::HelpMarker("Bytes currently allocated by the Lua VM (lua_gc(LUA_GCCOUNT)). Includes all live objects, strings, and userdata.");

        ImGui::Separator();

        static ImGuiTextFilter SearchFilter;
        ImGui::SetNextItemWidth(300.0f);
        SearchFilter.Draw();
        ImGuiX::HelpMarker("Filter scripts by name. Supports inclusive (foo) and exclusive (-bar) terms separated by commas.");

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

    static const char* SymbolKindLabel(Lua::ELuaSymbolKind Kind)
    {
        switch (Kind)
        {
            case Lua::ELuaSymbolKind::Table:    return "table";
            case Lua::ELuaSymbolKind::Function: return "function";
            case Lua::ELuaSymbolKind::Value:    return "value";
        }
        return "?";
    }

    static ImVec4 SymbolKindColor(Lua::ELuaSymbolKind Kind)
    {
        switch (Kind)
        {
            case Lua::ELuaSymbolKind::Table:    return ImVec4(0.55f, 0.78f, 1.00f, 1.0f);
            case Lua::ELuaSymbolKind::Function: return ImVec4(0.85f, 0.75f, 0.40f, 1.0f);
            case Lua::ELuaSymbolKind::Value:    return ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
        }
        return ImVec4(1, 1, 1, 1);
    }

    void FScriptsInfoEditorTool::DrawApiReferenceTab()
    {
        if (LastHarvestTimeSeconds < 0.0)
        {
            RebuildApiCache();
        }

        ImGui::TextWrapped(
            "Browse the live Lua API. This list is harvested from the running VM, so it always "
            "reflects what scripts actually see, including stdlib modules and recently registered classes.");
        ImGui::Separator();

        if (ImGui::Button(LE_ICON_REFRESH " Refresh"))
        {
            RebuildApiCache();
        }
        ImGuiX::TextTooltip("Re-walk the VM globals. Use after hot-reloading stdlib or registering new bindings.");

        ImGui::SameLine();
        ImGui::TextDisabled("(%zu symbols, harvested %.1fs ago)",
            CachedSymbols.size(),
            LastHarvestTimeSeconds >= 0.0 ? ImGui::GetTime() - LastHarvestTimeSeconds : 0.0);

        ImGui::Separator();

        static ImGuiTextFilter SymbolFilter;
        ImGui::SetNextItemWidth(360.0f);
        SymbolFilter.Draw("##ApiFilter");
        ImGuiX::HelpMarker(
            "Filter by dotted path (e.g. 'Engine.', 'Quat.', 'Spawn'). "
            "Comma-separated terms; prefix with '-' to exclude.");

        ImGui::SameLine();
        static bool bShowFunctions = true;
        static bool bShowTables    = true;
        static bool bShowValues    = false;
        ImGui::Checkbox("Functions", &bShowFunctions); ImGui::SameLine();
        ImGui::Checkbox("Tables",    &bShowTables);    ImGui::SameLine();
        ImGui::Checkbox("Values",    &bShowValues);

        ImGui::Separator();

        // Group by top-level path component for readability.
        struct FGroup
        {
            FString                                 Name;
            TVector<const Lua::FLuaSymbol*>         Members;
        };
        TVector<FGroup> Groups;
        THashMap<FString, int32> GroupIndex;

        auto FindOrAddGroup = [&](const FString& Key) -> FGroup&
        {
            auto It = GroupIndex.find(Key);
            if (It != GroupIndex.end())
            {
                return Groups[It->second];
            }
            const int32 Idx = static_cast<int32>(Groups.size());
            FGroup G;
            G.Name = Key;
            Groups.push_back(eastl::move(G));
            GroupIndex.emplace(Key, Idx);
            return Groups.back();
        };

        for (const Lua::FLuaSymbol& Sym : CachedSymbols)
        {
            if (Sym.Kind == Lua::ELuaSymbolKind::Function && !bShowFunctions) continue;
            if (Sym.Kind == Lua::ELuaSymbolKind::Table    && !bShowTables)    continue;
            if (Sym.Kind == Lua::ELuaSymbolKind::Value    && !bShowValues)    continue;

            if (!SymbolFilter.PassFilter(Sym.Path.c_str())) continue;

            FString GroupKey = Sym.Parent.empty() ? Sym.Name : Sym.Parent;
            const auto Dot = GroupKey.find('.');
            if (Dot != FString::npos)
            {
                GroupKey = GroupKey.substr(0, Dot);
            }
            FindOrAddGroup(GroupKey).Members.push_back(&Sym);
        }

        eastl::sort(Groups.begin(), Groups.end(), [](const FGroup& A, const FGroup& B)
        {
            return A.Name < B.Name;
        });

        if (ImGui::BeginChild("ApiList", ImVec2(0, 0), ImGuiChildFlags_Borders))
        {
            for (FGroup& Group : Groups)
            {
                eastl::sort(Group.Members.begin(), Group.Members.end(),
                    [](const Lua::FLuaSymbol* A, const Lua::FLuaSymbol* B) { return A->Path < B->Path; });

                FString Header;
                std::format_to(std::back_inserter(Header), "{}  ({})",
                    Group.Name.c_str(), Group.Members.size());
                if (!ImGui::CollapsingHeader(Header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    continue;
                }

                FString TableId;
                std::format_to(std::back_inserter(TableId), "ApiTable##{}", Group.Name.c_str());
                if (ImGui::BeginTable(TableId.c_str(), 3,
                    ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("Symbol",      ImGuiTableColumnFlags_WidthStretch, 0.55f);
                    ImGui::TableSetupColumn("Kind / Type", ImGuiTableColumnFlags_WidthStretch, 0.20f);
                    ImGui::TableSetupColumn("Detail",      ImGuiTableColumnFlags_WidthStretch, 0.25f);

                    for (const Lua::FLuaSymbol* Sym : Group.Members)
                    {
                        ImGui::TableNextRow();

                        ImGui::TableNextColumn();
                        ImGui::PushStyleColor(ImGuiCol_Text, SymbolKindColor(Sym->Kind));
                        ImGui::TextUnformatted(Sym->Path.c_str());
                        ImGui::PopStyleColor();

                        if (!Sym->Description.empty())
                        {
                            ImGuiX::WrappedTooltip("{}", Sym->Description.c_str());
                        }

                        ImGui::TableNextColumn();
                        if (Sym->Kind == Lua::ELuaSymbolKind::Function)
                        {
                            if (Sym->bIsCFunction)
                            {
                                ImGui::TextDisabled("C function");
                            }
                            else
                            {
                                ImGui::TextDisabled("function");
                            }
                        }
                        else
                        {
                            ImGui::TextDisabled("%s", SymbolKindLabel(Sym->Kind));
                        }

                        ImGui::TableNextColumn();
                        if (Sym->Kind == Lua::ELuaSymbolKind::Function)
                        {
                            FString Sig;
                            Sig.append("(");
                            if (!Sym->ParamNames.empty())
                            {
                                for (size_t i = 0; i < Sym->ParamNames.size(); ++i)
                                {
                                    if (i > 0) Sig.append(", ");
                                    Sig.append(Sym->ParamNames[i].c_str());
                                }
                            }
                            else if (Sym->bIsVararg)
                            {
                                Sig.append("...");
                            }
                            else if (Sym->ParamCount > 0)
                            {
                                for (uint8 i = 0; i < Sym->ParamCount; ++i)
                                {
                                    if (i > 0) Sig.append(", ");
                                    std::format_to(std::back_inserter(Sig), "arg{}", i + 1);
                                }
                            }
                            Sig.append(")");
                            ImGui::TextUnformatted(Sig.c_str());
                        }
                        else if (!Sym->ValuePreview.empty())
                        {
                            ImGui::TextDisabled("%s", Sym->ValuePreview.c_str());
                        }
                    }

                    ImGui::EndTable();
                }
            }
        }
        ImGui::EndChild();
    }
}
