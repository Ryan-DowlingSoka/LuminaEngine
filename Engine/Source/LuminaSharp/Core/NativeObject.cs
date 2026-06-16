using System;

namespace LuminaSharp;

/// <summary>
/// Base for the generated opaque wrappers around native CObjects. Holds the native handle; the typed
/// property/method API is added by the generated binding layer as it grows. Generated wrappers (in
/// namespace Lumina) derive from this.
/// </summary>
public class NativeObject
{
    internal IntPtr Handle;

    internal NativeObject(IntPtr Handle)
    {
        this.Handle = Handle;
    }

    protected NativeObject()
    {
    }

    /// <summary>Rebinds this wrapper to a different native instance without allocating. Used by the
    /// View to reuse ONE wrapper per type across an iteration, reassigning the handle each step.</summary>
    internal void SetHandle(IntPtr NewHandle)
    {
        Handle = NewHandle;
    }
}

/// <summary>
/// Base for the generated opaque wrappers around native structs that are NOT blittable (they hold
/// FString/containers/smart-pointers, so they can't be mirrored by value). Blittable structs get a
/// real [StructLayout(Sequential)] value type instead.
///
/// A wrapper is normally a VIEW: <see cref="Handle"/> points at a component living in the ECS (returned
/// by Registry.Get/Emplace). A component wrapper can also be constructed detached (`new T()`), which
/// allocates an engine-owned native instance; that one OWNS its memory and must be disposed (use
/// `using`, or pass it to Registry.Emplace which copies it in). Disposing a view is a no-op.
/// </summary>
public class NativeStruct : IDisposable
{
    internal IntPtr Handle;
    internal bool Owned;    // true only for a detached `new T()` instance
    internal IntPtr Ops;    // the component's op-table token (for the native deleter)

    internal NativeStruct(IntPtr Handle)
    {
        this.Handle = Handle;
    }

    protected NativeStruct()
    {
    }

    /// <summary>Detached-construction ctor: allocates an engine-owned default instance of TypeName.</summary>
    protected NativeStruct(string TypeName)
    {
        Ops = Native.FindComponentOps(TypeName);
        Handle = Native.NewComponent(Ops);
        Owned = Handle != IntPtr.Zero;
    }

    /// <summary>Rebinds this wrapper to a different live component without allocating. Used by the View
    /// to reuse ONE wrapper per component type across an iteration, reassigning the handle each step.
    /// Only valid on a non-owning VIEW wrapper.</summary>
    internal void SetHandle(IntPtr NewHandle)
    {
        Handle = NewHandle;
    }

    public void Dispose()
    {
        if (Owned && Handle != IntPtr.Zero)
        {
            Native.DeleteComponent(Ops, Handle);
            Handle = IntPtr.Zero;
            Owned = false;
        }
    }
}
