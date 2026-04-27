#include "pch.h"
#include "OptionalProperty.h"

namespace Lumina
{
    void FOptionalProperty::Serialize(FArchive& Ar, void* Value)
    {
        // Wire format: [bool engaged][payload?] -- payload only if engaged.
        if (Ar.IsWriting())
        {
            bool bEngaged = HasValue(Value);
            Ar << bEngaged;

            if (bEngaged)
            {
                Inner->Serialize(Ar, GetValue(Value));
            }
        }
        else
        {
            bool bEngaged = false;
            Ar << bEngaged;

            if (bEngaged)
            {
                // SetValue with null engages the optional via default-construction;
                // we then deserialize the payload directly into the engaged slot.
                SetValue(Value, nullptr);
                Inner->Serialize(Ar, GetValue(Value));
            }
            else
            {
                Reset(Value);
            }
        }
    }

    void FOptionalProperty::SerializeItem(IStructuredArchive::FSlot Slot, void* Value, void const* Defaults)
    {
        UNREACHABLE();
    }
}
