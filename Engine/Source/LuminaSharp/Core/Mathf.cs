using System;

namespace Lumina;

/// <summary>Scalar math helpers and engine constants that complement <see cref="System.MathF"/>.
/// Mirrors the gap-fillers from C++ Core/Math (Scalar.h): clamping, interpolation, angle helpers,
/// and the engine tolerance constants. Trig/exp/sqrt stay on <see cref="System.MathF"/>.</summary>
public static class Mathf
{
    public const float Pi = 3.14159265358979323846f;
    public const float TwoPi = 2.0f * Pi;
    public const float HalfPi = 0.5f * Pi;
    public const float InvPi = 1.0f / Pi;

    public const float Deg2Rad = Pi / 180.0f;
    public const float Rad2Deg = 180.0f / Pi;

    /// <summary>Machine epsilon for float (matches C++ Math::Epsilon&lt;float&gt;).</summary>
    public const float Epsilon = 1.1920929e-07f;

    /// <summary>LE_SMALL_NUMBER.</summary>
    public const float SmallNumber = 1e-8f;

    /// <summary>LE_KINDA_SMALL_NUMBER.</summary>
    public const float KindaSmallNumber = 1e-4f;

    public static float Radians(float Degrees) => Degrees * Deg2Rad;
    public static float Degrees(float Radians) => Radians * Rad2Deg;

    public static float Clamp(float V, float Lo, float Hi) => V < Lo ? Lo : (V > Hi ? Hi : V);
    public static float Clamp01(float V) => V < 0.0f ? 0.0f : (V > 1.0f ? 1.0f : V);

    /// <summary>Alias for <see cref="Clamp01"/> (GLSL/HLSL naming).</summary>
    public static float Saturate(float V) => Clamp01(V);

    public static float Lerp(float A, float B, float T) => A + (B - A) * T;
    public static float LerpClamped(float A, float B, float T) => A + (B - A) * Clamp01(T);

    /// <summary>Inverse of <see cref="Lerp"/>: the t that produces <paramref name="Value"/> between A and B.</summary>
    public static float InverseLerp(float A, float B, float Value)
    {
        float Denom = B - A;
        return MathF.Abs(Denom) < SmallNumber ? 0.0f : Clamp01((Value - A) / Denom);
    }

    /// <summary>Remaps <paramref name="Value"/> from [InMin, InMax] to [OutMin, OutMax].</summary>
    public static float Remap(float Value, float InMin, float InMax, float OutMin, float OutMax)
    {
        return Lerp(OutMin, OutMax, InverseLerp(InMin, InMax, Value));
    }

    /// <summary>Smooth Hermite interpolation between two edges.</summary>
    public static float SmoothStep(float Edge0, float Edge1, float X)
    {
        float T = Clamp01((X - Edge0) / (Edge1 - Edge0));
        return T * T * (3.0f - 2.0f * T);
    }

    /// <summary>0 if X &lt; Edge, else 1.</summary>
    public static float Step(float Edge, float X) => X < Edge ? 0.0f : 1.0f;

    /// <summary>Wraps <paramref name="T"/> into [0, Length).</summary>
    public static float Repeat(float T, float Length) => Clamp(T - MathF.Floor(T / Length) * Length, 0.0f, Length);

    /// <summary>Ping-pongs <paramref name="T"/> between 0 and Length.</summary>
    public static float PingPong(float T, float Length)
    {
        T = Repeat(T, Length * 2.0f);
        return Length - MathF.Abs(T - Length);
    }

    /// <summary>Floating-point modulo (result follows the sign of <paramref name="Y"/>, like GLSL mod).</summary>
    public static float Mod(float X, float Y) => X - Y * MathF.Floor(X / Y);

    /// <summary>Fractional part.</summary>
    public static float Fract(float V) => V - MathF.Floor(V);

    public static float Sign(float V) => V > 0.0f ? 1.0f : (V < 0.0f ? -1.0f : 0.0f);

    /// <summary>Moves <paramref name="Current"/> toward <paramref name="Target"/> by at most <paramref name="MaxDelta"/>.</summary>
    public static float MoveTowards(float Current, float Target, float MaxDelta)
    {
        return MathF.Abs(Target - Current) <= MaxDelta ? Target : Current + Sign(Target - Current) * MaxDelta;
    }

    /// <summary>Shortest signed difference between two angles (radians), in [-Pi, Pi].</summary>
    public static float DeltaAngle(float Current, float Target)
    {
        float Delta = Repeat(Target - Current, TwoPi);
        return Delta > Pi ? Delta - TwoPi : Delta;
    }

    /// <summary>Interpolates between angles (radians) along the shortest path.</summary>
    public static float LerpAngle(float A, float B, float T)
    {
        return A + DeltaAngle(A, B) * Clamp01(T);
    }

    /// <summary>Moves an angle (radians) toward a target along the shortest path.</summary>
    public static float MoveTowardsAngle(float Current, float Target, float MaxDelta)
    {
        float Delta = DeltaAngle(Current, Target);
        if (-MaxDelta < Delta && Delta < MaxDelta)
        {
            return Target;
        }
        return MoveTowards(Current, Current + Delta, MaxDelta);
    }

    /// <summary>Relative-tolerance compare scaled to magnitude (Unity-style).</summary>
    public static bool Approximately(float A, float B)
    {
        return MathF.Abs(B - A) <= MathF.Max(1e-6f * MathF.Max(MathF.Abs(A), MathF.Abs(B)), Epsilon * 8.0f);
    }

    public static bool IsNearlyZero(float V, float Tolerance = KindaSmallNumber) => MathF.Abs(V) <= Tolerance;
    public static bool IsNearlyEqual(float A, float B, float Tolerance = KindaSmallNumber) => MathF.Abs(A - B) <= Tolerance;

    /// <summary>1 / sqrt(V).</summary>
    public static float InvSqrt(float V) => 1.0f / MathF.Sqrt(V);

    /// <summary>Smallest power of two &gt;= <paramref name="V"/>.</summary>
    public static int NextPowerOfTwo(int V)
    {
        if (V <= 1)
        {
            return 1;
        }
        V--;
        V |= V >> 1;
        V |= V >> 2;
        V |= V >> 4;
        V |= V >> 8;
        V |= V >> 16;
        return V + 1;
    }

    public static bool IsPowerOfTwo(int V) => V > 0 && (V & (V - 1)) == 0;
}
