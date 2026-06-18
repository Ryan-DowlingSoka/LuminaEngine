using System;
using System.Collections.Generic;

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
/// </summary>
public readonly struct GameplayMessageBus
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
        if (!Channel.IsValid || Handler == null)
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
        // Walk channel -> root. Exact listeners fire only at the channel level; Partial at every ancestor
        // (those tags are all ancestors-or-equal of the channel, so the channel is "under" them).
        for (GameplayTag Cur = Channel; Cur.IsValid; Cur = Cur.Parent)
        {
            State.Dispatch(Cur.Id, Cur.Id == Channel.Id, MessageType, Boxed);
        }
    }

    /// <summary>Broadcast by tag name (interns the channel first).</summary>
    public void Broadcast<T>(string Channel, T Message) => Broadcast(GameplayTag.Request(Channel), Message);
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

/// <summary>Per-world listener table, keyed by tag id. Self-locking; safe to (un)subscribe during dispatch.</summary>
internal sealed class BusState
{
    private readonly object Gate = new();
    private readonly Dictionary<uint, List<Listener>> ByTag = new();

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
}

/// <summary>Holds one <see cref="BusState"/> per world handle (world-isolated event buses).</summary>
internal static class BusRegistry
{
    private static readonly object Gate = new();
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
    private readonly uint TagId;
    private Listener? Entry;

    private GameplayMessageSubscription()
    {
    }

    internal GameplayMessageSubscription(ulong World, uint TagId, Listener Entry)
    {
        this.World = World;
        this.TagId = TagId;
        this.Entry = Entry;
    }

    public bool IsActive => Entry != null;

    public void Dispose()
    {
        if (Entry != null)
        {
            BusRegistry.Find(World)?.Remove(TagId, Entry);
            Entry = null;
        }
    }
}
