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
    public DebugDraw Draw => new(WorldHandle);
    public Net Net => new(WorldHandle);
    
    public float DeltaTime => (float)GetWorldDeltaTime();
    public double ElapsedTime => GetTimeSinceWorldCreation();
}
