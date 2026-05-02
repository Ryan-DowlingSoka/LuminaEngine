#pragma once
#include "Core/Object/Class.h"
#include "Core/Reflection/Type/LuminaTypes.h"
#include "Memory/SmartPtr.h"

namespace Lumina
{
    class FEnumProperty : public FProperty
    {
    public:

        FEnumProperty(const FFieldOwner& InOwner, const FPropertyParams* Params)
            :FProperty(InOwner, Params)
        {
            auto* EnumParams = static_cast<const FEnumPropertyParams*>(Params);
            CEnum* InternalEnum = EnumParams->EnumFunc();
            ASSERT(InternalEnum);
            SetEnum(InternalEnum);
        }
        
        void AddProperty(FProperty* Property) override { InnerProperty.reset(static_cast<FNumericProperty*>(Property)); }
        RUNTIME_API FNumericProperty* GetInnerProperty() const { return InnerProperty.get(); }
        
        void SetEnum(CEnum* InEnum);

        FORCEINLINE CEnum* GetEnum() const { return Enum; }

        void Serialize(FArchive& Ar, void* Value) override;
        void SerializeItem(IStructuredArchive::FSlot Slot, void* Value, void const* Defaults) override;


    private:

        TUniquePtr<FNumericProperty> InnerProperty;
        CEnum* Enum = nullptr;
    };
}
