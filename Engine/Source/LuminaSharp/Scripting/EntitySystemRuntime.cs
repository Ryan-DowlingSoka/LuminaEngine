using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using Lumina;

namespace LuminaSharp;

/// <summary>
/// Owns the live <see cref="EntitySystem"/> instances for one loaded generation. The link to native is a
/// <see cref="GCHandle"/> stored as the <c>Self</c> of a native FStageSlot, so there is no per-handle
/// lookup table; the only collection kept is a flat set of live handles, freed before the ALC unloads
/// (a strong GCHandle pins its target). The reflection registry lives in the <see cref="TypeLibrary"/>.
///
/// Lifecycle mirrors the native CWorld managed-system store: native gathers descriptors, creates one
/// instance per descriptor per world, ticks each via the shared shim, and destroys on world teardown
/// (or drops stale handles after a hot reload without destroying, since the ALC already freed them).
/// </summary>
internal sealed class EntitySystemRuntime
{
    private readonly TypeLibrary Library;
    private readonly HashSet<GCHandle> LiveHandles = new();

    public EntitySystemRuntime(TypeLibrary Library)
    {
        this.Library = Library;
    }

    /// <summary>Number of discovered [EntitySystem] types in this generation (for editor diagnostics).</summary>
    public int TypeCount => Library.EntitySystemTypes.Count;

    /// <summary>Reports every discovered EntitySystem to a native sink as (full name, stage, priority).
    /// sink(ctx, utf8Name, byteLen, stage, priority) is called once per type.</summary>
    public unsafe void Enumerate(IntPtr Sink, IntPtr Context)
    {
        if (Sink == IntPtr.Zero)
        {
            return;
        }

        var Add = (delegate* unmanaged[Stdcall]<IntPtr, byte*, int, int, int, void>)Sink;
        Span<byte> Scratch = stackalloc byte[256];
        foreach (Type Type in Library.EntitySystemTypes)
        {
            EntitySystemAttribute? Meta = Type.GetCustomAttribute<EntitySystemAttribute>();
            if (Meta == null || Type.FullName is not { } FullName)
            {
                continue;
            }

            Interop.FInteropString Encoded = new(FullName, Scratch);
            try
            {
                Add(Context, Encoded.Pointer, Encoded.Length, (int)Meta.Stage, Meta.Priority);
            }
            finally
            {
                Encoded.Free();
            }
        }
    }

    /// <summary>Instantiates the named EntitySystem for a world; returns a strong GCHandle (as IntPtr)
    /// the native FStageSlot stores as Self, or IntPtr.Zero on failure.</summary>
    public IntPtr Create(string TypeName, ulong World)
    {
        Type? Type = Library.GetEntitySystem(TypeName);
        if (Type == null)
        {
            Native.Log(ELogLevel.Warn, $"EntitySystem type not found: '{TypeName}'.");
            return IntPtr.Zero;
        }

        if (Activator.CreateInstance(Type) is not EntitySystem System)
        {
            Native.Log(ELogLevel.Error, $"Failed to create EntitySystem '{TypeName}'.");
            return IntPtr.Zero;
        }

        System.World = new Lumina.CWorld(new IntPtr(unchecked((long)World)));

        GCHandle Handle = GCHandle.Alloc(System);
        LiveHandles.Add(Handle);
        return GCHandle.ToIntPtr(Handle);
    }

    public unsafe void Tick(IntPtr Handle, IntPtr Context)
    {
        if (Resolve(Handle) is not EntitySystem System)
        {
            return;
        }

        bool Profiling = Profiler.Enabled;
        try
        {
            if (Profiling)
            {
                Profiler.Begin(System.GetType().Name);
            }
            try
            {
                System.OnUpdate(new SystemContext(Context));
            }
            finally
            {
                if (Profiling)
                {
                    Profiler.End();
                }
            }
        }
        catch (Exception Exception)
        {
            Native.Log(ELogLevel.Error, $"EntitySystem.OnUpdate threw: {Exception}");
        }
    }

    public void Destroy(IntPtr Pointer)
    {
        if (Pointer == IntPtr.Zero)
        {
            return;
        }

        GCHandle Handle = GCHandle.FromIntPtr(Pointer);
        if (Handle.Target is EntitySystem System)
        {
            try
            {
                System.OnTeardown(default);
            }
            catch (Exception Exception)
            {
                Native.Log(ELogLevel.Error, $"EntitySystem.OnTeardown threw: {Exception}");
            }
        }

        LiveHandles.Remove(Handle);
        if (Handle.IsAllocated)
        {
            Handle.Free();
        }
    }

    /// <summary>Frees every live handle (running OnTeardown) so the collectible ALC can unload.</summary>
    public void FreeAll()
    {
        foreach (GCHandle Handle in LiveHandles)
        {
            if (Handle.Target is EntitySystem System)
            {
                try
                {
                    System.OnTeardown(default);
                }
                catch (Exception Exception)
                {
                    Native.Log(ELogLevel.Error, $"EntitySystem.OnTeardown threw during unload: {Exception}");
                }
            }
            if (Handle.IsAllocated)
            {
                Handle.Free();
            }
        }
        LiveHandles.Clear();
    }

    private static EntitySystem? Resolve(IntPtr Pointer)
    {
        if (Pointer == IntPtr.Zero)
        {
            return null;
        }
        return GCHandle.FromIntPtr(Pointer).Target as EntitySystem;
    }
}
