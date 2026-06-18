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
        Transform.AddYaw(DegreesPerSecond * DeltaTime);
    }

    [Button(Tooltip = "Flip the spin direction.")]
    public void ReverseDirection()
    {
        DegreesPerSecond = -DegreesPerSecond;
    }

    [Button("Snap +90°")]
    public void SnapQuarterTurn()
    {
        Transform.AddYaw(90.0f);
    }
}
