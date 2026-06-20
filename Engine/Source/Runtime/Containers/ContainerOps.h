#pragma once

#include "Containers/Array.h"

namespace Lumina
{
    // Type-erased operation table for a TVector<T>: a vtable of pure function pointers that operate on a
    // TVector<T> instance (the vector pointer itself, NOT the owning object) without knowing T. One static
    // table per element type, produced by GetVectorOps<T>(). This is the single source of truth for "how to
    // operate on a reflected vector," shared by:
    //   - FArrayProperty (Core reflection): serialization / net / copy / editor property table.
    //   - LuminaSharp.NativeList<T> (C#): reads decode the EASTL header in place; mutators call these fn-ptrs.
    //
    // It is deliberately decoupled from reflection: any TVector<T> -- a reflected member, a function return, a
    // plain local -- is operated on the same way. The field order/layout MUST match LuminaSharp.VectorOps
    // (Core/NativeList.cs); the C# side reads PushBack/RemoveAt/Clear at fixed offsets. Captureless lambdas
    // convert to plain function pointers whose calling convention matches C#'s delegate* unmanaged[Cdecl].
    struct FVectorOps
    {
        SIZE_T (*Size)(const void* Vector);
        void*  (*Data)(void* Vector);
        void   (*PushBack)(void* Vector, const void* Element); // null Element => default-construct (emplace_back)
        void   (*RemoveAt)(void* Vector, SIZE_T Index);
        void   (*Clear)(void* Vector);
        void   (*Resize)(void* Vector, SIZE_T Size);
        void   (*Reserve)(void* Vector, SIZE_T Size);
        void   (*Swap)(void* Vector, SIZE_T LHS, SIZE_T RHS);
        uint32 ElementSize;
    };

    template <typename T>
    const FVectorOps* GetVectorOps()
    {
        static const FVectorOps Ops =
        {
            [](const void* V) -> SIZE_T { return static_cast<const TVector<T>*>(V)->size(); },
            [](void* V) -> void* { return static_cast<TVector<T>*>(V)->data(); },
            [](void* V, const void* E) { TVector<T>* Vec = static_cast<TVector<T>*>(V); if (E) { Vec->push_back(*static_cast<const T*>(E)); } else { Vec->emplace_back(); } },
            [](void* V, SIZE_T I) { TVector<T>* Vec = static_cast<TVector<T>*>(V); Vec->erase(Vec->begin() + I); },
            [](void* V) { static_cast<TVector<T>*>(V)->clear(); },
            [](void* V, SIZE_T N) { static_cast<TVector<T>*>(V)->resize(N); },
            [](void* V, SIZE_T N) { static_cast<TVector<T>*>(V)->reserve(N); },
            [](void* V, SIZE_T A, SIZE_T B) { TVector<T>* Vec = static_cast<TVector<T>*>(V); T Tmp = (*Vec)[A]; (*Vec)[A] = (*Vec)[B]; (*Vec)[B] = Tmp; },
            static_cast<uint32>(sizeof(T)),
        };
        return &Ops;
    }
}
