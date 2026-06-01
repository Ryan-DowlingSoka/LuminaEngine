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
        FSoftObjectPath* Path = static_cast<FSoftObjectPath*>(Value);

        if (Slot.GetArchiver().IsReading())
        {
            FString PathStr;
            Slot.Serialize(PathStr);
            Path->SetPath(FStringView(PathStr.c_str(), PathStr.size()));
        }
        else
        {
            const FStringView View = Path->GetPath();
            FString PathStr(View.data(), View.size());
            Slot.Serialize(PathStr);
        }
    }
}
