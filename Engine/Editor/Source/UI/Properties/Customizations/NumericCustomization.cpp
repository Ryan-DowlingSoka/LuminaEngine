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
    namespace
    {
        // Per-axis tag colors (X/Y/Z), matching the gizmo.
        constexpr ImVec4 GAxisColors[3] =
        {
            ImVec4(0.72f, 0.27f, 0.30f, 1.0f),
            ImVec4(0.36f, 0.58f, 0.30f, 1.0f),
            ImVec4(0.24f, 0.44f, 0.78f, 1.0f),
        };
        constexpr const char* GAxisLabels[3] = { "X", "Y", "Z" };

        // Tracks a just-drawn item and folds its interaction into the running op
        // (Finished > Started > Updated, matching the rest of the customizations).
        void AccumulateOp(EPropertyChangeOp& Op)
        {
            if (ImGui::IsItemEdited())                  Op = EPropertyChangeOp::Updated;
            if (ImGui::IsItemActivated())               Op = EPropertyChangeOp::Started;
            if (ImGui::IsItemDeactivatedAfterEdit())    Op = EPropertyChangeOp::Finished;
        }

        // One labeled row: leading category icon + three color-tagged XYZ drag fields.
        // Clicking an axis tag zeroes that component and sets bResetClicked.
        EPropertyChangeOp DrawAxisRow(const char* ID, const char* Icon, const ImVec4& IconColor, const char* Tooltip, float* Values, float Speed, bool& bResetClicked)
        {
            EPropertyChangeOp Op = EPropertyChangeOp::None;
            const ImGuiStyle& Style = ImGui::GetStyle();
            const float LineHeight = ImGui::GetFrameHeight();
            const float IconColumnW = LineHeight + Style.ItemInnerSpacing.x * 2.0f;

            ImGui::PushID(ID);

            ImGui::AlignTextToFramePadding();
            ImGuiX::TextColoredUnformatted(IconColor, Icon);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip("%s", Tooltip);
            }
            ImGui::SameLine(IconColumnW);

            const float TagW = LineHeight;
            const float Avail = ImGui::GetContentRegionAvail().x;
            const float FieldW = Math::Max((Avail - 3.0f * (TagW + Style.ItemInnerSpacing.x)) / 3.0f, 1.0f);

            for (int32 Axis = 0; Axis < 3; ++Axis)
            {
                ImGui::PushID(Axis);
                if (Axis > 0)
                {
                    ImGui::SameLine(0.0f, Style.ItemInnerSpacing.x);
                }

                ImGui::PushStyleColor(ImGuiCol_Button, GAxisColors[Axis]);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GAxisColors[Axis]);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, GAxisColors[Axis]);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
                if (ImGui::Button(GAxisLabels[Axis], ImVec2(TagW, LineHeight)))
                {
                    Values[Axis] = 0.0f;
                    bResetClicked = true;
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                {
                    ImGui::SetTooltip("Reset to 0");
                }

                ImGui::SameLine(0.0f, 0.0f);
                ImGui::SetNextItemWidth(FieldW);
                if (ImGui::DragScalar("##V", ImGuiDataType_Float, &Values[Axis], Speed, nullptr, nullptr, "%.3f"))
                {
                    Op = EPropertyChangeOp::Updated;
                }
                AccumulateOp(Op);
                ImGui::PopID();
            }

            ImGui::PopID();
            return Op;
        }
    }

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
        EPropertyChangeOp DragOp = EPropertyChangeOp::None;
        auto Merge = [&DragOp](EPropertyChangeOp Op)
        {
            if (Op != EPropertyChangeOp::None)
            {
                DragOp = Op;
            }
        };

        bool bReset = false;
        Merge(DrawAxisRow("T", LE_ICON_AXIS_ARROW, ImVec4(0.40f, 0.70f, 1.0f, 1.0f), "Translation (Location)", Math::ValuePtr(DisplayValue.Location), 0.01f, bReset));

        FVector3 EulerRotation = Math::Degrees(Math::EulerAngles(DisplayValue.Rotation));
        bool bRotationReset = false;
        const EPropertyChangeOp RotationOp = DrawAxisRow("R", LE_ICON_ROTATE_360, ImVec4(0.40f, 1.0f, 0.70f, 1.0f), "Rotation (Euler Angles)", Math::ValuePtr(EulerRotation), 0.1f, bRotationReset);
        if (RotationOp == EPropertyChangeOp::Updated || bRotationReset)
        {
            DisplayValue.SetRotationFromEuler(EulerRotation);
        }
        Merge(RotationOp);
        bReset |= bRotationReset;

        Merge(DrawAxisRow("S", LE_ICON_ARROW_TOP_RIGHT_BOTTOM_LEFT, ImVec4(1.0f, 0.70f, 0.40f, 1.0f), "Scale", Math::ValuePtr(DisplayValue.Scale), 0.01f, bReset));

        // A reset writes the value this frame: open the undo transaction now (Started),
        // commit it next frame (Finished), like the discrete object/array edits.
        if (bReset)
        {
            if (bFinishPending)
            {
                return EPropertyChangeOp::Updated;
            }
            bFinishPending = true;
            return EPropertyChangeOp::Started;
        }
        if (bFinishPending)
        {
            bFinishPending = false;
            return EPropertyChangeOp::Finished;
        }

        return DragOp;
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
