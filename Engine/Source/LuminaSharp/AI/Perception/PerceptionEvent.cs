using System.Runtime.InteropServices;

namespace Lumina;

/// <summary>
/// Payload for the AI perception callbacks (the C# mirror of the engine's SPerceptionEvent, same layout,
/// blittable). Self-oriented: <see cref="Perceiver"/> is this entity, <see cref="Target"/> is the
/// sensed/lost entity. Layout order must match the native struct exactly (validated in CSharpLayoutChecks.cpp).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public readonly struct SPerceptionEvent
{
    // Raw entt ids (the native uint32 Perceiver/Target fields); surfaced as typed handles below.
    private readonly uint PerceiverId;
    private readonly uint TargetId;

    /// <summary>Last known / stimulus world location of the target.</summary>
    public readonly FVector3 Location;

    // Native uint32 Sense (EAISenseChannel bit); surfaced as the typed flags below.
    private readonly uint SenseRaw;

    /// <summary>Damage amount / noise loudness for hearing+damage; 0 for sight.</summary>
    public readonly float Strength;

    /// <summary>This perceiving entity.</summary>
    public LuminaSharp.Entity Perceiver => new(PerceiverId);

    /// <summary>The perceived (or lost) entity.</summary>
    public LuminaSharp.Entity Target => new(TargetId);

    /// <summary>The sense channel that triggered this event.</summary>
    public EAISenseChannel Sense => (EAISenseChannel)SenseRaw;
}
