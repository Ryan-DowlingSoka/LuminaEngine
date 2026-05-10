#include "ObjectBrowserEditorTool.h"

#include <EASTL/sort.h>

#include "Core/Object/Object.h"
#include "Core/Object/ObjectArray.h"
#include "Core/Object/ObjectIterator.h"
#include "Core/Object/Package/Package.h"
#include "Core/Object/Class.h"
#include "Core/Object/ObjectFlags.h"

namespace Lumina
{
    void FObjectBrowserEditorTool::OnInitialize()
    {
        CreateToolWindow("Object Browser", [this](bool bIsFocused)
        {
            DrawWindow(bIsFocused);
        });
    }

    void FObjectBrowserEditorTool::OnDeinitialize(const FUpdateContext& UpdateContext)
    {
    }

    void FObjectBrowserEditorTool::DrawHelpMenu()
    {
        DrawHelpTextRow("What this is",
            "Lists every live CObject in the engine — every loaded asset, every transient runtime object, "
            "each grouped by package. Useful when chasing leaks or unexpected references.");
        DrawHelpTextRow("Filters",
            "Search filters by object name. Class filter narrows to a specific CClass (and its subclasses). "
            "Show Only Active hides objects pending destroy.");
        DrawHelpTextRow("Sorting",
            "Toggle Sort By Name to sort alphabetically; otherwise rows are in registration (creation) order — "
            "newest at the bottom.");
        DrawHelpTextRow("Selection",
            "Click a row to inspect its package + class. The package is what FAssetRegistry / save paths use.");
    }

    void FObjectBrowserEditorTool::DrawWindow(bool bIsFocused)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.26f, 0.59f, 0.98f, 0.25f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.26f, 0.59f, 0.98f, 0.35f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.26f, 0.59f, 0.98f, 0.45f));

        const uint32 TotalObjects = GObjectArray.GetNumAliveObjects();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.14f, 0.18f, 1.0f));
        ImGui::BeginChild("##StatsBar", ImVec2(0, 70.0f), true, ImGuiWindowFlags_NoScrollbar);
        {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);

            ImGui::Columns(3, nullptr, false);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
            ImGui::TextWrapped("TOTAL OBJECTS");
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImGui::Text("%d", TotalObjects);
            ImGui::PopStyleColor();

            ImGui::NextColumn();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.6f, 1.0f));
            ImGui::TextWrapped("PACKAGES");
            ImGui::PopStyleColor();

            static int PackageCount = 0;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImGui::Text("%d", PackageCount);
            ImGui::PopStyleColor();

            ImGui::NextColumn();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
            ImGui::TextWrapped("FILTERED");
            ImGui::PopStyleColor();

            static int FilteredCount = 0;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImGui::Text("%d", FilteredCount);
            ImGui::PopStyleColor();

            ImGui::Columns(1);
            ImGui::PopFont();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.12f, 0.15f, 1.0f));
        ImGui::BeginChild("##FilterPanel", ImVec2(0, 90.0f), true, ImGuiWindowFlags_NoScrollbar);
        {
            ImGui::Text("FILTERS");
            ImGui::Separator();

            ImGui::Columns(2, nullptr, false);

            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##Search", "Search objects...", SearchBuffer, IM_ARRAYSIZE(SearchBuffer)))
            {
                SearchFilter = SearchBuffer;
            }

            ImGui::NextColumn();

            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##ClassFilter", "Filter by class...", ClassFilterBuffer, IM_ARRAYSIZE(ClassFilterBuffer)))
            {
                ClassFilter = ClassFilterBuffer;
            }

            ImGui::Columns(1);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Spacing();

        ImGui::BeginChild("##MainContent", ImVec2(0, 0), false);
        {
            ImGui::BeginChild("##PackageTree", ImVec2(ImGui::GetContentRegionAvail().x * 0.35f, 0), true);
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
                ImGui::Text("PACKAGES");
                ImGui::PopStyleColor();
                ImGui::Separator();

                THashMap<FString, TVector<CObject*>> PackageToObjects;

                for (TObjectIterator<CObject> It; It; ++It)
                {
                    CObject* Object = *It;
                    if (Object == nullptr)
                    {
                        continue;
                    }

                    FString ObjectName = Object->GetName().ToString();
                    FString ClassName = Object->GetClass() ? Object->GetClass()->GetName().ToString() : "None";

                    bool bPassesFilter = true;

                    if (!SearchFilter.empty() && ObjectName.find(SearchFilter) == FString::npos)
                    {
                        bPassesFilter = false;
                    }

                    if (ClassFilter.empty() && bPassesFilter && ClassName.find(ClassFilter) == FString::npos)
                    {
                        bPassesFilter = false;
                    }

                    if (bPassesFilter)
                    {
                        FString PackageName = Object->GetPackage() ? Object->GetPackage()->GetName().ToString() : "None";
                        PackageToObjects[PackageName].push_back(Object);
                    }
                }

                for (auto& Pair : PackageToObjects)
                {
                    const FString& PackageName = Pair.first;
                    TVector<CObject*>& Objects = Pair.second;

                    if (bSortByName)
                    {
                        eastl::sort(Objects.begin(), Objects.end(), [](CObject* A, CObject* B)
                        {
                            return A->GetName().ToString() < B->GetName().ToString();
                        });
                    }

                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.25f, 0.30f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.35f, 0.45f, 1.0f));

                    FString NodeLabel = PackageName + " (" + eastl::to_string(Objects.size()) + ")";
                    bool bIsSelected = (SelectedPackage == PackageName);

                    if (ImGui::Selectable(NodeLabel.c_str(), bIsSelected, ImGuiSelectableFlags_AllowDoubleClick))
                    {
                        SelectedPackage = PackageName;
                    }

                    ImGui::PopStyleColor(2);
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("##ObjectDetails", ImVec2(0, 0), true);
            {
                if (!SelectedPackage.empty())
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
                    ImGui::Text("OBJECTS IN: %s", SelectedPackage.c_str());
                    ImGui::PopStyleColor();
                    ImGui::Separator();

                    THashMap<FString, TVector<CObject*>> PackageToObjects;

                    for (TObjectIterator<CObject> It; It; ++It)
                    {
                        CObject* Object = *It;
                        if (Object == nullptr)
                        {
                            continue;
                        }

                        FString ObjectName = Object->GetName().ToString();
                        FString ClassName = Object->GetClass() ? Object->GetClass()->GetName().ToString() : "None";

                        bool bPassesFilter = true;

                        if (!SearchFilter.empty() && ObjectName.find(SearchFilter) == FString::npos)
                        {
                            bPassesFilter = false;
                        }

                        if (ClassFilter.empty() && bPassesFilter && ClassName.find(ClassFilter) == FString::npos)
                        {
                            bPassesFilter = false;
                        }

                        if (bPassesFilter)
                        {
                            FString PackageName = Object->GetPackage() ? Object->GetPackage()->GetName().ToString() : "None";
                            if (PackageName == SelectedPackage)
                            {
                                PackageToObjects[PackageName].push_back(Object);
                            }
                        }
                    }

                    if (PackageToObjects.find(SelectedPackage) != PackageToObjects.end())
                    {
                        TVector<CObject*>& Objects = PackageToObjects[SelectedPackage];

                        if (ImGui::BeginTable("##ObjectTable", 4,
                            ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Borders |
                            ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_Sortable |
                            ImGuiTableFlags_SizingStretchProp))
                        {
                            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch, 0.20f);
                            ImGui::TableSetupColumn("Class", ImGuiTableColumnFlags_WidthStretch, 0.20f);
                            ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthStretch, 0.20f);
                            ImGui::TableSetupColumn("GUID", ImGuiTableColumnFlags_WidthStretch, 0.25f);
                            ImGui::TableSetupScrollFreeze(0, 1);
                            ImGui::TableHeadersRow();

                            ImGuiListClipper Clipper;
                            Clipper.Begin((int)Objects.size());

                            while (Clipper.Step())
                            {
                                for (int i = Clipper.DisplayStart; i < Clipper.DisplayEnd; i++)
                                {
                                    CObject* Object = Objects[i];
                                    ImGui::TableNextRow();

                                    bool bIsRowSelected = (SelectedObjectIndex == i);

                                    ImGui::TableSetColumnIndex(0);
                                    ImGuiSelectableFlags Flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick;

                                    if (ImGui::Selectable(Object->GetName().ToString().c_str(), bIsRowSelected, Flags))
                                    {
                                        SelectedObjectIndex = i;

                                        if (ImGui::IsMouseDoubleClicked(0))
                                        {
                                            ImGui::SetClipboardText(Object->GetName().ToString().c_str());
                                        }
                                    }

                                    if (ImGui::BeginPopupContextItem())
                                    {
                                        if (ImGui::MenuItem("Copy Name"))
                                        {
                                            ImGui::SetClipboardText(Object->GetName().ToString().c_str());
                                        }
                                        if (ImGui::MenuItem("Copy GUID"))
                                        {
                                            ImGui::SetClipboardText(Object->GetGUID().ToString().c_str());
                                        }
                                        ImGui::EndPopup();
                                    }

                                    ImGui::TableSetColumnIndex(1);
                                    if (Object->GetClass())
                                    {
                                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
                                        ImGui::TextUnformatted(Object->GetClass()->GetName().ToString().c_str());
                                        ImGui::PopStyleColor();
                                    }
                                    else
                                    {
                                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                                        ImGui::TextUnformatted("None");
                                        ImGui::PopStyleColor();
                                    }

                                    ImGui::TableSetColumnIndex(2);
                                    FFixedString FlagsStr = ObjectFlagsToString(Object->GetFlags());
                                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.4f, 1.0f));
                                    ImGui::TextUnformatted(FlagsStr.c_str());
                                    ImGui::PopStyleColor();

                                    ImGui::TableSetColumnIndex(3);
                                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                                    ImGui::TextUnformatted(Object->GetGUID().ToString().c_str());
                                    ImGui::PopStyleColor();
                                }
                            }

                            ImGui::EndTable();
                        }
                    }
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                    ImVec2 TextSize = ImGui::CalcTextSize("Select a package to view objects");
                    ImVec2 WindowSize = ImGui::GetWindowSize();
                    ImGui::SetCursorPos(ImVec2((WindowSize.x - TextSize.x) * 0.5f, (WindowSize.y - TextSize.y) * 0.5f));
                    ImGui::Text("Select a package to view objects");
                    ImGui::PopStyleColor();
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);
    }
}
