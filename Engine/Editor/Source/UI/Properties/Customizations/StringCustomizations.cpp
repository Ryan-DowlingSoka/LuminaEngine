#include "CoreTypeCustomization.h"
#include "UI/Tools/AssetEditors/TextureEditor/TextureEditorTool.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"
#include "Tools/UI/ImGui/ImGuiX.h"
#include <Assets/AssetRegistry/AssetData.h>
#include <Assets/AssetRegistry/AssetRegistry.h>


namespace Lumina
{
    EPropertyChangeOp FNamePropertyCustomization::DrawProperty(TSharedPtr<FPropertyHandle> Property)
    {
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

        char Buffer[256];
        strncpy(Buffer, DisplayValue.c_str(), sizeof(Buffer));
        Buffer[sizeof(Buffer) - 1] = '\0';
        if (ImGui::InputText("##ParamName", Buffer, sizeof(Buffer)))
        {
            DisplayValue = FName(Buffer);
        }

        ImGui::PopItemWidth();

        return EPropertyChangeOp::None;
    }

    void FNamePropertyCustomization::UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property)
    {
        CachedValue = DisplayValue;
        Property->Property->SetValue(Property->ContainerPtr, CachedValue, Property->Index);
    }

    void FNamePropertyCustomization::HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property)
    {
        FName ActualValue;
        Property->Property->GetValue(Property->ContainerPtr, &ActualValue, Property->Index);
        
        if (CachedValue != ActualValue)
        {
            CachedValue = DisplayValue = ActualValue;
        }
    }

    EPropertyChangeOp FStringPropertyCustomization::DrawProperty(TSharedPtr<FPropertyHandle> Property)
    {
        // The "FilePath" meta turns a plain string field into an asset-path
        // picker: a "..." button opens a searchable list of registered assets
        // and drops the selected virtual path into the string.
        const bool bFilePath = Property->Property->HasMetadata("FilePath");
        const float ButtonWidth = bFilePath ? ImGui::GetFrameHeight() : 0.0f;

        EPropertyChangeOp Result = EPropertyChangeOp::None;

        char Buffer[256];
        strncpy(Buffer, DisplayValue.c_str(), sizeof(Buffer));
        Buffer[sizeof(Buffer) - 1] = '\0';

        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - ButtonWidth);
        if (ImGui::InputText("##ParamName", Buffer, sizeof(Buffer)))
        {
            DisplayValue = Buffer;
        }
        ImGui::PopItemWidth();

        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            Result = EPropertyChangeOp::Updated;
        }

        if (bFilePath)
        {
            ImGui::SameLine(0, 0);
            if (ImGui::Button(LE_ICON_DOTS_HORIZONTAL "##FilePathPick", ImVec2(ButtonWidth, 0)))
            {
                ImGui::OpenPopup("##FilePathPicker");
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGuiX::TextTooltip_Internal("Pick asset path");
            }

            if (ImGui::BeginPopup("##FilePathPicker"))
            {
                SearchFilter.Draw("##Search", 250.0f);
                if (ImGui::BeginChild("##PathList", ImVec2(300, 300)))
                {
                    TVector<FAssetData*> Assets = FAssetRegistry::Get().FindByPredicate([](const FAssetData&) { return true; });
                    for (const FAssetData* Asset : Assets)
                    {
                        if (!SearchFilter.PassFilter(Asset->Path.c_str()))
                        {
                            continue;
                        }

                        if (ImGui::Selectable(Asset->Path.c_str()))
                        {
                            DisplayValue = Asset->Path.c_str();
                            Result = EPropertyChangeOp::Updated;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::EndPopup();
            }
        }

        return Result;
    }

    
    void FStringPropertyCustomization::UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property)
    {
        Property->Property->SetValue(Property->ContainerPtr, DisplayValue, Property->Index);
    }

    void FStringPropertyCustomization::HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property)
    {
        FString ActualValue;
        Property->Property->GetValue(Property->ContainerPtr, &ActualValue, Property->Index);
        
        DisplayValue = ActualValue;
    }
}
