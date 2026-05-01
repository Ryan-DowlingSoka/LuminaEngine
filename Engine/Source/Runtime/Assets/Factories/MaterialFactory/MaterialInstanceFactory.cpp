#include "pch.h"
#include "MaterialInstanceFactory.h"

#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetTypes/Material/Material.h"
#include "Core/Object/Cast.h"
#include "Core/Object/Package/Package.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include "imgui.h"


namespace Lumina
{
    bool CMaterialInstanceFactory::HasCreationDialogue() const
    {
        return true;
    }

    bool CMaterialInstanceFactory::DrawCreationDialogue(FStringView Path, bool& bShouldClose)
    {
        static const FName MaterialClassName = FName("CMaterial");
        static ImGuiTextFilter SearchFilter;

        if (ImGui::IsWindowAppearing())
        {
            SelectedMaterialGUID.Invalidate();
            SearchFilter.Clear();
        }

        ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), LE_ICON_FORMAT_LIST_BULLETED_TYPE " Select Parent Material");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::SetNextItemWidth(-1);
        SearchFilter.Draw("##MaterialSearch");

        if (!SearchFilter.IsActive())
        {
            const ImGuiStyle& Style = ImGui::GetStyle();
            ImDrawList* DrawList = ImGui::GetWindowDrawList();
            ImVec2 TextPos = ImGui::GetItemRectMin();
            TextPos.x += Style.FramePadding.x + 2.0f;
            TextPos.y += Style.FramePadding.y;
            DrawList->AddText(TextPos, IM_COL32(110, 110, 110, 255), LE_ICON_FILE_SEARCH " Search materials...");
        }
        ImGui::PopStyleVar();

        ImGui::Spacing();

        TVector<FAssetData*> MaterialAssets = FAssetRegistry::Get().FindByPredicate([](const FAssetData& Data)
        {
            return Data.AssetClass == MaterialClassName;
        });

        eastl::sort(MaterialAssets.begin(), MaterialAssets.end(), [](const FAssetData* A, const FAssetData* B)
        {
            return A->AssetName.ToString() < B->AssetName.ToString();
        });

        bool bConfirm = false;

        const float FooterHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6, 6));
        ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.14f, 0.14f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.16f, 0.16f, 0.17f, 1.0f));

        if (ImGui::BeginTable("##MaterialList", 2,
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingFixedFit,
            ImVec2(0, -FooterHeight)))
        {
            ImGui::TableSetupColumn("##Thumb", ImGuiTableColumnFlags_WidthFixed, 48.0f);
            ImGui::TableSetupColumn("##Name", ImGuiTableColumnFlags_WidthStretch);

            if (MaterialAssets.empty())
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("No materials found in the project.");
            }

            for (const FAssetData* Asset : MaterialAssets)
            {
                if (!SearchFilter.PassFilter(Asset->AssetName.c_str()))
                {
                    continue;
                }

                ImGui::PushID(Asset);
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 48.0f);
                ImGui::TableSetColumnIndex(0);

                const bool bIsSelected = (SelectedMaterialGUID == Asset->AssetGUID);
                if (ImGui::Selectable("##Sel", bIsSelected,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick,
                    ImVec2(0, 48.0f)))
                {
                    SelectedMaterialGUID = Asset->AssetGUID;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        bConfirm = true;
                    }
                }

                ImGuiX::TextTooltip("{}", Asset->Path);

                ImGui::SameLine();

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.75f, 1.0f, 1.0f));
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(LE_ICON_PALETTE);
                ImGui::PopStyleColor();

                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(Asset->AssetName.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled(" - %s", Asset->Path.c_str());

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();

        ImGui::Separator();

        const float ButtonWidth = 110.0f;
        const float Spacing = ImGui::GetStyle().ItemSpacing.x;
        const float AvailWidth = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(AvailWidth - ButtonWidth * 2 - Spacing);

        ImGui::BeginDisabled(!SelectedMaterialGUID.IsValid());
        if (ImGui::Button(LE_ICON_CHECK " Create", ImVec2(ButtonWidth, 0)))
        {
            bConfirm = true;
        }
        ImGui::EndDisabled();

        ImGui::SameLine();

        if (ImGui::Button(LE_ICON_CLOSE " Cancel", ImVec2(ButtonWidth, 0)))
        {
            SelectedMaterialGUID.Invalidate();
            bShouldClose = true;
        }

        return bConfirm;
    }

    CObject* CMaterialInstanceFactory::CreateNew(const FName& Name, CPackage* Package)
    {
        CMaterialInstance* NewInstance = NewObject<CMaterialInstance>(Package, Name);

        if (SelectedMaterialGUID.IsValid())
        {
            NewInstance->Material = Cast<CMaterial>(LoadObject<CObject>(SelectedMaterialGUID));
            // Run the same parent-registration / parameter-resolve / material-manager
            // insertion that PostLoad does on a reload. Without this, a freshly created
            // instance has no Parameters list and MaterialIndex==-1, so the editor
            // shows "no parameters" and the renderer reads the parent's GPU slot.
            NewInstance->PostLoad();
        }

        SelectedMaterialGUID.Invalidate();
        return NewInstance;
    }
}
