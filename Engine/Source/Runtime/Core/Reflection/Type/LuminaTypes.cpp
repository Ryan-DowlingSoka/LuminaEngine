#include "pch.h"
#include "LuminaTypes.h"
#include "Core/Object/Field.h"
#include "Core/Object/Class.h"

namespace Lumina
{
    void FProperty::Init()
    {
        eastl::visit([this](auto& Value)
        {
            Value->AddProperty(this);
        }, Owner);
   
    }
    
    const FName& FProperty::GetTypeName() const
    {
        return TypeName;
    }
    
    void FProperty::OnMetadataFinalized()
    {
        if (Metadata.HasMetadata("DisplayName"))
        {
            DisplayName = Metadata.GetMetadata("DisplayName").ToString();
        }
        else
        {
            DisplayName = MakeDisplayNameFromName(TypeFlags, Name);
        }
    }

    FString FProperty::MakeDisplayNameFromName(EPropertyTypeFlags TypeFlags, const FName& InName)
    {
        FFixedString Raw = InName.c_str();
        FStringView View(Raw.begin(), Raw.length());
        
        if (TypeFlags == EPropertyTypeFlags::Bool)
        {
            if (View.starts_with('b') && std::isupper(Raw[1]))
            {
                Raw.erase(0, 1);
            }
        }

        FString Display;
        for (size_t i = 0; i < Raw.size(); ++i)
        {
            if (i > 0 && std::isupper(Raw[i]) && !std::isspace(Raw[i - 1]) && !std::isupper(Raw[i - 1]))
            {
                Display += ' ';
            }
            Display += Raw[i];
        }

        if (!Display.empty())
        {
            Display[0] = eastl::CharToUpper(Display[0]);
        }

        return Display;
    }
    
    void FProperty::CallSetter(void* Container, const void* InValue) const
    {
        if (!HasSetter())
        {
            LOG_CRITICAL("Calling a setter but the property has no setter defined.");
        }
    }

    void FProperty::CallGetter(const void* Container, void* OutValue) const
    {
        if (!HasGetter())
        {
            LOG_CRITICAL("Calling a getter but the property has no getter defined.");
        }
    }

    void* FProperty::GetValuePtrInternal(void* ContainerPtr, int64 ArrayIndex) const
    {
        void* PropertyPtr = (uint8*)ContainerPtr + Offset;
        return (uint8*)PropertyPtr + ArrayIndex * ElementSize;
    }
}
