using System;

namespace LuminaSharp;

/// Declares a blittable C# value type as a byte-for-byte mirror of a native C++ type; bootstrap aborts on a sizeof mismatch. NativeType must match a LE_REGISTER_LAYOUT key on the C++ side.
[AttributeUsage(AttributeTargets.Struct, Inherited = false)]
public sealed class NativeLayoutAttribute : Attribute
{
    public string NativeType { get; }

    public NativeLayoutAttribute(string NativeType)
    {
        this.NativeType = NativeType;
    }
}
