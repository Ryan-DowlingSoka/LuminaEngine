using System;
using System.Runtime.InteropServices;

namespace Lumina;

// Hand-written blittable mirrors of the engine's core math types (FVector*/FQuat are ManualStub + 
// in C++, so they aren't auto-generated). The field layout (declared first, in order) is validated against
// the native types by static_asserts in CSharpLayoutChecks.cpp; if those fail, these are wrong. Operators
// and helpers below are pure managed math and don't affect layout.
//
// Conventions mirror C++ (Core/Math): left-handed, +Z forward, +Y up, +X right. Angles are radians unless
// a member says otherwise. Tolerances match LE_SMALL_NUMBER (1e-8) and LE_KINDA_SMALL_NUMBER (1e-4).

[StructLayout(LayoutKind.Sequential)]
[LuminaSharp.NativeLayout("FVector2")]
public struct FVector2 : IEquatable<FVector2>
{
    public float X;
    public float Y;

    public FVector2(float X, float Y)
    {
        this.X = X;
        this.Y = Y;
    }

    public FVector2(float Value)
    {
        X = Value;
        Y = Value;
    }

    public static FVector2 Zero => new(0.0f, 0.0f);
    public static FVector2 One => new(1.0f, 1.0f);
    public static FVector2 UnitX => new(1.0f, 0.0f);
    public static FVector2 UnitY => new(0.0f, 1.0f);

    public float this[int Index]
    {
        get => Index == 0 ? X : Y;
        set { if (Index == 0) { X = value; } else { Y = value; } }
    }

    public float Length => MathF.Sqrt(X * X + Y * Y);
    public float LengthSquared => X * X + Y * Y;

    public bool IsNearlyZero(float Epsilon = 1e-4f) => LengthSquared <= Epsilon * Epsilon;
    public bool IsNormalized(float Epsilon = 1e-4f) => MathF.Abs(LengthSquared - 1.0f) <= Epsilon;

    public FVector2 Normalized()
    {
        float L = Length;
        return L > 1e-8f ? this / L : Zero;
    }

    /// <summary>Unit vector, or <paramref name="Fallback"/> when too short to normalize.</summary>
    public FVector2 NormalizedOr(FVector2 Fallback)
    {
        float L = Length;
        return L > 1e-8f ? this / L : Fallback;
    }

    public static float Dot(FVector2 A, FVector2 B)
    {
        return A.X * B.X + A.Y * B.Y;
    }

    /// <summary>2D cross product (scalar): the z of the 3D cross.</summary>
    public static float Cross(FVector2 A, FVector2 B)
    {
        return A.X * B.Y - A.Y * B.X;
    }

    /// <summary>Left-hand perpendicular (-Y, X).</summary>
    public FVector2 Perpendicular() => new(-Y, X);

    public static float Distance(FVector2 A, FVector2 B)
    {
        return (A - B).Length;
    }

    public static float DistanceSquared(FVector2 A, FVector2 B)
    {
        return (A - B).LengthSquared;
    }

    /// <summary>Unsigned angle between two vectors, in radians.</summary>
    public static float Angle(FVector2 A, FVector2 B)
    {
        float Denom = MathF.Sqrt(A.LengthSquared * B.LengthSquared);
        if (Denom < 1e-12f)
        {
            return 0.0f;
        }
        return MathF.Acos(Math.Clamp(Dot(A, B) / Denom, -1.0f, 1.0f));
    }

    public static FVector2 Lerp(FVector2 A, FVector2 B, float T)
    {
        return A + (B - A) * T;
    }

    public static FVector2 LerpClamped(FVector2 A, FVector2 B, float T)
    {
        return Lerp(A, B, Math.Clamp(T, 0.0f, 1.0f));
    }

    /// <summary>Moves <paramref name="Current"/> toward <paramref name="Target"/> by at most <paramref name="MaxDelta"/>.</summary>
    public static FVector2 MoveTowards(FVector2 Current, FVector2 Target, float MaxDelta)
    {
        FVector2 To = Target - Current;
        float Dist = To.Length;
        if (Dist <= MaxDelta || Dist < 1e-8f)
        {
            return Target;
        }
        return Current + To / Dist * MaxDelta;
    }

    /// <summary>Reflects <paramref name="Incident"/> about the (unit) <paramref name="Normal"/>.</summary>
    public static FVector2 Reflect(FVector2 Incident, FVector2 Normal)
    {
        return Incident - Normal * (2.0f * Dot(Incident, Normal));
    }

    /// <summary>Projection of <paramref name="V"/> onto <paramref name="Onto"/>.</summary>
    public static FVector2 Project(FVector2 V, FVector2 Onto)
    {
        float D = Dot(Onto, Onto);
        return D < 1e-12f ? Zero : Onto * (Dot(V, Onto) / D);
    }

    public static FVector2 Min(FVector2 A, FVector2 B) => new(MathF.Min(A.X, B.X), MathF.Min(A.Y, B.Y));
    public static FVector2 Max(FVector2 A, FVector2 B) => new(MathF.Max(A.X, B.X), MathF.Max(A.Y, B.Y));
    public static FVector2 Min(FVector2 V, float S) => new(MathF.Min(V.X, S), MathF.Min(V.Y, S));
    public static FVector2 Max(FVector2 V, float S) => new(MathF.Max(V.X, S), MathF.Max(V.Y, S));

    public static FVector2 Clamp(FVector2 V, FVector2 Lo, FVector2 Hi) => Min(Max(V, Lo), Hi);
    public static FVector2 Clamp(FVector2 V, float Lo, float Hi)
        => new(Math.Clamp(V.X, Lo, Hi), Math.Clamp(V.Y, Lo, Hi));

    /// <summary>Clamps the magnitude to <paramref name="MaxLength"/>, keeping direction.</summary>
    public FVector2 ClampLength(float MaxLength)
    {
        float LenSq = LengthSquared;
        if (LenSq > MaxLength * MaxLength && LenSq > 1e-12f)
        {
            return this * (MaxLength / MathF.Sqrt(LenSq));
        }
        return this;
    }

    public FVector2 ClampLength(float MinLength, float MaxLength)
    {
        float Len = Length;
        if (Len < 1e-8f)
        {
            return this;
        }
        float Clamped = Math.Clamp(Len, MinLength, MaxLength);
        return this * (Clamped / Len);
    }

    public static FVector2 Abs(FVector2 V) => new(MathF.Abs(V.X), MathF.Abs(V.Y));
    public static FVector2 Floor(FVector2 V) => new(MathF.Floor(V.X), MathF.Floor(V.Y));
    public static FVector2 Ceil(FVector2 V) => new(MathF.Ceiling(V.X), MathF.Ceiling(V.Y));
    public static FVector2 Round(FVector2 V) => new(MathF.Round(V.X), MathF.Round(V.Y));
    public static FVector2 Sign(FVector2 V) => new(MathF.Sign(V.X), MathF.Sign(V.Y));
    public static FVector2 Fract(FVector2 V) => new(V.X - MathF.Floor(V.X), V.Y - MathF.Floor(V.Y));

    public float MinComponent => MathF.Min(X, Y);
    public float MaxComponent => MathF.Max(X, Y);

    public static bool IsNearlyEqual(FVector2 A, FVector2 B, float Epsilon = 1e-4f)
        => MathF.Abs(A.X - B.X) <= Epsilon && MathF.Abs(A.Y - B.Y) <= Epsilon;

    public static FVector2 operator +(FVector2 A, FVector2 B) => new(A.X + B.X, A.Y + B.Y);
    public static FVector2 operator -(FVector2 A, FVector2 B) => new(A.X - B.X, A.Y - B.Y);
    public static FVector2 operator -(FVector2 A) => new(-A.X, -A.Y);
    public static FVector2 operator *(FVector2 A, FVector2 B) => new(A.X * B.X, A.Y * B.Y);
    public static FVector2 operator *(FVector2 A, float S) => new(A.X * S, A.Y * S);
    public static FVector2 operator *(float S, FVector2 A) => new(A.X * S, A.Y * S);
    public static FVector2 operator /(FVector2 A, FVector2 B) => new(A.X / B.X, A.Y / B.Y);
    public static FVector2 operator /(FVector2 A, float S) => new(A.X / S, A.Y / S);
    public static bool operator ==(FVector2 A, FVector2 B) => A.X == B.X && A.Y == B.Y;
    public static bool operator !=(FVector2 A, FVector2 B) => !(A == B);

    public bool Equals(FVector2 Other) => this == Other;
    public override bool Equals(object? Obj) => Obj is FVector2 Other && Equals(Other);
    public override int GetHashCode() => HashCode.Combine(X, Y);
    public override string ToString() => $"({X}, {Y})";
}

[StructLayout(LayoutKind.Sequential)]
[LuminaSharp.NativeLayout("FVector3")]
public struct FVector3 : IEquatable<FVector3>
{
    public float X;
    public float Y;
    public float Z;

    public FVector3(float X, float Y, float Z)
    {
        this.X = X;
        this.Y = Y;
        this.Z = Z;
    }

    public FVector3(float Value)
    {
        X = Value;
        Y = Value;
        Z = Value;
    }

    public FVector3(FVector2 XY, float Z)
    {
        X = XY.X;
        Y = XY.Y;
        this.Z = Z;
    }

    public static FVector3 Zero => new(0.0f);
    public static FVector3 One => new(1.0f);
    public static FVector3 UnitX => new(1.0f, 0.0f, 0.0f);
    public static FVector3 UnitY => new(0.0f, 1.0f, 0.0f);
    public static FVector3 UnitZ => new(0.0f, 0.0f, 1.0f);
    public static FVector3 Right => new(1.0f, 0.0f, 0.0f);
    public static FVector3 Up => new(0.0f, 1.0f, 0.0f);
    public static FVector3 Forward => new(0.0f, 0.0f, 1.0f);

    public FVector2 XY => new(X, Y);

    public float this[int Index]
    {
        get
        {
            switch (Index)
            {
                case 0: return X;
                case 1: return Y;
                default: return Z;
            }
        }
        set
        {
            switch (Index)
            {
                case 0: X = value; break;
                case 1: Y = value; break;
                default: Z = value; break;
            }
        }
    }

    public float Length => MathF.Sqrt(X * X + Y * Y + Z * Z);
    public float LengthSquared => X * X + Y * Y + Z * Z;

    public bool IsNearlyZero(float Epsilon = 1e-4f) => LengthSquared <= Epsilon * Epsilon;
    public bool IsNormalized(float Epsilon = 1e-4f) => MathF.Abs(LengthSquared - 1.0f) <= Epsilon;

    public FVector3 Normalized()
    {
        float L = Length;
        return L > 1e-8f ? this / L : Zero;
    }

    public FVector3 NormalizedOr(FVector3 Fallback)
    {
        float L = Length;
        return L > 1e-8f ? this / L : Fallback;
    }

    public static float Dot(FVector3 A, FVector3 B)
    {
        return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
    }

    public static FVector3 Cross(FVector3 A, FVector3 B)
    {
        return new FVector3(
            A.Y * B.Z - A.Z * B.Y,
            A.Z * B.X - A.X * B.Z,
            A.X * B.Y - A.Y * B.X);
    }

    public static float Distance(FVector3 A, FVector3 B)
    {
        return (A - B).Length;
    }

    public static float DistanceSquared(FVector3 A, FVector3 B)
    {
        return (A - B).LengthSquared;
    }

    /// <summary>Unsigned angle between two vectors, in radians.</summary>
    public static float Angle(FVector3 A, FVector3 B)
    {
        float Denom = MathF.Sqrt(A.LengthSquared * B.LengthSquared);
        if (Denom < 1e-12f)
        {
            return 0.0f;
        }
        return MathF.Acos(Math.Clamp(Dot(A, B) / Denom, -1.0f, 1.0f));
    }

    /// <summary>Signed angle (radians) from <paramref name="A"/> to <paramref name="B"/> about <paramref name="Axis"/>.</summary>
    public static float SignedAngle(FVector3 A, FVector3 B, FVector3 Axis)
    {
        float Unsigned = Angle(A, B);
        float Sign = MathF.Sign(Dot(Axis, Cross(A, B)));
        return Unsigned * (Sign == 0.0f ? 1.0f : Sign);
    }

    public static FVector3 Lerp(FVector3 A, FVector3 B, float T)
    {
        return A + (B - A) * T;
    }

    public static FVector3 LerpClamped(FVector3 A, FVector3 B, float T)
    {
        return Lerp(A, B, Math.Clamp(T, 0.0f, 1.0f));
    }

    /// <summary>Normalized linear interpolation of two directions (cheap great-arc approximation).</summary>
    public static FVector3 Slerp(FVector3 A, FVector3 B, float T)
    {
        float CosTheta = Math.Clamp(Dot(A.Normalized(), B.Normalized()), -1.0f, 1.0f);
        if (CosTheta > 1.0f - 1e-4f)
        {
            return Lerp(A, B, T).Normalized();
        }
        float Theta = MathF.Acos(CosTheta) * T;
        FVector3 Relative = (B - A * CosTheta).Normalized();
        return (A * MathF.Cos(Theta) + Relative * MathF.Sin(Theta));
    }

    public static FVector3 MoveTowards(FVector3 Current, FVector3 Target, float MaxDelta)
    {
        FVector3 To = Target - Current;
        float Dist = To.Length;
        if (Dist <= MaxDelta || Dist < 1e-8f)
        {
            return Target;
        }
        return Current + To / Dist * MaxDelta;
    }

    public static FVector3 Reflect(FVector3 Incident, FVector3 Normal)
    {
        return Incident - Normal * (2.0f * Dot(Incident, Normal));
    }

    /// <summary>Refracts <paramref name="Incident"/> (unit) about <paramref name="Normal"/> with index ratio <paramref name="Eta"/>; zero on total internal reflection.</summary>
    public static FVector3 Refract(FVector3 Incident, FVector3 Normal, float Eta)
    {
        float DotNI = Dot(Normal, Incident);
        float K = 1.0f - Eta * Eta * (1.0f - DotNI * DotNI);
        if (K < 0.0f)
        {
            return Zero;
        }
        return Incident * Eta - Normal * (Eta * DotNI + MathF.Sqrt(K));
    }

    public static FVector3 Project(FVector3 V, FVector3 Onto)
    {
        float D = Dot(Onto, Onto);
        return D < 1e-12f ? Zero : Onto * (Dot(V, Onto) / D);
    }

    /// <summary>Component of <paramref name="V"/> in the plane with the given (unit) <paramref name="Normal"/>.</summary>
    public static FVector3 ProjectOnPlane(FVector3 V, FVector3 Normal)
    {
        return V - Project(V, Normal);
    }

    public static FVector3 Min(FVector3 A, FVector3 B) => new(MathF.Min(A.X, B.X), MathF.Min(A.Y, B.Y), MathF.Min(A.Z, B.Z));
    public static FVector3 Max(FVector3 A, FVector3 B) => new(MathF.Max(A.X, B.X), MathF.Max(A.Y, B.Y), MathF.Max(A.Z, B.Z));
    public static FVector3 Min(FVector3 V, float S) => new(MathF.Min(V.X, S), MathF.Min(V.Y, S), MathF.Min(V.Z, S));
    public static FVector3 Max(FVector3 V, float S) => new(MathF.Max(V.X, S), MathF.Max(V.Y, S), MathF.Max(V.Z, S));

    public static FVector3 Clamp(FVector3 V, FVector3 Lo, FVector3 Hi) => Min(Max(V, Lo), Hi);
    public static FVector3 Clamp(FVector3 V, float Lo, float Hi)
        => new(Math.Clamp(V.X, Lo, Hi), Math.Clamp(V.Y, Lo, Hi), Math.Clamp(V.Z, Lo, Hi));

    public FVector3 ClampLength(float MaxLength)
    {
        float LenSq = LengthSquared;
        if (LenSq > MaxLength * MaxLength && LenSq > 1e-12f)
        {
            return this * (MaxLength / MathF.Sqrt(LenSq));
        }
        return this;
    }

    public FVector3 ClampLength(float MinLength, float MaxLength)
    {
        float Len = Length;
        if (Len < 1e-8f)
        {
            return this;
        }
        float Clamped = Math.Clamp(Len, MinLength, MaxLength);
        return this * (Clamped / Len);
    }

    public static FVector3 Abs(FVector3 V) => new(MathF.Abs(V.X), MathF.Abs(V.Y), MathF.Abs(V.Z));
    public static FVector3 Floor(FVector3 V) => new(MathF.Floor(V.X), MathF.Floor(V.Y), MathF.Floor(V.Z));
    public static FVector3 Ceil(FVector3 V) => new(MathF.Ceiling(V.X), MathF.Ceiling(V.Y), MathF.Ceiling(V.Z));
    public static FVector3 Round(FVector3 V) => new(MathF.Round(V.X), MathF.Round(V.Y), MathF.Round(V.Z));
    public static FVector3 Sign(FVector3 V) => new(MathF.Sign(V.X), MathF.Sign(V.Y), MathF.Sign(V.Z));
    public static FVector3 Fract(FVector3 V) => new(V.X - MathF.Floor(V.X), V.Y - MathF.Floor(V.Y), V.Z - MathF.Floor(V.Z));

    public float MinComponent => MathF.Min(X, MathF.Min(Y, Z));
    public float MaxComponent => MathF.Max(X, MathF.Max(Y, Z));

    public static bool IsNearlyEqual(FVector3 A, FVector3 B, float Epsilon = 1e-4f)
        => MathF.Abs(A.X - B.X) <= Epsilon && MathF.Abs(A.Y - B.Y) <= Epsilon && MathF.Abs(A.Z - B.Z) <= Epsilon;

    public static FVector3 operator +(FVector3 A, FVector3 B) => new(A.X + B.X, A.Y + B.Y, A.Z + B.Z);
    public static FVector3 operator -(FVector3 A, FVector3 B) => new(A.X - B.X, A.Y - B.Y, A.Z - B.Z);
    public static FVector3 operator -(FVector3 A) => new(-A.X, -A.Y, -A.Z);
    public static FVector3 operator *(FVector3 A, FVector3 B) => new(A.X * B.X, A.Y * B.Y, A.Z * B.Z);
    public static FVector3 operator *(FVector3 A, float S) => new(A.X * S, A.Y * S, A.Z * S);
    public static FVector3 operator *(float S, FVector3 A) => new(A.X * S, A.Y * S, A.Z * S);
    public static FVector3 operator /(FVector3 A, FVector3 B) => new(A.X / B.X, A.Y / B.Y, A.Z / B.Z);
    public static FVector3 operator /(FVector3 A, float S) => new(A.X / S, A.Y / S, A.Z / S);
    public static bool operator ==(FVector3 A, FVector3 B) => A.X == B.X && A.Y == B.Y && A.Z == B.Z;
    public static bool operator !=(FVector3 A, FVector3 B) => !(A == B);

    public bool Equals(FVector3 Other) => this == Other;
    public override bool Equals(object? Obj) => Obj is FVector3 Other && Equals(Other);
    public override int GetHashCode() => HashCode.Combine(X, Y, Z);
    public override string ToString() => $"({X}, {Y}, {Z})";
}

[StructLayout(LayoutKind.Sequential)]
[LuminaSharp.NativeLayout("FVector4")]
public struct FVector4 : IEquatable<FVector4>
{
    public float X;
    public float Y;
    public float Z;
    public float W;

    public FVector4(float X, float Y, float Z, float W)
    {
        this.X = X;
        this.Y = Y;
        this.Z = Z;
        this.W = W;
    }

    public FVector4(float Value)
    {
        X = Value;
        Y = Value;
        Z = Value;
        W = Value;
    }

    public FVector4(FVector3 XYZ, float W)
    {
        X = XYZ.X;
        Y = XYZ.Y;
        Z = XYZ.Z;
        this.W = W;
    }

    public static FVector4 Zero => new(0.0f);
    public static FVector4 One => new(1.0f);

    public FVector3 XYZ => new(X, Y, Z);

    public float this[int Index]
    {
        get
        {
            switch (Index)
            {
                case 0: return X;
                case 1: return Y;
                case 2: return Z;
                default: return W;
            }
        }
        set
        {
            switch (Index)
            {
                case 0: X = value; break;
                case 1: Y = value; break;
                case 2: Z = value; break;
                default: W = value; break;
            }
        }
    }

    public float Length => MathF.Sqrt(X * X + Y * Y + Z * Z + W * W);
    public float LengthSquared => X * X + Y * Y + Z * Z + W * W;

    public FVector4 Normalized()
    {
        float L = Length;
        return L > 1e-8f ? this / L : Zero;
    }

    public static float Dot(FVector4 A, FVector4 B)
    {
        return A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W;
    }

    public static float Distance(FVector4 A, FVector4 B) => (A - B).Length;
    public static float DistanceSquared(FVector4 A, FVector4 B) => (A - B).LengthSquared;

    public static FVector4 Lerp(FVector4 A, FVector4 B, float T)
    {
        return A + (B - A) * T;
    }

    public static FVector4 LerpClamped(FVector4 A, FVector4 B, float T)
    {
        return Lerp(A, B, Math.Clamp(T, 0.0f, 1.0f));
    }

    public static FVector4 Min(FVector4 A, FVector4 B) => new(MathF.Min(A.X, B.X), MathF.Min(A.Y, B.Y), MathF.Min(A.Z, B.Z), MathF.Min(A.W, B.W));
    public static FVector4 Max(FVector4 A, FVector4 B) => new(MathF.Max(A.X, B.X), MathF.Max(A.Y, B.Y), MathF.Max(A.Z, B.Z), MathF.Max(A.W, B.W));

    public static FVector4 Clamp(FVector4 V, FVector4 Lo, FVector4 Hi) => Min(Max(V, Lo), Hi);
    public static FVector4 Clamp(FVector4 V, float Lo, float Hi)
        => new(Math.Clamp(V.X, Lo, Hi), Math.Clamp(V.Y, Lo, Hi), Math.Clamp(V.Z, Lo, Hi), Math.Clamp(V.W, Lo, Hi));

    public static FVector4 Abs(FVector4 V) => new(MathF.Abs(V.X), MathF.Abs(V.Y), MathF.Abs(V.Z), MathF.Abs(V.W));

    public float MinComponent => MathF.Min(MathF.Min(X, Y), MathF.Min(Z, W));
    public float MaxComponent => MathF.Max(MathF.Max(X, Y), MathF.Max(Z, W));

    public static bool IsNearlyEqual(FVector4 A, FVector4 B, float Epsilon = 1e-4f)
        => MathF.Abs(A.X - B.X) <= Epsilon && MathF.Abs(A.Y - B.Y) <= Epsilon
        && MathF.Abs(A.Z - B.Z) <= Epsilon && MathF.Abs(A.W - B.W) <= Epsilon;

    public static FVector4 operator +(FVector4 A, FVector4 B) => new(A.X + B.X, A.Y + B.Y, A.Z + B.Z, A.W + B.W);
    public static FVector4 operator -(FVector4 A, FVector4 B) => new(A.X - B.X, A.Y - B.Y, A.Z - B.Z, A.W - B.W);
    public static FVector4 operator -(FVector4 A) => new(-A.X, -A.Y, -A.Z, -A.W);
    public static FVector4 operator *(FVector4 A, FVector4 B) => new(A.X * B.X, A.Y * B.Y, A.Z * B.Z, A.W * B.W);
    public static FVector4 operator *(FVector4 A, float S) => new(A.X * S, A.Y * S, A.Z * S, A.W * S);
    public static FVector4 operator *(float S, FVector4 A) => new(A.X * S, A.Y * S, A.Z * S, A.W * S);
    public static FVector4 operator /(FVector4 A, FVector4 B) => new(A.X / B.X, A.Y / B.Y, A.Z / B.Z, A.W / B.W);
    public static FVector4 operator /(FVector4 A, float S) => new(A.X / S, A.Y / S, A.Z / S, A.W / S);
    public static bool operator ==(FVector4 A, FVector4 B) => A.X == B.X && A.Y == B.Y && A.Z == B.Z && A.W == B.W;
    public static bool operator !=(FVector4 A, FVector4 B) => !(A == B);

    public bool Equals(FVector4 Other) => this == Other;
    public override bool Equals(object? Obj) => Obj is FVector4 Other && Equals(Other);
    public override int GetHashCode() => HashCode.Combine(X, Y, Z, W);
    public override string ToString() => $"({X}, {Y}, {Z}, {W})";
}

[StructLayout(LayoutKind.Sequential)]
[LuminaSharp.NativeLayout("FQuat")]
public struct FQuat : IEquatable<FQuat>
{
    public float X;
    public float Y;
    public float Z;
    public float W;

    public FQuat(float X, float Y, float Z, float W)
    {
        this.X = X;
        this.Y = Y;
        this.Z = Z;
        this.W = W;
    }

    public static FQuat Identity => new(0.0f, 0.0f, 0.0f, 1.0f);

    /// <summary>The imaginary (vector) part.</summary>
    public FVector3 Vector => new(X, Y, Z);

    /// <summary>A rotation of <paramref name="AngleRadians"/> about the (normalized) <paramref name="Axis"/>.</summary>
    public static FQuat AngleAxis(float AngleRadians, FVector3 Axis)
    {
        FVector3 N = Axis.Normalized();
        float Half = AngleRadians * 0.5f;
        float S = MathF.Sin(Half);
        return new FQuat(N.X * S, N.Y * S, N.Z * S, MathF.Cos(Half));
    }

    /// <summary>Alias of <see cref="AngleAxis"/> with axis-first argument order (matches C++ Math::FromAxisAngle).</summary>
    public static FQuat FromAxisAngle(FVector3 Axis, float AngleRadians) => AngleAxis(AngleRadians, Axis);

    /// <summary>From euler angles in radians (pitch=X, yaw=Y, roll=Z). Matches C++ TQuat(euler).</summary>
    public static FQuat FromEuler(FVector3 EulerRadians)
    {
        float Cx = MathF.Cos(EulerRadians.X * 0.5f), Sx = MathF.Sin(EulerRadians.X * 0.5f);
        float Cy = MathF.Cos(EulerRadians.Y * 0.5f), Sy = MathF.Sin(EulerRadians.Y * 0.5f);
        float Cz = MathF.Cos(EulerRadians.Z * 0.5f), Sz = MathF.Sin(EulerRadians.Z * 0.5f);

        return new FQuat(
            Sx * Cy * Cz - Cx * Sy * Sz,
            Cx * Sy * Cz + Sx * Cy * Sz,
            Cx * Cy * Sz - Sx * Sy * Cz,
            Cx * Cy * Cz + Sx * Sy * Sz);
    }

    public static FQuat FromEuler(float PitchRadians, float YawRadians, float RollRadians)
        => FromEuler(new FVector3(PitchRadians, YawRadians, RollRadians));

    /// <summary>Euler angles in radians (pitch=X, yaw=Y, roll=Z). Matches C++ Math::EulerAngles.</summary>
    public FVector3 ToEuler()
    {
        float Pitch = MathF.Atan2(2.0f * (Y * Z + W * X), W * W - X * X - Y * Y + Z * Z);
        float SinYaw = Math.Clamp(-2.0f * (X * Z - W * Y), -1.0f, 1.0f);
        float Yaw = MathF.Asin(SinYaw);
        float Roll = MathF.Atan2(2.0f * (X * Y + W * Z), W * W + X * X - Y * Y - Z * Z);
        return new FVector3(Pitch, Yaw, Roll);
    }

    /// <summary>Shortest-arc rotation that maps <paramref name="From"/> onto <paramref name="To"/> (both should be unit).</summary>
    public static FQuat FromToRotation(FVector3 From, FVector3 To)
    {
        From = From.Normalized();
        To = To.Normalized();
        float CosTheta = FVector3.Dot(From, To);

        if (CosTheta >= 1.0f - 1e-6f)
        {
            return Identity;
        }
        if (CosTheta < -1.0f + 1e-6f)
        {
            FVector3 Axis = FVector3.Cross(FVector3.UnitZ, From);
            if (Axis.LengthSquared < 1e-6f)
            {
                Axis = FVector3.Cross(FVector3.UnitX, From);
            }
            return AngleAxis(MathF.PI, Axis.Normalized());
        }

        FVector3 C = FVector3.Cross(From, To);
        float S = MathF.Sqrt((1.0f + CosTheta) * 2.0f);
        float InvS = 1.0f / S;
        return new FQuat(C.X * InvS, C.Y * InvS, C.Z * InvS, S * 0.5f);
    }

    /// <summary>Look-rotation from a forward <paramref name="Direction"/> and an <paramref name="Up"/> hint (left-handed, +Z forward).</summary>
    public static FQuat LookRotation(FVector3 Direction, FVector3 Up)
    {
        FVector3 F = Direction.Normalized();
        FVector3 R = FVector3.Cross(Up, F);
        float RLenSq = R.LengthSquared;
        R = RLenSq > 1e-8f ? R / MathF.Sqrt(RLenSq) : FVector3.UnitX;
        FVector3 U = FVector3.Cross(F, R);

        // Column-major basis (R, U, F) -> quaternion, branch-by-largest-component.
        float M00 = R.X, M01 = R.Y, M02 = R.Z;
        float M10 = U.X, M11 = U.Y, M12 = U.Z;
        float M20 = F.X, M21 = F.Y, M22 = F.Z;

        float FourWSq = M00 + M11 + M22;
        float FourXSq = M00 - M11 - M22;
        float FourYSq = M11 - M00 - M22;
        float FourZSq = M22 - M00 - M11;

        int Biggest = 0;
        float FourBiggest = FourWSq;
        if (FourXSq > FourBiggest) { FourBiggest = FourXSq; Biggest = 1; }
        if (FourYSq > FourBiggest) { FourBiggest = FourYSq; Biggest = 2; }
        if (FourZSq > FourBiggest) { FourBiggest = FourZSq; Biggest = 3; }

        float BiggestVal = MathF.Sqrt(FourBiggest + 1.0f) * 0.5f;
        float Mult = 0.25f / BiggestVal;

        switch (Biggest)
        {
            case 0: return new FQuat((M12 - M21) * Mult, (M20 - M02) * Mult, (M01 - M10) * Mult, BiggestVal);
            case 1: return new FQuat(BiggestVal, (M01 + M10) * Mult, (M20 + M02) * Mult, (M12 - M21) * Mult);
            case 2: return new FQuat((M01 + M10) * Mult, BiggestVal, (M12 + M21) * Mult, (M20 - M02) * Mult);
            default: return new FQuat((M20 + M02) * Mult, (M12 + M21) * Mult, BiggestVal, (M01 - M10) * Mult);
        }
    }

    public float Length => MathF.Sqrt(X * X + Y * Y + Z * Z + W * W);
    public float LengthSquared => X * X + Y * Y + Z * Z + W * W;

    public FQuat Normalized()
    {
        float L = Length;
        return L > 1e-8f ? new FQuat(X / L, Y / L, Z / L, W / L) : Identity;
    }

    public FQuat Conjugate() => new(-X, -Y, -Z, W);

    /// <summary>Inverse rotation (conjugate / |q|^2; equals the conjugate for a unit quaternion).</summary>
    public FQuat Inverse()
    {
        float LenSq = LengthSquared;
        if (LenSq < 1e-8f)
        {
            return Identity;
        }
        float Inv = 1.0f / LenSq;
        return new FQuat(-X * Inv, -Y * Inv, -Z * Inv, W * Inv);
    }

    public static float Dot(FQuat A, FQuat B) => A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W;

    /// <summary>Total rotation angle of this quaternion, in radians (0..PI).</summary>
    public float Angle()
    {
        float Norm = Length;
        float WAbs = Norm > 1e-8f ? MathF.Abs(W) / Norm : 1.0f;
        return 2.0f * MathF.Acos(Math.Clamp(WAbs, -1.0f, 1.0f));
    }

    /// <summary>Angle between two rotations, in radians.</summary>
    public static float Angle(FQuat A, FQuat B)
    {
        float D = MathF.Abs(Dot(A.Normalized(), B.Normalized()));
        return 2.0f * MathF.Acos(Math.Clamp(D, -1.0f, 1.0f));
    }

    /// <summary>Normalized linear interpolation (cheap; shortest arc). Result is normalized.</summary>
    public static FQuat Nlerp(FQuat A, FQuat B, float T)
    {
        if (Dot(A, B) < 0.0f)
        {
            B = -B;
        }
        return (A + (B - A) * T).Normalized();
    }

    /// <summary>Spherical interpolation along the shortest arc. Matches C++ Math::Slerp.</summary>
    public static FQuat Slerp(FQuat A, FQuat B, float Alpha)
    {
        float CosTheta = Dot(A, B);
        FQuat End = B;

        if (CosTheta < 0.0f)
        {
            End = -B;
            CosTheta = -CosTheta;
        }

        if (CosTheta > 1.0f - 1e-4f)
        {
            return (A + (End - A) * Alpha).Normalized();
        }

        float Theta = MathF.Acos(CosTheta);
        float SinTheta = MathF.Sin(Theta);
        float WA = MathF.Sin((1.0f - Alpha) * Theta) / SinTheta;
        float WB = MathF.Sin(Alpha * Theta) / SinTheta;
        return A * WA + End * WB;
    }

    /// <summary>Rotates <paramref name="From"/> toward <paramref name="To"/> by at most <paramref name="MaxRadians"/>.</summary>
    public static FQuat RotateTowards(FQuat From, FQuat To, float MaxRadians)
    {
        float Theta = Angle(From, To);
        if (Theta < 1e-6f)
        {
            return To;
        }
        return Slerp(From, To, MathF.Min(1.0f, MaxRadians / Theta));
    }

    /// <summary>Clamps the rotation magnitude (angle from identity) to <paramref name="MaxRadians"/>.</summary>
    public static FQuat ClampAngle(FQuat Q, float MaxRadians)
    {
        Q = Q.Normalized();
        float Theta = Q.Angle();
        if (Theta <= MaxRadians || Theta < 1e-6f)
        {
            return Q;
        }
        return Slerp(Identity, Q, MaxRadians / Theta);
    }

    /// <summary>Decomposes into axis (unit) and angle (radians).</summary>
    public void ToAxisAngle(out FVector3 Axis, out float AngleRadians)
    {
        FQuat Q = Normalized();
        AngleRadians = 2.0f * MathF.Acos(Math.Clamp(Q.W, -1.0f, 1.0f));
        float S = MathF.Sqrt(MathF.Max(0.0f, 1.0f - Q.W * Q.W));
        Axis = S < 1e-6f ? FVector3.UnitX : new FVector3(Q.X / S, Q.Y / S, Q.Z / S);
    }

    /// <summary>Rotates <paramref name="V"/> by this quaternion (assumes a unit quaternion).</summary>
    public FVector3 Rotate(FVector3 V)
    {
        FVector3 U = new(X, Y, Z);
        float S = W;
        return 2.0f * FVector3.Dot(U, V) * U
            + (S * S - FVector3.Dot(U, U)) * V
            + 2.0f * S * FVector3.Cross(U, V);
    }

    public static bool IsNearlyEqual(FQuat A, FQuat B, float Epsilon = 1e-4f)
        => MathF.Abs(A.X - B.X) <= Epsilon && MathF.Abs(A.Y - B.Y) <= Epsilon
        && MathF.Abs(A.Z - B.Z) <= Epsilon && MathF.Abs(A.W - B.W) <= Epsilon;

    /// <summary>Quaternion composition (apply <paramref name="B"/> then <paramref name="A"/>).</summary>
    public static FQuat operator *(FQuat A, FQuat B)
    {
        return new FQuat(
            A.W * B.X + A.X * B.W + A.Y * B.Z - A.Z * B.Y,
            A.W * B.Y - A.X * B.Z + A.Y * B.W + A.Z * B.X,
            A.W * B.Z + A.X * B.Y - A.Y * B.X + A.Z * B.W,
            A.W * B.W - A.X * B.X - A.Y * B.Y - A.Z * B.Z);
    }

    /// <summary>Rotates a vector by the quaternion.</summary>
    public static FVector3 operator *(FQuat Q, FVector3 V) => Q.Rotate(V);

    public static FQuat operator *(FQuat Q, float S) => new(Q.X * S, Q.Y * S, Q.Z * S, Q.W * S);
    public static FQuat operator *(float S, FQuat Q) => new(Q.X * S, Q.Y * S, Q.Z * S, Q.W * S);
    public static FQuat operator +(FQuat A, FQuat B) => new(A.X + B.X, A.Y + B.Y, A.Z + B.Z, A.W + B.W);
    public static FQuat operator -(FQuat A, FQuat B) => new(A.X - B.X, A.Y - B.Y, A.Z - B.Z, A.W - B.W);
    public static FQuat operator -(FQuat Q) => new(-Q.X, -Q.Y, -Q.Z, -Q.W);

    public static bool operator ==(FQuat A, FQuat B) => A.X == B.X && A.Y == B.Y && A.Z == B.Z && A.W == B.W;
    public static bool operator !=(FQuat A, FQuat B) => !(A == B);

    public bool Equals(FQuat Other) => this == Other;
    public override bool Equals(object? Obj) => Obj is FQuat Other && Equals(Other);
    public override int GetHashCode() => HashCode.Combine(X, Y, Z, W);
    public override string ToString() => $"({X}, {Y}, {Z}, {W})";
}
