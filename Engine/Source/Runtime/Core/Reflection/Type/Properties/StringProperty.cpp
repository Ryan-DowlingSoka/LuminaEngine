#include "pch.h"
#include "StringProperty.h"

namespace Lumina
{
    void FStringProperty::Serialize(FArchive& Ar, void* Value)
    {
        FString* StringValue = (FString*)Value;
        Ar << *StringValue;
    }

    void FStringProperty::SerializeItem(IStructuredArchive::FSlot Slot, void* Value, void const* Defaults)
    {
    }

    void FNameProperty::SerializeItem(IStructuredArchive::FSlot Slot, void* Value, void const* Defaults)
    {
    }

    void FNameProperty::Serialize(FArchive& Ar, void* Value)
    {
        FName* StringValue = (FName*)Value;
        Ar << *StringValue;
    }

    bool FStringProperty::Identical(const void* ValueA, const void* ValueB) const
    {
        return *static_cast<const FString*>(ValueA) == *static_cast<const FString*>(ValueB);
    }

    void FStringProperty::CopyCompleteValue(void* Dst, const void* Src) const
    {
        *static_cast<FString*>(Dst) = *static_cast<const FString*>(Src);
    }
}
