using System;
using System.Runtime.InteropServices;

namespace LuminaSharp;

/// <summary>
/// Resolves native engine exports into raw function pointers for the generated bindings.
/// </summary>
public static unsafe class NativeBindings
{
    /// <summary>Resolves an export from a module (by module name) to a function pointer; null on miss.</summary>
    public static void* Resolve(string Module, string EntryPoint)
    {
        IntPtr Handle = Host.ModuleHandle(Module);
        if (Handle == IntPtr.Zero)
        {
            Native.Log(ELogLevel.Error, $"NativeBindings: module '{Module}' not loaded for '{EntryPoint}'.");
            return null;
        }
        return ResolveFrom(Handle, EntryPoint);
    }

    /// <summary>Resolves an export from an already-known module handle (used for the bootstrap binds).</summary>
    public static void* ResolveFrom(IntPtr ModuleHandle, string EntryPoint)
    {
        if (ModuleHandle != IntPtr.Zero && NativeLibrary.TryGetExport(ModuleHandle, EntryPoint, out IntPtr Export))
        {
            return (void*)Export;
        }
        Native.Log(ELogLevel.Error, $"NativeBindings: export '{EntryPoint}' not found.");
        return null;
    }

    // Property resolve helpers for the generated bindings. A blittable property caches just its byte
    // offset (PropertyOffset); a non-blittable property caches its FProperty* token (FindProperty). Both
    // resolve once per property in a generated `static readonly` initializer, then the get/set bodies use
    // the cached value with no further name lookup. Type is the reflected type's simple name (resolved
    // native-side via FindObject, mirroring the component-ops lookup).

    /// <summary>The byte offset of Type.Prop within its container, resolved once from live reflection.</summary>
    public static int PropertyOffset(string Type, string Prop)
    {
        Span<byte> TypeScratch = stackalloc byte[256];
        Span<byte> PropScratch = stackalloc byte[256];
        Interop.FInteropString TypeUtf8 = new(Type, TypeScratch);
        Interop.FInteropString PropUtf8 = new(Prop, PropScratch);
        try
        {
            return PropertyOffsetByName(TypeUtf8.Pointer, TypeUtf8.Length, PropUtf8.Pointer, PropUtf8.Length);
        }
        finally
        {
            TypeUtf8.Free();
            PropUtf8.Free();
        }
    }

    /// <summary>The FProperty* token for Type.Prop (opaque), resolved once; IntPtr.Zero on miss.</summary>
    public static IntPtr FindProperty(string Type, string Prop)
    {
        Span<byte> TypeScratch = stackalloc byte[256];
        Span<byte> PropScratch = stackalloc byte[256];
        Interop.FInteropString TypeUtf8 = new(Type, TypeScratch);
        Interop.FInteropString PropUtf8 = new(Prop, PropScratch);
        try
        {
            return FindPropertyExport(TypeUtf8.Pointer, TypeUtf8.Length, PropUtf8.Pointer, PropUtf8.Length);
        }
        finally
        {
            TypeUtf8.Free();
            PropUtf8.Free();
        }
    }

    private static readonly delegate* unmanaged[Cdecl]<byte*, int, byte*, int, int> PropertyOffsetByName =
        (delegate* unmanaged[Cdecl]<byte*, int, byte*, int, int>)Resolve(Host.NativeLibrary, "LuminaSharp_PropertyOffsetByName");

    private static readonly delegate* unmanaged[Cdecl]<byte*, int, byte*, int, IntPtr> FindPropertyExport =
        (delegate* unmanaged[Cdecl]<byte*, int, byte*, int, IntPtr>)Resolve(Host.NativeLibrary, "LuminaSharp_FindProperty");
}
