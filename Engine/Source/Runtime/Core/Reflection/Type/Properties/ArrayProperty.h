#pragma once

#include "Core/Reflection/Type/LuminaTypes.h"
#include "Memory/SmartPtr.h"

namespace Lumina
{

    
    class FArrayProperty : public FProperty
    {
    public:
        FArrayProperty(const FFieldOwner& InOwner, const FArrayPropertyParams* Params)
            : FProperty(InOwner, Params)
        {
            PushBackFn  = Params->PushBackFn;
            GetNumFn    = Params->GetNumFn;
            RemoveAtFn  = Params->RemoveAtFn;
            ClearFn     = Params->ClearFn;
            GetAtFn     = Params->GetAtFn;
            ResizeFn    = Params->ResizeFn;
            ReserveFn   = Params->ReserveFn;
            SwapFn      = Params->SwapFn;
        }
        
        void AddProperty(FProperty* Property) override { Inner.reset(Property); }

        void Serialize(FArchive& Ar, void* Value) override;
        void SerializeItem(IStructuredArchive::FSlot Slot, void* Value, void const* Defaults) override;

        // Tight: uint32 count + each element via the element property's own NetSerialize.
        RUNTIME_API void NetSerialize(FNetArchive& Ar, void* Value) override;

        /** Per-element compare via Inner; CopyCompleteValue resizes Dst then element-copies. */
        RUNTIME_API bool Identical(const void* ValueA, const void* ValueB) const override;
        RUNTIME_API void CopyCompleteValue(void* Dst, const void* Src) const override;

        FProperty* GetInternalProperty() const { return Inner.get(); }
        
        SIZE_T GetNum(const void* InContainer) const
        {
            return GetNumFn(InContainer);
        }

        void PushBack(void* InContainer, const void* InValue) const
        {
            PushBackFn(InContainer, InValue);
        }

        void RemoveAt(void* InContainer, size_t Index) const
        {
            RemoveAtFn(InContainer, Index);
        }

        void Clear(void* InContainer) const
        {
            ClearFn(InContainer);
        }

        void* GetAt(void* InContainer, size_t Index) const
        {
            return GetAtFn(InContainer, Index);
        }
        
        void Resize(void* InContainer, size_t Size) const
        {
            ResizeFn(InContainer, Size);
        }

        void Reserve(void* InContainer, size_t Size) const
        {
            ReserveFn(InContainer, Size);
        }
        
        void Swap(void* InContainer, size_t LHS, size_t RHS) const
        {
            SwapFn(InContainer, LHS, RHS);
        }

        template<typename T = void, typename TFunc>
        void ForEach(void* InContainer, TFunc&& Func) const
        {
            SIZE_T Num = GetNum(InContainer);
            for (SIZE_T i = 0; i < Num; ++i)
            {
                if constexpr (std::is_same_v<T, void>)
                {
                    void* Elem = GetAt(InContainer, i);
                    Func(Elem, i);
                }
                else
                {
                    T* Elem = static_cast<T*>(GetAt(InContainer, i));
                    Func(Elem, i);
                }
            }
        }
        
    private:

        ArrayPushBackPtr        PushBackFn;
        ArrayGetNumPtr          GetNumFn;
        ArrayRemoveAtPtr        RemoveAtFn;
        ArrayClearPtr           ClearFn;
        ArrayGetAtPtr           GetAtFn;
        ArrayResizePtr          ResizeFn;
        ArrayReservePtr         ReserveFn;
        ArraySwapPtr            SwapFn;

        TUniquePtr<FProperty>   Inner;
        
    };
}
