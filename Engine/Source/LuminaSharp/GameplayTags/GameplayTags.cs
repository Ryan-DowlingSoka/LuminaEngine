using System;
using Lumina;

namespace LuminaSharp;

/// <summary>
/// A world's per-entity gameplay-tag interface (<c>World.Tags</c>). Add, remove, and query hierarchical
/// <see cref="GameplayTag"/>s on entities at runtime; tags are stored on an SGameplayTagComponent (created
/// on first Add) that designers can also seed in the editor. Queries are hierarchical,  an entity tagged
/// <c>"Status.Burning"</c> satisfies <c>Has(e, "Status")</c>. Game thread only.
/// </summary>
public readonly partial struct GameplayTags
{
    internal readonly ulong Handle;

    internal GameplayTags(ulong Handle)
    {
        this.Handle = Handle;
    }

    /// <summary>Largest tag count <see cref="Get"/> returns in one call.</summary>
    public const int MaxTags = 64;

    /// <summary>Adds <paramref name="Tag"/> to <paramref name="Entity"/> (creating the tag component if needed). Idempotent.</summary>
    public void Add(Entity Entity, GameplayTag Tag)
    {
        if (Tag.IsValid)
        {
            AddRaw(Entity.Id, Tag.Id);
        }
    }

    /// <summary>Removes <paramref name="Tag"/> from <paramref name="Entity"/> (exact match). No-op if absent.</summary>
    public void Remove(Entity Entity, GameplayTag Tag)
    {
        if (Tag.IsValid)
        {
            RemoveRaw(Entity.Id, Tag.Id);
        }
    }

    /// <summary>Hierarchical: true if the entity has <paramref name="Tag"/> or any descendant of it.</summary>
    public bool Has(Entity Entity, GameplayTag Tag) => Tag.IsValid && HasRaw(Entity.Id, Tag.Id) != 0;

    /// <summary>Exact: true only if the entity has this precise tag (not a descendant).</summary>
    public bool HasExact(Entity Entity, GameplayTag Tag) => Tag.IsValid && HasExactRaw(Entity.Id, Tag.Id) != 0;

    /// <summary>Removes every tag from the entity.</summary>
    public void Clear(Entity Entity) => ClearRaw(Entity.Id);

    /// <summary>The entity's current tags (empty if it has none).</summary>
    public GameplayTag[] Get(Entity Entity)
    {
        Span<uint> Buffer = stackalloc uint[MaxTags];
        int Count = GetRaw(Entity.Id, Buffer);
        GameplayTag[] Out = new GameplayTag[Count];
        for (int i = 0; i < Count; ++i)
        {
            Out[i] = new GameplayTag(Buffer[i]);
        }
        return Out;
    }

    // String convenience overloads (intern the tag first).
    public void Add(Entity Entity, string Tag) => Add(Entity, GameplayTag.Request(Tag));
    public void Remove(Entity Entity, string Tag) => Remove(Entity, GameplayTag.Request(Tag));
    public bool Has(Entity Entity, string Tag) => Has(Entity, GameplayTag.Request(Tag));
    public bool HasExact(Entity Entity, string Tag) => HasExact(Entity, GameplayTag.Request(Tag));

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_GameplayTags_Add")]
    private partial void AddRaw(uint Entity, uint TagId);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_GameplayTags_Remove")]
    private partial void RemoveRaw(uint Entity, uint TagId);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_GameplayTags_Has")]
    private partial int HasRaw(uint Entity, uint TagId);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_GameplayTags_HasExact")]
    private partial int HasExactRaw(uint Entity, uint TagId);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_GameplayTags_Clear")]
    private partial void ClearRaw(uint Entity);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_GameplayTags_Get")]
    private partial int GetRaw(uint Entity, Span<uint> OutIds);
}
