using System;
using System.Runtime.InteropServices;

namespace LuminaSharp;

/// <summary>
/// A live UI event listener, returned by <see cref="UIElement.On"/> / <see cref="UIElement.OnClick(System.Action{UIEvent})"/>.
/// Dispose it to unsubscribe and release the callback. World-scoped: dispose before the world is destroyed
/// (e.g. in <see cref="EntityScript.OnDetach"/>). A subscription whose element/world is already gone is inert;
/// leaving it undisposed leaks only the managed callback (mirrors <see cref="RegistrySubscription"/>).
/// </summary>
public sealed class UIEventSubscription : IDisposable
{
    /// <summary>An inert subscription (returned when the element was invalid or the connect failed).</summary>
    internal static readonly UIEventSubscription Empty = new();

    private readonly ulong World;
    private IntPtr Listener;
    private GCHandle Handle;

    private UIEventSubscription()
    {
    }

    internal UIEventSubscription(ulong World, IntPtr Listener, GCHandle Handle)
    {
        this.World = World;
        this.Listener = Listener;
        this.Handle = Handle;
    }

    /// <summary>True while connected; false for an inert subscription or after <see cref="Dispose"/>.</summary>
    public bool IsActive => Listener != IntPtr.Zero;

    public void Dispose()
    {
        if (Listener != IntPtr.Zero)
        {
            Native.UI_RemoveEventListener(World, Listener);
            Listener = IntPtr.Zero;
        }
        if (Handle.IsAllocated)
        {
            Handle.Free();
        }
    }
}
