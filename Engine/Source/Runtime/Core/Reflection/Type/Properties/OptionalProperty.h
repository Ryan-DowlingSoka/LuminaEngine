#pragma once

#include "Core/Reflection/Type/LuminaTypes.h"
#include "Memory/SmartPtr.h"

namespace Lumina
{
    /**
     * Reflection wrapper for TOptional<T> (eastl::optional<T>).
     *
     * Layout-wise, optionals carry the same wire format as a single-element
     * array: one bool ("engaged?") followed by an optional payload. The Inner
     * FProperty describes the payload type so structured archives and the
     * editor can recurse into it without knowing T at compile time.
     */
    class FOptionalProperty : public FProperty
    {
    public:
        FOptionalProperty(const FFieldOwner& InOwner, const FOptionalPropertyParams* Params)
            : FProperty(InOwner, Params)
            , HasValueFn(Params->HasValueFn)
            , GetValueFn(Params->GetValueFn)
            , SetValueFn(Params->SetValueFn)
            , ResetFn   (Params->ResetFn)
        {
        }

        DECLARE_FPROPERTY(EPropertyTypeFlags::Optional)

        // ConstructProperties feeds the inner property in via this hook; we
        // take ownership so the optional fully describes its payload.
        void AddProperty(FProperty* Property) override { Inner.reset(Property); }

        FProperty* GetInternalProperty() const { return Inner.get(); }

        bool  HasValue(const void* InContainer) const { return HasValueFn(InContainer); }
        void* GetValue(void* InContainer) const       { return GetValueFn(InContainer); }
        void  SetValue(void* InContainer, const void* InValue) const { SetValueFn(InContainer, InValue); }
        void  Reset(void* InContainer) const          { ResetFn(InContainer); }

        void Serialize(FArchive& Ar, void* Value) override;
        void SerializeItem(IStructuredArchive::FSlot Slot, void* Value, void const* Defaults) override;

    private:

        OptionalHasValuePtr     HasValueFn;
        OptionalGetValuePtr     GetValueFn;
        OptionalSetValuePtr     SetValueFn;
        OptionalResetPtr        ResetFn;

        TUniquePtr<FProperty>   Inner;
    };
}
