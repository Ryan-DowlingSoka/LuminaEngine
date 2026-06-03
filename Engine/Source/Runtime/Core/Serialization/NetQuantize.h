#pragma once

#include "Core/Math/Math.h"

namespace Lumina
{
    class FNetArchive;

    // Compact wire encodings for replicated transforms, written through FNetArchive's bit stream.
    namespace NetQuantize
    {
        inline constexpr double PositionQuantum = 0.001; // 1 mm

        void WritePackedVector(FNetArchive& Ar, const FVector3& V);
        void ReadPackedVector (FNetArchive& Ar, FVector3& V);

        void WritePackedQuat(FNetArchive& Ar, const FQuat& Q);
        void ReadPackedQuat (FNetArchive& Ar, FQuat& Q);
    }
}
