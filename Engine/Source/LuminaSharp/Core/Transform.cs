using System.Runtime.InteropServices;

namespace Lumina;

/// <summary>
/// Hand-written blittable mirror of the engine's SIMD FTransform (VTransform). The native type stores three
/// 16-byte VFloat4 (Location.xyz+pad, Rotation.xyzw, Scale.xyz+pad), so it's 48 bytes with a pad float after
/// Location and after Scale. This mirror reproduces that exact byte layout (Location@0, Rotation@16, Scale@32)
/// so it blits by value across the boundary; the pad fields are private. Validated by static_asserts in
/// CSharpLayoutChecks.cpp. The native type is ManualStub + NoCSharp precisely so this hand-written mirror is
/// used instead of an auto-generated one (C# can't express the 16-byte alignment / padding otherwise).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
[LuminaSharp.NativeLayout("FTransform")]
public struct FTransform
{
    public FVector3 Location;   // @0
    private float Pad0;         // @12 (VFloat4 Location's unused w lane)
    public FQuat Rotation;      // @16
    public FVector3 Scale;      // @32
    private float Pad1;         // @44 (VFloat4 Scale's unused w lane)

    public FTransform(FVector3 Location, FQuat Rotation, FVector3 Scale)
    {
        this.Location = Location;
        Pad0 = 0.0f;
        this.Rotation = Rotation;
        this.Scale = Scale;
        Pad1 = 0.0f;
    }

    public static FTransform Identity => new(FVector3.Zero, FQuat.Identity, FVector3.One);
}
