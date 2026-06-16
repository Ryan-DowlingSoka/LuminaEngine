#include "pch.h"

#include "Core/Math/Quat/Quat.h"
#include "Core/Math/Vector/VectorTypes.h"

namespace Lumina
{
    // The hand-written LuminaSharp math value types (Math.cs) are [StructLayout(Sequential)] mirrors
    // of these. If a size changes here without updating Math.cs, the mirror corrupts across the
    // boundary, these guards catch it at compile time.
    static_assert(sizeof(FVector2) == 8,  "LuminaSharp FVector2 mirror size mismatch (update Math.cs).");
    static_assert(sizeof(FVector3) == 12, "LuminaSharp FVector3 mirror size mismatch (update Math.cs).");
    static_assert(sizeof(FVector4) == 16, "LuminaSharp FVector4 mirror size mismatch (update Math.cs).");
    static_assert(sizeof(FQuat)    == 16, "LuminaSharp FQuat mirror size mismatch (update Math.cs).");
}
