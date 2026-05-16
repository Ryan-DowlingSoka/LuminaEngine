#pragma once
#include "imgui.h"
#include "Core/Math/Transform.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Core/Reflection/PropertyCustomization/PropertyCustomization.h"
#include "Core/Reflection/Type/LuminaTypes.h"
#include "glm/glm.hpp"

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
        
        EPropertyChangeOp DrawProperty(TSharedPtr<FPropertyHandle> Property) override
        {
            FProperty* Prop = Property->Property;
            float Speed = Prop->HasMetadata("Delta") ? std::stof(Prop->GetMetadata("Delta").c_str()) : (IsFloatType(DT) ? 0.01f : 1.0f);

            // ImGui::DragScalar reads sizeof(T) bytes from Min/Max as the
            // property's actual type. Passing &float when T is int32 makes
            // ImGui reinterpret the float bit pattern as an int, which is
            // why drags on int properties used to snap to ~1e9 (the
            // bit-pattern of float(3.0) is 0x40400000 = 1077936128).
            std::optional<ValueType> MinOpt;
            std::optional<ValueType> MaxOpt;

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
            ImGui::DragScalar("##Value", DT, &DisplayValue, Speed, Min, Max);
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
        
        void UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property) override
        {
            CachedValue = DisplayValue;
            Property->Property->SetValue(Property->ContainerPtr, CachedValue, Property->Index);
        }

        void HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property) override
        {
            ValueType ActualValue;
            Property->Property->GetValue(Property->ContainerPtr, &ActualValue, Property->Index);
        
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
        
        EPropertyChangeOp DrawProperty(TSharedPtr<FPropertyHandle> Property) override
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
        
        void UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property) override
        {
            Property->Property->SetValue(Property->ContainerPtr, bValue, Property->Index);
        }

        void HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property) override
        {
            Property->Property->GetValue(Property->ContainerPtr, &bValue, Property->Index);
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
        
        EPropertyChangeOp DrawProperty(TSharedPtr<FPropertyHandle> Property) override;
        
        void UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property) override;

        void HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property) override;

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

        EPropertyChangeOp DrawProperty(TSharedPtr<FPropertyHandle> Property) override;
        void UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property) override;
        void HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property) override;

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

        EPropertyChangeOp DrawProperty(TSharedPtr<FPropertyHandle> Property) override;
        void UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property) override;
        void HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property) override;

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

        EPropertyChangeOp DrawProperty(TSharedPtr<FPropertyHandle> Property) override;
        void UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property) override;
        void HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property) override;

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

        EPropertyChangeOp DrawProperty(TSharedPtr<FPropertyHandle> Property) override;
        void UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property) override;
        void HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property) override;

    private:

        glm::vec2 CachedValue{};
        glm::vec2 DisplayValue{};
    };

    class FVec3PropertyCustomization : public IPropertyTypeCustomization
    {
    public:
        
        static TSharedPtr<FVec3PropertyCustomization> MakeInstance()
        {
            return MakeShared<FVec3PropertyCustomization>();
        }

        EPropertyChangeOp DrawProperty(TSharedPtr<FPropertyHandle> Property) override;
        void UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property) override;
        void HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property) override;

    private:
        
        glm::vec3 CachedValue{};
        glm::vec3 DisplayValue{};
    };

    class FVec4PropertyCustomization : public IPropertyTypeCustomization
    {
    public:
        
        static TSharedPtr<FVec4PropertyCustomization> MakeInstance()
        {
            return MakeShared<FVec4PropertyCustomization>();
        }

        EPropertyChangeOp DrawProperty(TSharedPtr<FPropertyHandle> Property) override;
        void UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property) override;
        void HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property) override;

    private:

        glm::vec4 CachedValue{};
        glm::vec4 DisplayValue{};
    };

    class FQuatPropertyCustomization : public IPropertyTypeCustomization
    {
    public:
        
        static TSharedPtr<FQuatPropertyCustomization> MakeInstance()
        {
            return MakeShared<FQuatPropertyCustomization>();
        }

        EPropertyChangeOp DrawProperty(TSharedPtr<FPropertyHandle> Property) override;
        void UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property) override;
        void HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property) override;

    private:

        glm::quat CachedValue{};
        glm::quat DisplayValue{};
    };

    class FTransformPropertyCustomization : public IPropertyTypeCustomization
    {
    public:
        
        static TSharedPtr<FTransformPropertyCustomization> MakeInstance()
        {
            return MakeShared<FTransformPropertyCustomization>();
        }

        EPropertyChangeOp DrawProperty(TSharedPtr<FPropertyHandle> Property) override;
        void UpdatePropertyValue(TSharedPtr<FPropertyHandle> Property) override;
        void HandleExternalUpdate(TSharedPtr<FPropertyHandle> Property) override;

        FTransform CachedValue{};
        FTransform DisplayValue{};
    };
    
}
