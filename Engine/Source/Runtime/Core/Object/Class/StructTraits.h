#pragma once

#include "StructConcepts.h"
#include "Containers/String.h"


namespace Lumina
{
    class FArchive;

    struct FStructOps
    {
        using SerializeFn   = bool(*)(FArchive&, void*);
        using CopyFn        = void(*)(void*, const void*);
        using EqualsFn      = bool(*)(const void*, const void*);
        using ToStringFn    = FString(*)(const void*);
        using LessThanFn    = bool(*)(const void*, const void*);

        SerializeFn     Serialize   = nullptr;
        CopyFn          Copy        = nullptr;
        EqualsFn        Equals      = nullptr;
        ToStringFn      ToString    = nullptr;
        LessThanFn      LessThan    = nullptr;

        bool HasSerializer()    const { return Serialize    != nullptr; }
        bool HasCopy()          const { return Copy         != nullptr; }
        bool HasEquality()      const { return Equals       != nullptr; }
        bool HasToString()      const { return ToString     != nullptr; }
        bool HasLessThan()      const { return LessThan     != nullptr; }
    };
    
    template<typename T>
    FStructOps* MakeStructOps()
    {
        FStructOps* Ops = new FStructOps{};
        
        if constexpr (Concepts::THasSerialize<T>)
        {
            Ops->Serialize = +[](FArchive& Ar, void* Value)
            {
                return static_cast<T*>(Value)->Serialize(Ar);
            };
        }
        
        if constexpr (Concepts::THasCopy<T>)
        {
            Ops->Copy = +[](void* Dst, const void* Src)
            {
                static_cast<T*>(Dst)->CopyFrom(*static_cast<const T*>(Src));
            };
        }
        
        if constexpr (Concepts::THasEquality<T>)
        {
            Ops->Equals = +[](const void* LHS, const void* RHS)
            {
                return *static_cast<const T*>(LHS) == *static_cast<const T*>(RHS);
            };
        }
        
        if constexpr (Concepts::THasToString<T>)
        {
            Ops->ToString = +[](const void* Data)
            {
                return static_cast<const T*>(Data)->ToString();
            };
        }
        
        if constexpr (Concepts::THasLessThan<T>)
        {
            Ops->LessThan = +[](const void* LHS, const void* RHS)
            {
                return *static_cast<const T*>(LHS) < *static_cast<const T*>(RHS);
            };
        }
        
        return Ops;
    }
}
