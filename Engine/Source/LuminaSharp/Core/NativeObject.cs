using System;

namespace LuminaSharp;

/// <summary>
/// Base for the generated opaque wrappers around native CObjects. Holds a WEAK handle (the CObject's
/// object-array index + generation) rather than a bare pointer, so a wrapper to a since-destroyed object
/// fails loudly instead of silently reading reclaimed memory: <see cref="IsValid"/> reports liveness (like
/// Unity's <c>obj != null</c>), and every generated property/method accessor reads through
/// <see cref="Handle"/>, which re-resolves and throws once the object is freed. Engine-constructed only,
/// instances are handed out by the engine (Asset.Load, the generated getters, ...), never <c>new</c>'d by
/// user code. CObjects don't move while alive, so re-resolving returns the same pointer; it's purely a
/// generation-validated liveness gate.
/// </summary>
public class NativeObject
{
    private readonly IntPtr RawHandle;       // pointer captured at construction; the fallback when untracked
    private readonly int ObjectIndex;        // GObjectArray slot, or -1 if the object isn't array-tracked
    private readonly int ObjectGeneration;   // slot generation at capture; a free/reuse bumps it -> stale

    internal NativeObject(IntPtr Handle)
    {
        RawHandle = Handle;
        long Packed = Native.ObjectGetHandle(Handle);
        ObjectIndex = unchecked((int)Packed);
        ObjectGeneration = (int)(Packed >> 32);
    }

    /// <summary>True while the native CObject this wraps is still alive (index + generation validated
    /// against the object array). Mirrors Unity's <c>obj != null</c>, check it before using a reference
    /// you've held across frames, asset unloads, or other structural changes.</summary>
    public bool IsValid => ObjectIndex < 0
        ? RawHandle != IntPtr.Zero
        : Native.ObjectResolve(ObjectIndex, ObjectGeneration) != IntPtr.Zero;

    /// <summary>The live native pointer. Throws <see cref="InvalidOperationException"/> if the object has
    /// been destroyed; every generated accessor reads through here, so touching a dead reference fails
    /// loudly rather than corrupting memory. (An object the array doesn't track falls back to the raw
    /// pointer, preserving the old behavior.)</summary>
    internal IntPtr Handle
    {
        get
        {
            if (ObjectIndex < 0)
            {
                return RawHandle;
            }
            IntPtr Pointer = Native.ObjectResolve(ObjectIndex, ObjectGeneration);
            if (Pointer == IntPtr.Zero)
            {
                throw new InvalidOperationException(
                    "Use of a destroyed native object: the CObject this wrapper referenced has been freed. " +
                    "Don't cache wrappers across frames or structural changes, re-fetch it (Asset.Load, the property, ...).");
            }
            return Pointer;
        }
    }

    /// <summary>Throws <see cref="InvalidOperationException"/> if the object has been destroyed.</summary>
    public void ThrowIfInvalid()
    {
        _ = Handle;
    }
}

/// <summary>
/// Base for the generated opaque wrappers around native structs that are NOT blittable (they hold
/// FString/containers/smart-pointers, so they can't be mirrored by value). A wrapper is a VIEW onto a
/// component living in the ECS (returned by Registry.Get/Emplace/View); writes through it persist.
/// Engine-constructed only.
/// </summary>
public class NativeStruct
{
    internal IntPtr Handle;

    internal NativeStruct(IntPtr Handle)
    {
        this.Handle = Handle;
    }
}
