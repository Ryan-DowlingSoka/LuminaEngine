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

    bool FOptionalProperty::Identical(const void* ValueA, const void* ValueB) const
    {
        const bool bA = HasValue(ValueA);
        const bool bB = HasValue(ValueB);
        if (bA != bB)
        {
            return false;
        }

        if (!bA)
        {
            return true;
        }

        const void* PayloadA = GetValueFn(const_cast<void*>(ValueA));
        const void* PayloadB = GetValueFn(const_cast<void*>(ValueB));
        return Inner->Identical(PayloadA, PayloadB);
    }

    void FOptionalProperty::CopyCompleteValue(void* Dst, const void* Src) const
    {
        const bool bSrcEngaged = HasValue(Src);
        if (!bSrcEngaged)
        {
            Reset(Dst);
            return;
        }

        // Engage Dst with a default-constructed payload, then in-place
        // copy the source payload into it.
        SetValue(Dst, nullptr);
        void* DstPayload = GetValueFn(Dst);
        const void* SrcPayload = GetValueFn(const_cast<void*>(Src));
        Inner->CopyCompleteValue(DstPayload, SrcPayload);
    }
}
