#pragma once
#include "imgui.h"
#include "Core/Math/Transform.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Core/Reflection/PropertyCustomization/PropertyCustomization.h"
#include "Core/Reflection/Type/LuminaTypes.h"
#include "Core/Math/Math.h"

namespace Lumina
{
    static bool IsFloatType(ImGuiDataType dt)
    {
        return dt == ImGuiDataType_Float || dt == ImGuiDataType_Double;
    }
    
    template<typename T, ImGuiDataType_ DT>
    class FNumericPropertyCustomization : public IPropertyTypeCustomization
    {
        using ValueType = T;
        
    public:
        
        static TSharedPtr<FNumericPropertyCustomization> MakeInstance()
        {
            return MakeShared<FNumericPropertyCustomization>();
        }
        
        EPropertyChangeOp DrawProperty(const TSharedPtr<FPropertyHandle>& Property) override
        {
            FProperty* Prop = Property->Property;
            float Speed = Prop->HasMetadata("Delta") ? std::stof(Prop->GetMetadata("Delta").c_str()) : (IsFloatType(DT) ? 0.01f : 1.0f);
            
            TOptional<ValueType> MinOpt;
            TOptional<ValueType> MaxOpt;

            if (Prop->HasMetadata("ClampMin"))
            {
                MinOpt = static_cast<ValueType>(std::stod(Prop->GetMetadata("ClampMin").c_str()));
            }
            if (Prop->HasMetadata("ClampMax"))
            {
                MaxOpt = static_cast<ValueType>(std::stod(Prop->GetMetadata("ClampMax").c_str()));
            }

            const ValueType* Min = MinOpt ? &MinOpt.value() : nullptr;
            const ValueType* Max = MaxOpt ? &MaxOpt.value() : nullptr;

            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
            if (Prop->HasMetadata("NoDrag"))
            {
                // Type-only entry; +/- buttons step by Delta, no click-drag scrubbing.
                ValueType Step = static_cast<ValueType>(Speed);
                ImGui::InputScalar("##Value", DT, &DisplayValue, &Step, nullptr, nullptr);
                if (Min && DisplayValue < *Min) DisplayValue = *Min;
                if (Max && DisplayValue > *Max) DisplayValue = *Max;
            }
            else
            {
                ImGui::DragScalar("##Value", DT, &DisplayValue, Speed, Min, Max);
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
        
        void UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property) override
        {
            CachedValue = DisplayValue;
            Property->SetValue(CachedValue);
        }

        void HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property) override
        {
            ValueType ActualValue;
            Property->GetValue(&ActualValue);
        
            if (!Math::IsNearlyEqual(CachedValue, ActualValue, LE_SMALL_NUMBER))
            {
                CachedValue = DisplayValue = ActualValue;
            }
        }

        ValueType CachedValue;
        ValueType DisplayValue;
        
    };
    
    class FBoolPropertyCustomization : public IPropertyTypeCustomization
    {
    public:

        static TSharedPtr<FBoolPropertyCustomization> MakeInstance()
        {
            return MakeShared<FBoolPropertyCustomization>();
        }
        
        EPropertyChangeOp DrawProperty(const TSharedPtr<FPropertyHandle>& Property) override
        {
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::Checkbox("##", &bValue);
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
        
        void UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property) override
        {
            Property->SetValue(bValue);
        }

        void HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property) override
        {
            Property->GetValue(&bValue);
        }

        bool bValue;
    };
    
    class FCObjectPropertyCustomization : public IPropertyTypeCustomization
    {
    public:

        static TSharedPtr<FCObjectPropertyCustomization> MakeInstance()
        {
            return MakeShared<FCObjectPropertyCustomization>();
        }
        
        EPropertyChangeOp DrawProperty(const TSharedPtr<FPropertyHandle>& Property) override;
        
        void UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property) override;

        void HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property) override;

    private:

        TWeakObjectPtr<CObject> Object;
        ImGuiTextFilter SearchFilter;

        // Object edits (clear / pick / drop) are discrete one-frame events. We emit Started
        // on the change frame and Finished the next so the change is wrapped in a proper
        // undo transaction instead of a bare Updated that never opens or commits one.
        bool bFinishPending = false;
    };

    class FEnumPropertyCustomization : public IPropertyTypeCustomization
    {
    public:
        
        static TSharedPtr<FEnumPropertyCustomization> MakeInstance()
        {
            return MakeShared<FEnumPropertyCustomization>();
        }

        EPropertyChangeOp DrawProperty(const TSharedPtr<FPropertyHandle>& Property) override;
        void UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property) override;
        void HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property) override;

    private:

        int64 CachedValue = 0;
    };

    class FNamePropertyCustomization : public IPropertyTypeCustomization
    {
    public:

        static TSharedPtr<FNamePropertyCustomization> MakeInstance()
        {
            return MakeShared<FNamePropertyCustomization>();
        }

        EPropertyChangeOp DrawProperty(const TSharedPtr<FPropertyHandle>& Property) override;
        void UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property) override;
        void HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property) override;

    private:

        FName CachedValue;
        FName DisplayValue;
        ImGuiTextFilter BoneFilter;
    };

    class FStringPropertyCustomization : public IPropertyTypeCustomization
    {
    public:
        
        static TSharedPtr<FStringPropertyCustomization> MakeInstance()
        {
            return MakeShared<FStringPropertyCustomization>();
        }

        EPropertyChangeOp DrawProperty(const TSharedPtr<FPropertyHandle>& Property) override;
        void UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property) override;
        void HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property) override;

    private:

        FString DisplayValue;
        ImGuiTextFilter SearchFilter;
    };

    class FVec2PropertyCustomization : public IPropertyTypeCustomization
    {
    public:
        
        static TSharedPtr<FVec2PropertyCustomization> MakeInstance()
        {
            return MakeShared<FVec2PropertyCustomization>();
        }

        EPropertyChangeOp DrawProperty(const TSharedPtr<FPropertyHandle>& Property) override;
        void UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property) override;
        void HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property) override;

    private:

        FVector2 CachedValue{};
        FVector2 DisplayValue{};
    };

    class FVec3PropertyCustomization : public IPropertyTypeCustomization
    {
    public:
        
        static TSharedPtr<FVec3PropertyCustomization> MakeInstance()
        {
            return MakeShared<FVec3PropertyCustomization>();
        }

        EPropertyChangeOp DrawProperty(const TSharedPtr<FPropertyHandle>& Property) override;
        void UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property) override;
        void HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property) override;

    private:
        
        FVector3 CachedValue{};
        FVector3 DisplayValue{};
    };

    class FVec4PropertyCustomization : public IPropertyTypeCustomization
    {
    public:
        
        static TSharedPtr<FVec4PropertyCustomization> MakeInstance()
        {
            return MakeShared<FVec4PropertyCustomization>();
        }

        EPropertyChangeOp DrawProperty(const TSharedPtr<FPropertyHandle>& Property) override;
        void UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property) override;
        void HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property) override;

    private:

        FVector4 CachedValue{};
        FVector4 DisplayValue{};
    };

    class FQuatPropertyCustomization : public IPropertyTypeCustomization
    {
    public:
        
        static TSharedPtr<FQuatPropertyCustomization> MakeInstance()
        {
            return MakeShared<FQuatPropertyCustomization>();
        }

        EPropertyChangeOp DrawProperty(const TSharedPtr<FPropertyHandle>& Property) override;
        void UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property) override;
        void HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property) override;

    private:

        FQuat CachedValue{};
        FQuat DisplayValue{};
    };

    class FTransformPropertyCustomization : public IPropertyTypeCustomization
    {
    public:
        
        static TSharedPtr<FTransformPropertyCustomization> MakeInstance()
        {
            return MakeShared<FTransformPropertyCustomization>();
        }

        EPropertyChangeOp DrawProperty(const TSharedPtr<FPropertyHandle>& Property) override;
        void UpdatePropertyValue(const TSharedPtr<FPropertyHandle>& Property) override;
        void HandleExternalUpdate(const TSharedPtr<FPropertyHandle>& Property) override;

        FTransform CachedValue{};
        FTransform DisplayValue{};

        // Clicking an axis tag resets that component to 0 — a discrete edit, so we open the
        // transaction the click frame (Started) and commit it the next (Finished).
        bool bFinishPending = false;
    };
    
}
