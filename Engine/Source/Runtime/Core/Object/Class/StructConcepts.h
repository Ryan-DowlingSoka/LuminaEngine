#pragma once

#include "Containers/String.h"

namespace Lumina
{
    class FArchive;
}

namespace Lumina::Concepts
{
    template<typename T>
    concept THasSerialize = requires(T& V, FArchive& Ar)
    {
        { V.Serialize(Ar) } -> std::same_as<bool>;
    };
    
    template<typename T>
    concept THasCopy = requires(T& Dst, const T& Src)
    {
        { Dst.CopyFrom(Src) } -> std::same_as<void>;
    };
    
    template<typename T>
    concept THasEquality = requires(const T& A, const T& B)
    {
        { A == B } -> std::same_as<bool>;
    };
    
    template<typename T>
    concept THasToString = requires(const T& V)
    {
        { V.ToString() } -> std::same_as<FString>;
    };
    
    template<typename T>
    concept THasLessThan = requires(const T& A, const T& B)
    {
        { A < B } -> std::same_as<bool>;
    };
}
