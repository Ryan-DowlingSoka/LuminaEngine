using LuminaSharp;
using Lumina;

namespace Engine;

/// <summary>
/// Example engine script, spins its entity around the up axis every frame.
/// </summary>
public sealed class Rotator : EntityScript
{
    [Property(Tooltip = "Yaw speed in degrees per second.")]
    public float DegreesPerSecond = 90.0f;

    public override void OnUpdate(float DeltaTime)
    {
        Registry.Get<STransformComponent>(Entity).AddYaw(DegreesPerSecond * DeltaTime);
    }
}
