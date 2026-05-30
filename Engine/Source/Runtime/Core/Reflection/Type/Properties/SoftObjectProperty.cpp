#include "pch.h"
#include "SoftObjectProperty.h"

#include "Core/Object/SoftObjectPtr.h"


namespace Lumina
{
    void FSoftObjectProperty::Serialize(FArchive& Ar, void* Value)
    {
        // TSoftObjectPtr<T> holds a single FSoftObjectPath, so reinterpreting the storage
        // is safe; the path's operator<< handles the save-side soft-ref hook.
        FSoftObjectPath* Path = static_cast<FSoftObjectPath*>(Value);
        Ar << *Path;
    }

    void FSoftObjectProperty::SerializeItem(IStructuredArchive::FSlot Slot, void* Value, void const* Defaults)
    {
        // No-op until structured archives land, matching FString/FNameProperty; cook uses
        // binary Serialize() above. Do NOT assert here — other primitives behave the same.
        (void)Slot; (void)Value; (void)Defaults;
    }
}
