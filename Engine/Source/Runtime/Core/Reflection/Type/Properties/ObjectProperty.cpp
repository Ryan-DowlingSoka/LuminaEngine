#include "pch.h"
#include "ObjectProperty.h"

#include "Core/Object/Object.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Core/Object/ObjectCore.h"
#include "Core/Serialization/NetArchive.h"


namespace Lumina
{

    void FObjectProperty::Serialize(FArchive& Ar, void* Value)
    {
        auto Ptr = static_cast<TObjectPtr<CObject>*>(Value);

        CObject* Raw = Ptr->Get();
        Ar << Raw;
        *Ptr = Raw;
    }

    void FObjectProperty::NetSerialize(FNetArchive& Ar, void* Value)
    {
        auto Ptr = static_cast<TObjectPtr<CObject>*>(Value);

        if (Ar.IsWriting())
        {
            CObject* Raw  = Ptr->Get();
            FGuid    Guid = Raw ? Raw->GetGUID() : FGuid();
            Ar << Guid;
        }
        else
        {
            FGuid Guid;
            Ar << Guid;

            CObject* Resolved = nullptr;
            if (Guid.IsValid())
            {
                Resolved = FindObject<CObject>(Guid);
                if (Resolved == nullptr)
                {
                    Resolved = StaticLoadObject(Guid); // not resident yet -> try to load by GUID
                }
            }
            *Ptr = Resolved;
        }
    }

    void FObjectProperty::SerializeItem(IStructuredArchive::FSlot Slot, void* Value, void const* Defaults)
    {
        auto Ptr = static_cast<TObjectPtr<CObject>*>(Value);

        CObject* Raw = Ptr->Get();
        Slot.Serialize(Raw);
        *Ptr = Raw;
    }

    void FObjectProperty::CopyCompleteValue(void* Dst, const void* Src) const
    {
        *static_cast<TObjectPtr<CObject>*>(Dst) = *static_cast<const TObjectPtr<CObject>*>(Src);
    }
}
