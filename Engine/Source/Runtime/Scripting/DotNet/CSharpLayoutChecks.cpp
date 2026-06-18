#include "pch.h"

#include "Core/Math/Quat/Quat.h"
#include "Core/Math/Vector/VectorTypes.h"
#include "World/Entity/Events/PerceptionEvent.h"

namespace Lumina
{
    // The hand-written LuminaSharp math value types (Math.cs) are [StructLayout(Sequential)] mirrors
    // of these. If a size changes here without updating Math.cs, the mirror corrupts across the
    // boundary, these guards catch it at compile time.
    static_assert(sizeof(FVector2) == 8,  "LuminaSharp FVector2 mirror size mismatch (update Math.cs).");
    static_assert(sizeof(FVector3) == 12, "LuminaSharp FVector3 mirror size mismatch (update Math.cs).");
    static_assert(sizeof(FVector4) == 16, "LuminaSharp FVector4 mirror size mismatch (update Math.cs).");
    static_assert(sizeof(FQuat)    == 16, "LuminaSharp FQuat mirror size mismatch (update Math.cs).");

    // SPerceptionEvent (PerceptionEvent.h) is mirrored by the blittable LuminaSharp SPerceptionEvent
    // (PerceptionEvent.cs). Both must be field-for-field identical or the perception callbacks corrupt.
    static_assert(sizeof(SPerceptionEvent)             == 28, "LuminaSharp SPerceptionEvent mirror size mismatch (update PerceptionEvent.cs).");
    static_assert(offsetof(SPerceptionEvent, Perceiver) == 0,  "SPerceptionEvent.Perceiver offset mismatch.");
    static_assert(offsetof(SPerceptionEvent, Target)    == 4,  "SPerceptionEvent.Target offset mismatch.");
    static_assert(offsetof(SPerceptionEvent, Location)  == 8,  "SPerceptionEvent.Location offset mismatch.");
    static_assert(offsetof(SPerceptionEvent, Sense)     == 20, "SPerceptionEvent.Sense offset mismatch.");
    static_assert(offsetof(SPerceptionEvent, Strength)  == 24, "SPerceptionEvent.Strength offset mismatch.");
}
