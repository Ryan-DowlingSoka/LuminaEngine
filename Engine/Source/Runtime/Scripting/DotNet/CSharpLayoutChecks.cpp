#include "pch.h"

#include "Core/Math/Quat/Quat.h"
#include "Core/Math/Vector/VectorTypes.h"
#include "Core/Math/Matrix/Matrix.h"
#include "Core/Math/Transform.h"
#include "Scripting/DotNet/LayoutRegistry.h"

namespace Lumina
{
    // The hand-written LuminaSharp math value types (Math.cs / Matrix.cs) are [StructLayout(Sequential)]
    // mirrors of these. They're ManualStub, so the Reflector doesn't emit them or an auto size assert;
    // these guards catch a native size drift at compile time. (Reflected blittable value mirrors, e.g.
    // SPerceptionEvent / SCollisionEvent, get their size assert auto-emitted into the generated bindings.)
    static_assert(sizeof(FVector2) == 8,  "LuminaSharp FVector2 mirror size mismatch (update Math.cs).");
    static_assert(sizeof(FVector3) == 12, "LuminaSharp FVector3 mirror size mismatch (update Math.cs).");
    static_assert(sizeof(FVector4) == 16, "LuminaSharp FVector4 mirror size mismatch (update Math.cs).");
    static_assert(sizeof(FQuat)    == 16, "LuminaSharp FQuat mirror size mismatch (update Math.cs).");
    static_assert(sizeof(FMatrix4) == 64, "LuminaSharp FMatrix mirror size mismatch (update Matrix.cs).");

    // SIMD FTransform (VTransform): three 16-byte VFloat4 (Location.xyz+pad, Rotation.xyzw, Scale.xyz+pad).
    // The hand-written C# mirror (Transform.cs) reproduces this padded 48-byte layout for the by-value blit.
    static_assert(sizeof(FTransform)              == 48, "LuminaSharp FTransform mirror size mismatch (update Transform.cs).");
    static_assert(offsetof(FTransform, Location)  == 0,  "FTransform.Location offset mismatch.");
    static_assert(offsetof(FTransform, Rotation)  == 16, "FTransform.Rotation offset mismatch.");
    static_assert(offsetof(FTransform, Scale)     == 32, "FTransform.Scale offset mismatch.");

    // Runtime cross-check: report each math mirror's native sizeof so the managed LayoutValidator can compare
    // it against Unsafe.SizeOf<T>() at bootstrap (keys match the C# [LuminaSharp.NativeLayout("...")]).
    LE_REGISTER_LAYOUT("FVector2",     FVector2);
    LE_REGISTER_LAYOUT("FVector3",     FVector3);
    LE_REGISTER_LAYOUT("FVector4",     FVector4);
    LE_REGISTER_LAYOUT("FQuat",        FQuat);
    LE_REGISTER_LAYOUT("FMatrix",      FMatrix4);
    LE_REGISTER_LAYOUT("FTransform",   FTransform);
    LE_REGISTER_LAYOUT("FUIntVector2", FUIntVector2);
    LE_REGISTER_LAYOUT("FUIntVector3", FUIntVector3);
    LE_REGISTER_LAYOUT("FIntVector2",  FIntVector2);
    LE_REGISTER_LAYOUT("FIntVector3",  FIntVector3);
}
