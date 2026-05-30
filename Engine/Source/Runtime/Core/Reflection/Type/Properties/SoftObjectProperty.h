#pragma once
#include "Core/Object/SoftObjectPtr.h"
#include "Core/Reflection/Type/LuminaTypes.h"

namespace Lumina
{
    // FObjectProperty counterpart for soft refs; Serialize routes through FSoftObjectPath::operator<<
    // so the saver records a Soft ImportTable entry.
    class FSoftObjectProperty : public FProperty
    {
    public:
        DECLARE_FPROPERTY(EPropertyTypeFlags::SoftObject)

        FSoftObjectProperty(const FFieldOwner& InOwner, const FSoftObjectPropertyParams* Params)
            : FProperty(InOwner, Params)
        {
            ObjectClass = Params->ClassFunc ? Params->ClassFunc() : nullptr;
            // Storage is FSoftObjectPath (path + GUID). TSoftObjectPtr<T> is
            // layout-identical (single FSoftObjectPath member, no vtable).
            SetElementSize(sizeof(FSoftObjectPath));
        }

        void Serialize(FArchive& Ar, void* Value) override;
        void SerializeItem(IStructuredArchive::FSlot Slot, void* Value, void const* Defaults) override;

        RUNTIME_API CClass* GetPropertyClass() const { return ObjectClass; }

    private:

        CClass* ObjectClass = nullptr;
    };
}
