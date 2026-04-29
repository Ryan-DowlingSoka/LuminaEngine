#include "pch.h"
#include "PropertyCustomization.h"

#include "imgui.h"
#include "Core/Assertions/Assert.h"
#include "Core/Reflection/Type/LuminaTypes.h"

namespace Lumina
{
    FPropertyHandle::FPropertyHandle(void* InContainerPtr, FProperty* InProperty, int64 InIndex)
        : ContainerPtr(InContainerPtr)
        , Property(InProperty)
        , Index(InIndex)
    {
    }

    FPropertyHandle::FPropertyHandle(void* InContainerPtr, void* InDefaultContainerPtr, FProperty* InProperty, int64 InIndex)
        : ContainerPtr(InContainerPtr)
        , DefaultContainerPtr(InDefaultContainerPtr)
        , Property(InProperty)
        , Index(InIndex)
    {
    }

    bool FPropertyHandle::DiffersFromDefault() const
    {
        if (ContainerPtr == nullptr || DefaultContainerPtr == nullptr || Property == nullptr)
        {
            return false;
        }
        return !Property->Identical_InContainer(ContainerPtr, DefaultContainerPtr, Index);
    }

    void FPropertyHandle::ResetToDefault()
    {
        if (ContainerPtr == nullptr || DefaultContainerPtr == nullptr || Property == nullptr)
        {
            return;
        }
        Property->CopyCompleteValue_InContainer(ContainerPtr, DefaultContainerPtr, Index);
    }

    EPropertyChangeOp IPropertyTypeCustomization::UpdateAndDraw(const TSharedPtr<FPropertyHandle>& Property, bool bReadOnly)
    {
        ImGui::PushID(Property.get());
        HandleExternalUpdate(Property);
        ImGui::BeginDisabled(bReadOnly);
        EPropertyChangeOp Result = DrawProperty(Property);
        ImGui::EndDisabled();
        ImGui::PopID();

        return Result;
    }

    void FPropertyCustomizationRegistry::RegisterPropertyCustomization(const FName& Name, PropertyCustomizationRegisterFn Callback)
    {
        ASSERT(RegisteredProperties.find(Name) == RegisteredProperties.end());
        RegisteredProperties.emplace(Name, Callback);
    }

    void FPropertyCustomizationRegistry::UnregisterPropertyCustomization(const FName& Name)
    {
        ASSERT(RegisteredProperties.find(Name) != RegisteredProperties.end());
        RegisteredProperties.erase(Name);
    }

    bool FPropertyCustomizationRegistry::IsTypeRegistered(const FName& Name)
    {
        return RegisteredProperties.find(Name) != RegisteredProperties.end();
    }

    TSharedPtr<IPropertyTypeCustomization> FPropertyCustomizationRegistry::GetPropertyCustomizationForType(const FName& Type)
    {
        if (RegisteredProperties.find(Type) != RegisteredProperties.end())
        {
            return RegisteredProperties[Type]();
        }

        return nullptr;
    }
    
}
