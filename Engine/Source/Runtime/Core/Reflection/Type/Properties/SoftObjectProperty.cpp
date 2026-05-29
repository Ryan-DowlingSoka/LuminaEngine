#include "pch.h"
#include "SoftObjectProperty.h"

#include "Core/Object/SoftObjectPtr.h"


namespace Lumina
{
    void FSoftObjectProperty::Serialize(FArchive& Ar, void* Value)
    {
        // Both FSoftObjectPath and TSoftObjectPtr<T> serialize identically —
        // T<T> is a templated wrapper holding a single FSoftObjectPath, so
        // reinterpreting the storage is safe and avoids a templated dispatch
        // here. The path's operator<< handles the save-side soft-ref hook.
        FSoftObjectPath* Path = static_cast<FSoftObjectPath*>(Value);
        Ar << *Path;
    }

    void FSoftObjectProperty::SerializeItem(IStructuredArchive::FSlot Slot, void* Value, void const* Defaults)
    {
        // Structured-archive path not used by the cook flow today; FStringProperty
        // does the same. Reflection-driven JSON/YAML editor serialization will fill
        // this in when it lands.
    }
}
