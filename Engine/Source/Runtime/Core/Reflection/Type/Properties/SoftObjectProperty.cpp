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
        // Intentionally a no-op — matches FStringProperty / FNameProperty,
        // which also leave SerializeItem empty until reflection-driven
        // JSON/YAML structured archives land. The cook flow uses the
        // binary Serialize() above, which DOES handle paths correctly.
        //
        // Do NOT replace this with an assert: every other primitive
        // property does the same and crashing here would diverge from
        // house style. When structured archives ship, this body should
        // be filled in alongside the other property stubs.
        (void)Slot; (void)Value; (void)Defaults;
    }
}
