using System;
using System.Collections.Generic;
using System.Threading;

namespace LuminaSharp;

/// <summary>How a listener's channel is matched against a broadcast tag.</summary>
public enum GameplayTagMatch
{
    /// <summary>Receive broadcasts on this exact tag AND any descendant (e.g. a listener on
    /// <c>"Damage"</c> hears <c>"Damage.Fire"</c>). The default.</summary>
    Partial,

    /// <summary>Receive only broadcasts on this exact tag.</summary>
    Exact,
}

/// <summary>
/// A world's generic gameplay event bus (<c>World.Messages</c>): send/dispatch/listen for typed messages on
/// hierarchical <see cref="GameplayTag"/> channels. Type-safe (each listener declares its payload type) and
/// hierarchical (a broadcast on <c>"Combat.Damage.Fire"</c> reaches Partial listeners on <c>"Combat.Damage"</c>
/// and <c>"Combat"</c>). Per-world, so PIE / multiple worlds stay isolated. Game thread only.
///
/// Two delivery models share one channel space:
/// <list type="bullet">
/// <item><see cref="Broadcast{T}(GameplayTag, T)"/> reaches every listener on the channel, anywhere in the world.</item>
/// <item><see cref="SendUp{T}"/> / <see cref="SendDown{T}"/> deliver only along a source entity's scene-graph
/// chain (ancestors / descendants). Listeners opt into directional delivery with the entity-scoped
/// <see cref="Subscribe{T}(Entity, GameplayTag, Action{T}, GameplayTagMatch)"/> overload.</item>
/// </list>
/// </summary>
public readonly unsafe partial struct GameplayMessageBus
{
    private readonly ulong World;

    internal GameplayMessageBus(ulong World)
    {
        this.World = World;
    }

    /// <summary>
    /// Listen for <typeparamref name="T"/> messages on <paramref name="Channel"/>. <paramref name="Match"/>
    /// controls whether descendant-channel broadcasts are also delivered. Dispose the returned handle to stop
    /// listening (e.g. in <c>EntityScript.OnDetach</c>).
    /// </summary>
    public IDisposable Subscribe<T>(GameplayTag Channel, Action<T> Handler, GameplayTagMatch Match = GameplayTagMatch.Partial)
    {
        if (!Channel.IsValid)
        {
            return GameplayMessageSubscription.Empty;
        }
        Listener Entry = new(typeof(T), Message => Handler((T)Message), Match);
        BusRegistry.Of(World).Add(Channel.Id, Entry);
        return new GameplayMessageSubscription(World, Channel.Id, Entry);
    }

    /// <summary>Subscribe by tag name (interns the channel first).</summary>
    public IDisposable Subscribe<T>(string Channel, Action<T> Handler, GameplayTagMatch Match = GameplayTagMatch.Partial)
        => Subscribe(GameplayTag.Request(Channel), Handler, Match);

    /// <summary>
    /// Broadcast <paramref name="Message"/> on <paramref name="Channel"/>. Delivered to exact listeners on
    /// the channel and to Partial listeners on the channel and every ancestor, whose payload type is
    /// assignable from <typeparamref name="T"/>.
    /// </summary>
    public void Broadcast<T>(GameplayTag Channel, T Message)
    {
        if (!Channel.IsValid)
        {
            return;
        }
        BusState? State = BusRegistry.Find(World);
        if (State == null)
        {
            return;
        }

        object Boxed = Message!;
        Type MessageType = typeof(T);
        for (GameplayTag Cur = Channel; Cur.IsValid; Cur = Cur.Parent)
        {
            State.Dispatch(Cur.Id, Cur.Id == Channel.Id, MessageType, Boxed);
        }
    }

    /// <summary>Broadcast by tag name (interns the channel first).</summary>
    public void Broadcast<T>(string Channel, T Message) => Broadcast(GameplayTag.Request(Channel), Message);
    
    /// <summary>
    /// Listen for <typeparamref name="T"/> messages delivered to <paramref name="Owner"/> via
    /// <see cref="SendUp{T}"/> / <see cref="SendDown{T}"/>. Unlike the global <see cref="Subscribe{T}(GameplayTag, Action{T}, GameplayTagMatch)"/>,
    /// this listener fires only when a directional send reaches <paramref name="Owner"/>'s entity along the
    /// scene graph. <paramref name="Match"/> applies the same hierarchical channel rule as the global overload.
    /// Dispose the handle to stop listening (e.g. in <c>EntityScript.OnDetach</c>).
    /// </summary>
    public IDisposable Subscribe<T>(Entity Owner, GameplayTag Channel, Action<T> Handler, GameplayTagMatch Match = GameplayTagMatch.Partial)
    {
        if (Owner.IsNull || !Channel.IsValid)
        {
            return GameplayMessageSubscription.Empty;
        }
        EntityListener Entry = new(typeof(T), Message => Handler((T)Message), Channel.Id, Match);
        BusRegistry.Of(World).AddEntity(Owner.Id, Entry);
        return new GameplayMessageSubscription(World, Owner.Id, Entry);
    }

    /// <summary>Entity-scoped subscribe by tag name (interns the channel first).</summary>
    public IDisposable Subscribe<T>(Entity Owner, string Channel, Action<T> Handler, GameplayTagMatch Match = GameplayTagMatch.Partial)
        => Subscribe(Owner, GameplayTag.Request(Channel), Handler, Match);

    /// <summary>
    /// Send <paramref name="Message"/> on <paramref name="Channel"/> UP the scene graph: to <paramref name="Source"/>
    /// (when <paramref name="IncludeSelf"/>) then each ancestor up to the root. Only entity-scoped listeners
    /// (see the <see cref="Subscribe{T}(Entity, GameplayTag, Action{T}, GameplayTagMatch)"/> overload) on those
    /// entities receive it, filtered by channel and payload type.
    /// </summary>
    public void SendUp<T>(Entity Source, GameplayTag Channel, T Message, bool IncludeSelf = true)
    {
        if (Source.IsNull || !Channel.IsValid)
        {
            return;
        }
        BusState? State = BusRegistry.Find(World);
        if (State == null)
        {
            return;
        }

        object Boxed = Message!;
        Type MessageType = typeof(T);
        Entity Cur = IncludeSelf ? Source : new Entity(GetParentRaw(World, Source.Id));
        while (!Cur.IsNull)
        {
            State.DispatchEntity(Cur.Id, Channel, MessageType, Boxed);
            Cur = new Entity(GetParentRaw(World, Cur.Id));
        }
    }

    /// <summary>Send up the scene graph by tag name (interns the channel first).</summary>
    public void SendUp<T>(Entity Source, string Channel, T Message, bool IncludeSelf = true)
        => SendUp(Source, GameplayTag.Request(Channel), Message, IncludeSelf);

    /// <summary>
    /// Send <paramref name="Message"/> on <paramref name="Channel"/> DOWN the scene graph: to <paramref name="Source"/>
    /// (when <paramref name="IncludeSelf"/>) then every descendant (depth-first). Only entity-scoped listeners
    /// (see the <see cref="Subscribe{T}(Entity, GameplayTag, Action{T}, GameplayTagMatch)"/> overload) on those
    /// entities receive it, filtered by channel and payload type.
    /// </summary>
    public void SendDown<T>(Entity Source, GameplayTag Channel, T Message, bool IncludeSelf = true)
    {
        if (Source.IsNull || !Channel.IsValid)
        {
            return;
        }
        BusState? State = BusRegistry.Find(World);
        if (State == null)
        {
            return;
        }

        object Boxed = Message!;
        Type MessageType = typeof(T);
        if (IncludeSelf)
        {
            State.DispatchEntity(Source.Id, Channel, MessageType, Boxed);
        }
        DispatchDescendants(State, Source.Id, Channel, MessageType, Boxed);
    }

    /// <summary>Send down the scene graph by tag name (interns the channel first).</summary>
    public void SendDown<T>(Entity Source, string Channel, T Message, bool IncludeSelf = true)
        => SendDown(Source, GameplayTag.Request(Channel), Message, IncludeSelf);

    // Depth-first walk of an entity's children via the relationship first-child / next-sibling links.
    private void DispatchDescendants(BusState State, uint Parent, GameplayTag Channel, Type MessageType, object Boxed)
    {
        for (Entity Child = new(GetFirstChildRaw(World, Parent)); !Child.IsNull; Child = new Entity(GetNextSiblingRaw(World, Child.Id)))
        {
            State.DispatchEntity(Child.Id, Channel, MessageType, Boxed);
            DispatchDescendants(State, Child.Id, Channel, MessageType, Boxed);
        }
    }

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_World_GetParentEntity")]
    private static partial uint GetParentRaw(ulong World, uint Entity);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_World_GetFirstChildEntity")]
    private static partial uint GetFirstChildRaw(ulong World, uint Entity);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_World_GetNextSiblingEntity")]
    private static partial uint GetNextSiblingRaw(ulong World, uint Entity);
}

/// <summary>One registered listener: its payload type, the boxed-invoke shim, and its match mode.</summary>
internal sealed class Listener
{
    public readonly Type MessageType;
    public readonly Action<object> Invoke;
    public readonly GameplayTagMatch Match;

    public Listener(Type MessageType, Action<object> Invoke, GameplayTagMatch Match)
    {
        this.MessageType = MessageType;
        this.Invoke = Invoke;
        this.Match = Match;
    }
}

/// <summary>An entity-scoped listener for directional (SendUp/SendDown) delivery: like <see cref="Listener"/>
/// but it also carries the channel tag, since directional sends index by entity, not by channel.</summary>
internal sealed class EntityListener
{
    public readonly Type MessageType;
    public readonly Action<object> Invoke;
    public readonly uint ChannelId;
    public readonly GameplayTagMatch Match;

    public EntityListener(Type MessageType, Action<object> Invoke, uint ChannelId, GameplayTagMatch Match)
    {
        this.MessageType = MessageType;
        this.Invoke = Invoke;
        this.ChannelId = ChannelId;
        this.Match = Match;
    }

    /// <summary>True if a directional send on <paramref name="Sent"/> reaches this listener's channel, applying
    /// the same hierarchical rule as a Broadcast: Exact wants the same tag; Partial also accepts a descendant.</summary>
    public bool Reaches(GameplayTag Sent)
    {
        if (Match == GameplayTagMatch.Exact)
        {
            return Sent.Id == ChannelId;
        }
        for (GameplayTag Cur = Sent; Cur.IsValid; Cur = Cur.Parent)
        {
            if (Cur.Id == ChannelId)
            {
                return true;
            }
        }
        return false;
    }
}

/// <summary>Per-world listener table, keyed by tag id. Self-locking; safe to (un)subscribe during dispatch.</summary>
internal sealed class BusState
{
    private readonly Lock Gate = new();
    private readonly Dictionary<uint, List<Listener>> ByTag = new();
    private readonly Dictionary<uint, List<EntityListener>> ByEntity = new();

    public void Add(uint TagId, Listener Entry)
    {
        lock (Gate)
        {
            if (!ByTag.TryGetValue(TagId, out List<Listener>? List))
            {
                List = new List<Listener>();
                ByTag[TagId] = List;
            }
            List.Add(Entry);
        }
    }

    public void Remove(uint TagId, Listener Entry)
    {
        lock (Gate)
        {
            if (ByTag.TryGetValue(TagId, out List<Listener>? List))
            {
                List.Remove(Entry);
            }
        }
    }

    public void Dispatch(uint TagId, bool IsChannelLevel, Type MessageType, object Boxed)
    {
        Listener[] Snapshot;
        lock (Gate)
        {
            if (!ByTag.TryGetValue(TagId, out List<Listener>? List) || List.Count == 0)
            {
                return;
            }
            Snapshot = List.ToArray(); // snapshot so a handler may (un)subscribe mid-dispatch
        }

        foreach (Listener Entry in Snapshot)
        {
            bool Reaches = Entry.Match == GameplayTagMatch.Partial || IsChannelLevel;
            if (Reaches && Entry.MessageType.IsAssignableFrom(MessageType))
            {
                Entry.Invoke(Boxed);
            }
        }
    }

    public void AddEntity(uint EntityId, EntityListener Entry)
    {
        lock (Gate)
        {
            if (!ByEntity.TryGetValue(EntityId, out List<EntityListener>? List))
            {
                List = new List<EntityListener>();
                ByEntity[EntityId] = List;
            }
            List.Add(Entry);
        }
    }

    public void RemoveEntity(uint EntityId, EntityListener Entry)
    {
        lock (Gate)
        {
            if (ByEntity.TryGetValue(EntityId, out List<EntityListener>? List))
            {
                List.Remove(Entry);
            }
        }
    }

    /// <summary>Deliver a directional send that has reached <paramref name="EntityId"/> to that entity's
    /// listeners whose channel and payload type match.</summary>
    public void DispatchEntity(uint EntityId, GameplayTag Channel, Type MessageType, object Boxed)
    {
        EntityListener[] Snapshot;
        lock (Gate)
        {
            if (!ByEntity.TryGetValue(EntityId, out List<EntityListener>? List) || List.Count == 0)
            {
                return;
            }
            Snapshot = List.ToArray(); // snapshot so a handler may (un)subscribe mid-dispatch
        }

        foreach (EntityListener Entry in Snapshot)
        {
            if (Entry.Reaches(Channel) && Entry.MessageType.IsAssignableFrom(MessageType))
            {
                Entry.Invoke(Boxed);
            }
        }
    }
}

/// <summary>Holds one <see cref="BusState"/> per world handle (world-isolated event buses).</summary>
internal static class BusRegistry
{
    private static readonly Lock Gate = new();
    private static readonly Dictionary<ulong, BusState> Worlds = new();

    public static BusState Of(ulong World)
    {
        lock (Gate)
        {
            if (!Worlds.TryGetValue(World, out BusState? State))
            {
                State = new BusState();
                Worlds[World] = State;
            }
            return State;
        }
    }

    public static BusState? Find(ulong World)
    {
        lock (Gate)
        {
            return Worlds.TryGetValue(World, out BusState? State) ? State : null;
        }
    }
}

/// <summary>A live message-bus subscription; dispose to stop listening.</summary>
public sealed class GameplayMessageSubscription : IDisposable
{
    internal static readonly GameplayMessageSubscription Empty = new();

    private readonly ulong World;
    private readonly uint Key; // tag id for a global listener, entity id for an entity-scoped one
    private Listener? Entry;
    private EntityListener? EntityEntry;

    private GameplayMessageSubscription()
    {
    }

    internal GameplayMessageSubscription(ulong World, uint TagId, Listener Entry)
    {
        this.World = World;
        this.Key = TagId;
        this.Entry = Entry;
    }

    internal GameplayMessageSubscription(ulong World, uint EntityId, EntityListener Entry)
    {
        this.World = World;
        this.Key = EntityId;
        this.EntityEntry = Entry;
    }

    public bool IsActive => Entry != null || EntityEntry != null;

    public void Dispose()
    {
        if (Entry != null)
        {
            BusRegistry.Find(World)?.Remove(Key, Entry);
            Entry = null;
        }
        if (EntityEntry != null)
        {
            BusRegistry.Find(World)?.RemoveEntity(Key, EntityEntry);
            EntityEntry = null;
        }
    }
}
