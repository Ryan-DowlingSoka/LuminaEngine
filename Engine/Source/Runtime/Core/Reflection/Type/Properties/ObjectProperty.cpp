#include "pch.h"
#include "ObjectProperty.h"

#include "Core/Object/Object.h"
#include "Core/Object/ObjectHandleTyped.h"


namespace Lumina
{
    
    void FObjectProperty::Serialize(FArchive& Ar, void* Value)
    {
        auto Ptr = static_cast<TObjectPtr<CObject>*>(Value);
        
        CObject* Raw = Ptr->Get();
        Ar << Raw;
        *Ptr = Raw;
    }

    void FObjectProperty::SerializeItem(IStructuredArchive::FSlot Slot, void* Value, void const* Defaults)
    {
        auto Ptr = static_cast<TObjectPtr<CObject>*>(Value);

        CObject* Raw = Ptr->Get();
        Slot.Serialize(Raw);
        *Ptr = Raw;
    }
}
