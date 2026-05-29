#include "CoreTypeCustomization.h"
#include "imgui.h"
#include "Core/Object/ObjectCore.h"
#include "Core/Reflection/Type/LuminaTypes.h"
#include <limits>
#include "Core/Math/Math.h"

#include "Core/Math/Transform.h"
#include "Core/Reflection/Type/Properties/StructProperty.h"
#include "Tools/UI/ImGui/ImGuiDesignIcons.h"
#include "Tools/UI/ImGui/ImGuiX.h"

namespace Lumina
{

    EPropertyChangeOp FVec2PropertyCustomization::DrawProperty(const TSharedPtr<FPropertyHandle>& Property)
    {
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        FStructProperty* Prop = static_cast<FStructProperty*>(Property->Property);

        TOptional<float> MinOpt;
        TOptional<float> MaxOpt;

        if (Prop->HasMetadata("ClampMin"))
        {
            MinOpt = std::stof(Prop->GetMetadata("ClampMin").c_str());
        }
        if (Prop->HasMetadata("ClampMax"))
        {
            MaxOpt = std::stof(Prop->GetMetadata("ClampMax").c_str());
        }

        float Min = MinOpt ? MinOpt.value() : 0.0f;
        float Max = MaxOpt ? MaxOpt.value() : 0.0f;

        float Speed = Prop->HasMetadata("Delta") ? std::stof(Prop->GetMetadata("Delta").c_str()) : 0.01f;
        if (Prop->HasMetadata("NoDrag"))
        {
            ImGui::InputScalarN("##", ImGuiDataType_Float, Math::ValuePtr(DisplayValue), 2, &Speed, nullptr, nullptr);
        }
        else
        {
            ImGui::DragFloat2("##", Math::ValuePtr(DisplayValue), Speed, Min, Max);
        }

        ImGui::PopItemWidth();

        EPropertyChangeOp Result = EPropertyChangeOp::None;
        if (ImGui::IsItemEdited())
        {
            Result = EPropertyChangeOp::Updated;
        }
        if (ImGui::IsItemActivated())
        {
            Result = EPropertyChangeOp::Started;
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            Result = EPropertyChangeOp::Finished;
        }
        
        return Result;
    }

    void FVec2PropertyCustomization::UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property)
    {
        CachedValue = DisplayValue;
        Property->SetValue(CachedValue);
    }

    void FVec2PropertyCustomization::HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property)
    {
        FVector2 ActualValue;
        Property->GetValue(&ActualValue);
        
        if (CachedValue != ActualValue)
        {
            CachedValue = DisplayValue = ActualValue;
        }
    }

    EPropertyChangeOp FVec3PropertyCustomization::DrawProperty(const TSharedPtr<FPropertyHandle>& Property)
    {
        FStructProperty* Prop = static_cast<FStructProperty*>(Property->Property);

        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

        if (Prop->Metadata.HasMetadata("Color"))
        {
            ImGui::ColorEdit3("##", Math::ValuePtr(DisplayValue));
        }
        else
        {
            TOptional<float> MinOpt;
            TOptional<float> MaxOpt;

            if (Prop->HasMetadata("ClampMin"))
            {
                MinOpt = std::stof(Prop->GetMetadata("ClampMin").c_str());
            }
            if (Prop->HasMetadata("ClampMax"))
            {
                MaxOpt = std::stof(Prop->GetMetadata("ClampMax").c_str());
            }

            float Min = MinOpt ? MinOpt.value() : 0.0f;
            float Max = MaxOpt ? MaxOpt.value() : 0.0f;

            float Speed = Prop->HasMetadata("Delta") ? std::stof(Prop->GetMetadata("Delta").c_str()) : 0.01f;
            if (Prop->HasMetadata("NoDrag"))
            {
                ImGui::InputScalarN("##", ImGuiDataType_Float, Math::ValuePtr(DisplayValue), 3, &Speed, nullptr, nullptr);
            }
            else
            {
                ImGui::DragFloat3("##", Math::ValuePtr(DisplayValue), Speed, Min, Max);
            }
        }
        
        ImGui::PopItemWidth();

        EPropertyChangeOp Result = EPropertyChangeOp::None;
        if (ImGui::IsItemEdited())
        {
            Result = EPropertyChangeOp::Updated;
        }
        if (ImGui::IsItemActivated())
        {
            Result = EPropertyChangeOp::Started;
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            Result = EPropertyChangeOp::Finished;
        }
        
        return Result;
    }
    
    void FVec3PropertyCustomization::UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property)
    {
        CachedValue = DisplayValue;
        Property->SetValue(CachedValue);
    }

    void FVec3PropertyCustomization::HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property)
    {
        FVector3 ActualValue;
        Property->GetValue(&ActualValue);
        
        if (CachedValue != ActualValue)
        {
            CachedValue = DisplayValue = ActualValue;
        }
    }

    EPropertyChangeOp FVec4PropertyCustomization::DrawProperty(const TSharedPtr<FPropertyHandle>& Property)
    {
        FStructProperty* Prop = static_cast<FStructProperty*>(Property->Property);

        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

        if (Prop->Metadata.HasMetadata("Color"))
        {
            ImGui::ColorEdit4("##", Math::ValuePtr(DisplayValue));
        }
        else
        {
            TOptional<float> MinOpt;
            TOptional<float> MaxOpt;

            if (Prop->HasMetadata("ClampMin"))
            {
                MinOpt = std::stof(Prop->GetMetadata("ClampMin").c_str());
            }
            if (Prop->HasMetadata("ClampMax"))
            {
                MaxOpt = std::stof(Prop->GetMetadata("ClampMax").c_str());
            }

            float Min = MinOpt ? MinOpt.value() : 0.0f;
            float Max = MaxOpt ? MaxOpt.value() : 0.0f;

            float Speed = Prop->HasMetadata("Delta") ? std::stof(Prop->GetMetadata("Delta").c_str()) : 0.01f;
            if (Prop->HasMetadata("NoDrag"))
            {
                ImGui::InputScalarN("##", ImGuiDataType_Float, Math::ValuePtr(DisplayValue), 4, &Speed, nullptr, nullptr);
            }
            else
            {
                ImGui::DragFloat4("##", Math::ValuePtr(DisplayValue), Speed, Min, Max);
            }
        }

        ImGui::PopItemWidth();
        
        EPropertyChangeOp Result = EPropertyChangeOp::None;
        if (ImGui::IsItemEdited())
        {
            Result = EPropertyChangeOp::Updated;
        }
        if (ImGui::IsItemActivated())
        {
            Result = EPropertyChangeOp::Started;
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            Result = EPropertyChangeOp::Finished;
        }
        
        return Result;
    }

    void FVec4PropertyCustomization::UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property)
    {
        CachedValue = DisplayValue;
        Property->SetValue(CachedValue);
    }

    void FVec4PropertyCustomization::HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property)
    {
        FVector4 ActualValue;
        Property->GetValue(&ActualValue);
        
        if (CachedValue != ActualValue)
        {
            CachedValue = DisplayValue = ActualValue;
        }
    }

    EPropertyChangeOp FQuatPropertyCustomization::DrawProperty(const TSharedPtr<FPropertyHandle>& Property)
    {
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

        ImGui::DragFloat4("##", Math::ValuePtr(DisplayValue), 0.01f);

        ImGui::PopItemWidth();
        
        EPropertyChangeOp Result = EPropertyChangeOp::None;
        if (ImGui::IsItemEdited())
        {
            Result = EPropertyChangeOp::Updated;
        }
        if (ImGui::IsItemActivated())
        {
            Result = EPropertyChangeOp::Started;
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            Result = EPropertyChangeOp::Finished;
        }
        
        return Result;
    }

    void FQuatPropertyCustomization::UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property)
    {
        CachedValue = DisplayValue;
        Property->SetValue(CachedValue);
    }

    void FQuatPropertyCustomization::HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property)
    {
        FQuat ActualValue;
        Property->GetValue(&ActualValue);
        
        if (CachedValue != ActualValue)
        {
            CachedValue = DisplayValue = ActualValue;
        }
    }
    
    EPropertyChangeOp FTransformPropertyCustomization::DrawProperty(const TSharedPtr<FPropertyHandle>& Property)
    {
        EPropertyChangeOp Result = EPropertyChangeOp::None;
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
        ImGui::TextUnformatted(LE_ICON_AXIS_ARROW);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGui::SetTooltip("Translation (Location)");
        }

        if (ImGui::DragFloat3("T", Math::ValuePtr(DisplayValue.Location), 0.01f))
        {
            Result = EPropertyChangeOp::Updated;
        }
        if (ImGui::IsItemActivated())
        {
            Result = EPropertyChangeOp::Started;
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            Result = EPropertyChangeOp::Finished;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.7f, 1.0f));
        ImGui::TextUnformatted(LE_ICON_ROTATE_360);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGui::SetTooltip("Rotation (Euler Angles)");
        }
        ImGui::SameLine();
        
        FVector3 EulerRotation = Math::Degrees(Math::EulerAngles(DisplayValue.Rotation));
        if (ImGui::DragFloat3("R", Math::ValuePtr(EulerRotation), 0.01f))
        {
            DisplayValue.SetRotationFromEuler(EulerRotation);
            Result = EPropertyChangeOp::Updated;
        }
        if (ImGui::IsItemActivated())
        {
            Result = EPropertyChangeOp::Started;
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            Result = EPropertyChangeOp::Finished;
        }
    
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.4f, 1.0f));
        ImGui::TextUnformatted(LE_ICON_ARROW_TOP_RIGHT_BOTTOM_LEFT);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGui::SetTooltip("Scale");
        }
        ImGui::SameLine();
        
        if (ImGui::DragFloat3("S", Math::ValuePtr(DisplayValue.Scale), 0.01f))
        {
            Result = EPropertyChangeOp::Updated;
        }
        if (ImGui::IsItemActivated())
        {
            Result = EPropertyChangeOp::Started;
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            Result = EPropertyChangeOp::Finished;
        }
    
        ImGui::PopItemWidth();
        return Result;
    }

    void FTransformPropertyCustomization::UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property)
    {
        CachedValue = DisplayValue;
        Property->SetValue(CachedValue);
    }

    void FTransformPropertyCustomization::HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property)
    {
        FTransform ActualValue;
        Property->GetValue(&ActualValue);
        
        if (CachedValue != ActualValue)
        {
            CachedValue = DisplayValue = ActualValue;
        }
    }
}
