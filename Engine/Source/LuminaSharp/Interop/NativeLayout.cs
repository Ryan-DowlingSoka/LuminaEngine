using System;

namespace LuminaSharp;

/// <summary>
/// Declares that a blittable C# value type is a byte-for-byte mirror of a native C++ type. At host bootstrap
/// <see cref="LayoutValidator"/> cross-checks <c>Unsafe.SizeOf&lt;T&gt;()</c> against the real native
/// <c>sizeof</c> (reported by the engine through <c>LuminaSharp_Layout_GetSize</c>); any mismatch aborts C#
/// startup loudly instead of letting a bad layout silently corrupt interop. <paramref name="NativeType"/> is
/// the registration key the native side registers the type's size under (e.g. <c>"RHI::FTextureDesc"</c>),
/// which must match a <c>LE_REGISTER_LAYOUT(key, Type)</c> on the C++ side.
/// </summary>
[AttributeUsage(AttributeTargets.Struct, Inherited = false)]
public sealed class NativeLayoutAttribute : Attribute
{
    public string NativeType { get; }

    public NativeLayoutAttribute(string NativeType)
    {
        this.NativeType = NativeType;
    }
}
