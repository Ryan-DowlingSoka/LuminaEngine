using System;
using System.Runtime.InteropServices;

namespace LuminaSharp;

/// <summary>
/// A live subscription to a registry signal, returned by <see cref="EntityRegistry.OnConstruct{T}"/> /
/// <see cref="EntityRegistry.OnDestroy{T}"/> / <see cref="EntityRegistry.OnUpdate{T}"/>. Dispose it to
/// unsubscribe and release the callback. Subscriptions are world-scoped: dispose them before the world is
/// destroyed (e.g. in <c>EntityScript.OnDetach</c>); a subscription whose world is already gone leaks its
/// callback rather than firing into freed memory.
/// </summary>
public sealed class RegistrySubscription : IDisposable
{
    /// <summary>An inert subscription (returned when the component type is unknown or the connect failed).</summary>
    internal static readonly RegistrySubscription Empty = new();

    private readonly ulong WorldHandle;
    private readonly IntPtr Token;
    private readonly int Kind;
    private IntPtr Listener;
    private GCHandle Handle;

    private RegistrySubscription()
    {
    }

    internal RegistrySubscription(ulong WorldHandle, IntPtr Token, int Kind, IntPtr Listener, GCHandle Handle)
    {
        this.WorldHandle = WorldHandle;
        this.Token = Token;
        this.Kind = Kind;
        this.Listener = Listener;
        this.Handle = Handle;
    }

    /// <summary>True while connected; false for an inert/failed subscription or after <see cref="Dispose"/>.</summary>
    public bool IsActive => Listener != IntPtr.Zero;

    public void Dispose()
    {
        if (Listener != IntPtr.Zero)
        {
            Native.RegistryDisconnect(WorldHandle, Token, Kind, Listener);
            Listener = IntPtr.Zero;
        }
        if (Handle.IsAllocated)
        {
            Handle.Free();
        }
    }
}
