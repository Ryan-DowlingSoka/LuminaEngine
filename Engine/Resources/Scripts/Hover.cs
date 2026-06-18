using System;
using LuminaSharp;
using Lumina;

namespace Engine;

/// <summary>
/// Example engine script, bobs its entity up and down on a sine wave.
/// </summary>
public sealed class Hover : EntityScript
{
    [Property(Tooltip = "Peak vertical offset (world units).")]
    public float Amplitude = 0.5f;

    [Property(Tooltip = "Oscillations per second.")]
    public float Frequency = 1.0f;

    private float _Time;
    private FVector3 _Origin;

    public override void OnReady()
    {
        _Origin = Transform.GetLocalLocation();
    }

    public override void OnUpdate(float DeltaTime)
    {
        _Time += DeltaTime * new Random().Next(1, 2);
        float Offset = Amplitude * MathF.Sin(_Time * Frequency * MathF.Tau);
        Transform.SetLocalLocation(_Origin + new FVector3(0.0f, Offset, 0.0f));
    }
}
