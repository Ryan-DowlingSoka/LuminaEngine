using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace LuminaSharp;

/// <summary>
/// Resolves and caches a component type's native op-table token once per type (T's wrapper class name
/// == the C++ CStruct name). Zero if T isn't a registered component; the native ops then no-op.
/// </summary>
internal static class ComponentOps<T> where T : NativeStruct
{
    public static readonly IntPtr Token = Native.FindComponentOps(typeof(T).Name);
}

/// <summary>
/// The component store for a world, the C# mirror of C++ <c>entt::registry</c> / <c>FEntityRegistry</c>.
/// <c>Get/TryGet/Has/Emplace/Remove&lt;T&gt;</c> call straight through each component's direct op table
/// (resolved once per type), with no entt::meta trampoline. The returned wrapper points at the live
/// component, so writes persist.
/// </summary>
public readonly struct EntityRegistry
{
    internal readonly ulong WorldHandle; // CWorld* the native helpers resolve the registry from

    internal EntityRegistry(ulong WorldHandle)
    {
        this.WorldHandle = WorldHandle;
    }

    public bool IsValid => WorldHandle != 0;

    /// <summary>The component of type T on the entity, or null if absent (mirrors registry.try_get).</summary>
    public T? TryGet<T>(Entity Entity) where T : NativeStruct
    {
        IntPtr Pointer = Native.GetComponent(WorldHandle, Entity.Id, ComponentOps<T>.Token);
        if (Pointer == IntPtr.Zero)
        {
            return null;
        }
        return Wrapper<T>.Create(Pointer);
    }

    /// <summary>The component of type T on the entity; throws if absent (mirrors registry.get).</summary>
    public T Get<T>(Entity Entity) where T : NativeStruct
    {
        return TryGet<T>(Entity) ?? throw new InvalidOperationException($"Entity {Entity.Id} has no {typeof(T).Name}.");
    }

    public bool Has<T>(Entity Entity) where T : NativeStruct
    {
        return Native.HasComponent(WorldHandle, Entity.Id, ComponentOps<T>.Token) != 0;
    }

    /// <summary>Get-or-emplace a default component and return the live wrapper to configure in place
    /// (null for a tag component). Idempotent; never clobbers an existing instance.</summary>
    public T? Emplace<T>(Entity Entity) where T : NativeStruct
    {
        IntPtr Pointer = Native.EmplaceComponent(WorldHandle, Entity.Id, ComponentOps<T>.Token);
        if (Pointer == IntPtr.Zero)
        {
            return null;
        }
        return Wrapper<T>.Create(Pointer);
    }

    public bool Remove<T>(Entity Entity) where T : NativeStruct
    {
        return Native.RemoveComponent(WorldHandle, Entity.Id, ComponentOps<T>.Token) != 0;
    }

    /// <summary>Alias of <see cref="Emplace{T}(Entity)"/> (s&amp;box wording).</summary>
    public T? Add<T>(Entity Entity) where T : NativeStruct => Emplace<T>(Entity);

    /// <summary>The component of type T, adding a default one first if absent.</summary>
    public T? GetOrAdd<T>(Entity Entity) where T : NativeStruct => TryGet<T>(Entity) ?? Emplace<T>(Entity);

    /// <summary>Get-or-emplace by a pre-resolved op-table token (zero on failure). For the [RequireComponent]
    /// injector, where the token is cached without a generic instantiation.</summary>
    internal IntPtr EmplaceRaw(Entity Entity, IntPtr Token)
    {
        return Token == IntPtr.Zero ? IntPtr.Zero : Native.EmplaceComponent(WorldHandle, Entity.Id, Token);
    }

    // Registry signals (the entt on_construct/on_destroy/on_update hooks). Subscribe to a component's
    // lifecycle and get the entity each time one fires; Dispose the returned subscription to unsubscribe.
    // Build your OWN events by declaring a component as a signal channel: Emplace it to raise OnConstruct,
    // Remove it to raise OnDestroy, and Patch it to pulse OnUpdate (carry data in the component's fields).

    /// <summary>Fires when a <typeparamref name="T"/> component is added to an entity.</summary>
    public RegistrySubscription OnConstruct<T>(Action<Entity> Callback) where T : NativeStruct
        => Subscribe(ComponentOps<T>.Token, 0, Callback);

    /// <summary>Fires when a <typeparamref name="T"/> component is removed from an entity (or its entity destroyed).</summary>
    public RegistrySubscription OnDestroy<T>(Action<Entity> Callback) where T : NativeStruct
        => Subscribe(ComponentOps<T>.Token, 1, Callback);

    /// <summary>Fires when a <typeparamref name="T"/> component is patched/replaced (see <see cref="Patch{T}"/>).</summary>
    public RegistrySubscription OnUpdate<T>(Action<Entity> Callback) where T : NativeStruct
        => Subscribe(ComponentOps<T>.Token, 2, Callback);

    /// <summary>Pulses the <c>on_update</c> signal for an entity's <typeparamref name="T"/> component, so
    /// <see cref="OnUpdate{T}"/> listeners run. No-op for a tag component or if the entity lacks T. Mutate
    /// the component first (via <see cref="Get{T}"/>), then Patch to notify.</summary>
    public void Patch<T>(Entity Entity) where T : NativeStruct
        => Native.RegistryPatch(WorldHandle, Entity.Id, ComponentOps<T>.Token);

    private RegistrySubscription Subscribe(IntPtr Token, int Kind, Action<Entity> Callback)
    {
        if (Token == IntPtr.Zero || Callback == null)
        {
            return RegistrySubscription.Empty;
        }

        GCHandle Handle = GCHandle.Alloc(Callback);
        IntPtr Listener = Native.RegistryConnect(WorldHandle, Token, Kind, SignalThunkPtr, GCHandle.ToIntPtr(Handle));
        if (Listener == IntPtr.Zero)
        {
            Handle.Free();
            return RegistrySubscription.Empty;
        }
        return new RegistrySubscription(WorldHandle, Token, Kind, Listener, Handle);
    }

    // Native signal trampoline: resolve the GCHandle context back to the managed callback and invoke it.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void SignalThunk(IntPtr Context, uint Entity)
    {
        try
        {
            if (GCHandle.FromIntPtr(Context).Target is Action<Entity> Body)
            {
                Body(new Entity(Entity));
            }
        }
        catch (Exception Exception)
        {
            Interop.LogException(Exception);
        }
    }

    private static readonly unsafe IntPtr SignalThunkPtr =
        (IntPtr)(delegate* unmanaged[Cdecl]<IntPtr, uint, void>)&SignalThunk;

    /// <summary>The C# script of type T on the entity, its concrete <see cref="EntityScript"/> subclass,
    /// or null if the entity has no script or it isn't a T. The returned instance is the LIVE managed
    /// object; call its public methods directly, exactly like a C++ component reference (no marshalling).
    /// Re-fetch per use rather than caching across frames: GetScript itself is always safe (it returns null
    /// once the script is gone), but a stored reference to a since-destroyed script's object stays in managed
    /// memory pointing at a logically-dead entity.</summary>
    public T? GetScript<T>(Entity Entity) where T : EntityScript
    {
        IntPtr Handle = Native.GetEntityScriptHandle(WorldHandle, Entity.Id);
        if (Handle == IntPtr.Zero)
        {
            return null;
        }
        return GCHandle.FromIntPtr(Handle).Target as T;
    }

    /// <summary>True if the entity has a C# script assignable to T.</summary>
    public bool HasScript<T>(Entity Entity) where T : EntityScript
    {
        return GetScript<T>(Entity) != null;
    }

    // entt-style typed views (mirrors registry.view<...>). Native iterates an entt::runtime_view and gathers
    // entities + live component pointers in CHUNKS (one boundary crossing per chunk); the View struct hands
    // back reused wrappers per element. Pass an Exclude<...>() filter as the optional second argument.
    // Arity 1..4 (extend by adding the next View<...> struct + overload; the native side is arity-agnostic).

    public View<T1> View<T1>(Exclude Filter = default)
        where T1 : NativeStruct
    {
        return new View<T1>(WorldHandle, ComponentOps<T1>.Token, Filter);
    }

    /// <summary>Shorthand for <see cref="View{T1}"/>: iterate every entity that has a T component.</summary>
    public View<T1> All<T1>(Exclude Filter = default) where T1 : NativeStruct => View<T1>(Filter);

    public View<T1, T2> View<T1, T2>(Exclude Filter = default)
        where T1 : NativeStruct
        where T2 : NativeStruct
    {
        return new View<T1, T2>(WorldHandle, ComponentOps<T1>.Token, ComponentOps<T2>.Token, Filter);
    }

    public View<T1, T2, T3> View<T1, T2, T3>(Exclude Filter = default)
        where T1 : NativeStruct
        where T2 : NativeStruct
        where T3 : NativeStruct
    {
        return new View<T1, T2, T3>(WorldHandle, ComponentOps<T1>.Token, ComponentOps<T2>.Token, ComponentOps<T3>.Token, Filter);
    }

    public View<T1, T2, T3, T4> View<T1, T2, T3, T4>(Exclude Filter = default)
        where T1 : NativeStruct
        where T2 : NativeStruct
        where T3 : NativeStruct
        where T4 : NativeStruct
    {
        return new View<T1, T2, T3, T4>(WorldHandle, ComponentOps<T1>.Token, ComponentOps<T2>.Token, ComponentOps<T3>.Token, ComponentOps<T4>.Token, Filter);
    }

    // Exclude-filter builders for a View (mirrors entt::exclude<...>). Resolve the component-ops tokens
    // (cached per type) for the excluded component types. Arity 1..3.

    public static Exclude Exclude<T1>()
        where T1 : NativeStruct
    {
        return new Exclude(ComponentOps<T1>.Token, IntPtr.Zero, IntPtr.Zero, 1);
    }

    public static Exclude Exclude<T1, T2>()
        where T1 : NativeStruct
        where T2 : NativeStruct
    {
        return new Exclude(ComponentOps<T1>.Token, ComponentOps<T2>.Token, IntPtr.Zero, 2);
    }

    public static Exclude Exclude<T1, T2, T3>()
        where T1 : NativeStruct
        where T2 : NativeStruct
        where T3 : NativeStruct
    {
        return new Exclude(ComponentOps<T1>.Token, ComponentOps<T2>.Token, ComponentOps<T3>.Token, 3);
    }
}
