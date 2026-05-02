#include "pch.h"
#include "ArrayProperty.h"

namespace Lumina
{
    void FArrayProperty::Serialize(FArchive& Ar, void* Value)
    {
        SIZE_T ElementCount = GetNum(Value);
        Ar << ElementCount;
        
        size_t SerializedInnerElementSize = Inner->GetElementSize();
        Ar << SerializedInnerElementSize;

        const size_t CurrentInnerElementSize = Inner->GetElementSize();
        // Trivial types memcpy in bulk so size must match; non-trivial types tolerate in-memory padding diffs.
        if (Ar.IsReading() && Inner->IsTrivial() && SerializedInnerElementSize != CurrentInnerElementSize)
        {
            LOG_ERROR("Inner element size changed for array '{}' (inner '{}'), aborting load: Current=({}) Serialized=({})", Name, Inner->Name, CurrentInnerElementSize, SerializedInnerElementSize);
            return;
        }
        const size_t InnerElementSize = CurrentInnerElementSize;
        
        if (ElementCount > eastl::numeric_limits<uint32>::max())
        {
            LOG_ERROR("Array Property tried to serialize {} elements. Aborted", ElementCount);
            return;
        }

        if (Ar.IsWriting())
        {
            if (Inner->IsTrivial() && ElementCount)
            {
                Ar.Serialize(GetAt(Value, 0), static_cast<int64>(ElementCount * InnerElementSize));
            }
            else
            {
                for (SIZE_T i = 0; i < ElementCount; i++)
                {
                    Inner->Serialize(Ar, GetAt(Value, i));
                }   
            }
        }
        else
        {
            if (Inner->IsTrivial() && ElementCount)
            {
                Resize(Value, ElementCount);
                Ar.Serialize(GetAt(Value, 0), static_cast<int64>(ElementCount * InnerElementSize));
            }
            else
            {
                Resize(Value, ElementCount);
                for (SIZE_T i = 0; i < ElementCount; ++i)
                {
                    Inner->Serialize(Ar, GetAt(Value, i));
                }
            }
        }
    }

    void FArrayProperty::SerializeItem(IStructuredArchive::FSlot Slot, void* Value, void const* Defaults)
    {
        UNREACHABLE();
    }

    bool FArrayProperty::Identical(const void* ValueA, const void* ValueB) const
    {
        const SIZE_T NumA = GetNum(ValueA);
        const SIZE_T NumB = GetNum(ValueB);
        if (NumA != NumB)
        {
            return false;
        }

        for (SIZE_T i = 0; i < NumA; ++i)
        {
            const void* ElemA = GetAt(const_cast<void*>(ValueA), i);
            const void* ElemB = GetAt(const_cast<void*>(ValueB), i);
            if (!Inner->Identical(ElemA, ElemB))
            {
                return false;
            }
        }
        return true;
    }

    void FArrayProperty::CopyCompleteValue(void* Dst, const void* Src) const
    {
        const SIZE_T SrcCount = GetNum(Src);
        Resize(Dst, SrcCount);
        for (SIZE_T i = 0; i < SrcCount; ++i)
        {
            void* DstElem = GetAt(Dst, i);
            const void* SrcElem = GetAt(const_cast<void*>(Src), i);
            Inner->CopyCompleteValue(DstElem, SrcElem);
        }
    }
}
