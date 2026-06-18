#include "pch.h"
#include "ClassProperty.h"

#include "Core/Object/Class.h"
#include "Core/Object/ObjectCore.h"

namespace Lumina
{
    void FClassProperty::Serialize(FArchive& Ar, void* Value)
    {
        auto Ptr = static_cast<CClass**>(Value);

        if (Ar.IsReading())
        {
            FName ClassName;
            Ar << ClassName;
            *Ptr = ClassName.IsNone() ? nullptr : FindObject<CClass>(ClassName);
        }
        else
        {
            FName ClassName = *Ptr ? (*Ptr)->GetName() : NAME_None;
            Ar << ClassName;
        }
    }

    void FClassProperty::SerializeItem(IStructuredArchive::FSlot Slot, void* Value, void const* Defaults)
    {
        auto Ptr = static_cast<CClass**>(Value);

        if (Slot.GetArchiver().IsReading())
        {
            FName ClassName;
            Slot.Serialize(ClassName);
            *Ptr = ClassName.IsNone() ? nullptr : FindObject<CClass>(ClassName);
        }
        else
        {
            FName ClassName = *Ptr ? (*Ptr)->GetName() : NAME_None;
            Slot.Serialize(ClassName);
        }
    }
}
