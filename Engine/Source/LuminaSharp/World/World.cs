using System;
using LuminaSharp;

namespace Lumina;

/// <summary>
/// Hand-written extensions to the REFLECTED <see cref="CWorld"/> wrapper.
/// </summary>
public unsafe partial class CWorld
{
    private ulong WorldHandle => (ulong)Handle.ToInt64();
    
    public EntityRegistry Registry => new(WorldHandle);
    public Physics Physics => new(WorldHandle);
    public Camera Camera => new(WorldHandle);
    public DebugDraw Draw => new(WorldHandle);
    public Net Net => new(WorldHandle);
    public UI UI => new(WorldHandle);
    public Navigation Navigation => new(WorldHandle);
    public Perception Perception => new(WorldHandle);
    public GameplayMessageBus Messages => new(WorldHandle);
    public GameplayTags Tags => new(WorldHandle);
    public Audio Audio => new(WorldHandle);
    public Timers Timers => new(WorldHandle);
    public LuminaSharp.Animation Animation => new(WorldHandle);

    public float DeltaTime => (float)GetWorldDeltaTime();
    public double ElapsedTime => GetTimeSinceWorldCreation();

    /// <summary>
    /// Spawns the prefab at <paramref name="Path"/> and places its root at <paramref name="Location"/>
    /// (optionally rotated, optionally parented) in one call -- composes the generated
    /// <see cref="SpawnPrefab(string)"/> with SetEntityLocation/SetEntityRotation/SetParent. Returns the
    /// spawned root entity, or <see cref="Entity.Null"/> if the prefab couldn't be spawned.
    /// </summary>
    public Entity SpawnPrefab(string Path, FVector3 Location, FQuat? Rotation = null, Entity? Parent = null)
    {
        Entity Spawned = SpawnPrefab(Path);
        if (Spawned.IsNull)
        {
            return Spawned;
        }
        SetEntityLocation(Spawned, Location);
        if (Rotation.HasValue)
        {
            SetEntityRotation(Spawned, Rotation.Value);
        }
        if (Parent.HasValue)
        {
            SetParent(Spawned, Parent.Value);
        }
        return Spawned;
    }

    /// <summary>Spawn the prefab at <paramref name="Path"/> (s&amp;box-style alias of <see cref="SpawnPrefab(string)"/>).</summary>
    public Entity Spawn(string Path) => SpawnPrefab(Path);

    /// <summary>Spawn and place the prefab at <paramref name="Path"/>.</summary>
    public Entity Spawn(string Path, FVector3 Location, FQuat? Rotation = null, Entity? Parent = null)
        => SpawnPrefab(Path, Location, Rotation, Parent);
}
