#include "CoreTypeCustomization.h"
#include "UI/Tools/AssetEditors/TextureEditor/TextureEditorTool.h"


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
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

        ImGui::InputText("##Name", const_cast<char*>(DisplayValue.c_str()), 256);

        ImGui::PopItemWidth();
        
        return ImGui::IsItemDeactivatedAfterEdit() ? EPropertyChangeOp::Updated : EPropertyChangeOp::None;
    }

    
    void FStringPropertyCustomization::UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property)
    {
        CachedValue = DisplayValue;
        Property->Property->SetValue(Property->ContainerPtr, CachedValue, Property->Index);
    }

    void FStringPropertyCustomization::HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property)
    {
        FString ActualValue;
        Property->Property->GetValue(Property->ContainerPtr, &ActualValue, Property->Index);
        
        if (CachedValue != ActualValue)
        {
            CachedValue = DisplayValue = ActualValue;
        }
    }
}
