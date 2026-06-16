using System.Runtime.InteropServices;

namespace Lumina;

/// <summary>
/// Payload for the collision/overlap callbacks (the C# mirror of the engine's SCollisionEvent, same
/// layout, blittable). Fields are self-oriented: <see cref="Entity"/>/<see cref="Velocity"/> are this
/// entity's, and <see cref="Normal"/> points away from self toward the other body. Layout order must
/// match the native struct exactly (validated in CSharpLayoutChecks.cpp).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public readonly struct SCollisionEvent
{
    // Raw entt ids (the native uint32 Entity/Other fields); surfaced as typed handles below.
    private readonly uint EntityId;
    private readonly uint OtherId;

    /// <summary>This body's Jolt body id.</summary>
    public readonly uint BodyID;

    /// <summary>The other body's Jolt body id.</summary>
    public readonly uint OtherBodyID;

    /// <summary>World-space contact point.</summary>
    public readonly FVector3 Point;

    /// <summary>Contact normal pointing away from self toward the other body.</summary>
    public readonly FVector3 Normal;

    /// <summary>This body's linear velocity at contact time (m/s).</summary>
    public readonly FVector3 Velocity;

    /// <summary>The other body's linear velocity at contact time (m/s).</summary>
    public readonly FVector3 OtherVelocity;

    /// <summary>Other - self linear velocity (m/s).</summary>
    public readonly FVector3 RelativeVelocity;

    /// <summary>|relative velocity along the normal| (m/s).</summary>
    public readonly float ImpactSpeed;

    // Native bIsTrigger (one byte); surfaced as IsTrigger below.
    private readonly byte TriggerByte;

    /// <summary>This script's entity.</summary>
    public LuminaSharp.Entity Entity => new(EntityId);

    /// <summary>The other body's entity.</summary>
    public LuminaSharp.Entity Other => new(OtherId);

    /// <summary>True if the OTHER side was a trigger/sensor.</summary>
    public bool IsTrigger => TriggerByte != 0;
}
