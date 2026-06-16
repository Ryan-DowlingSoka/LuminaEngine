using System;
using System.Runtime.InteropServices;

namespace Lumina;

// Hand-written blittable mirrors of the engine's core math types (FVector*/FQuat are ManualStub + NoLua
// in C++, so they aren't auto-generated). The field layout (declared first, in order) is validated against
// the native types by static_asserts in CSharpLayoutChecks.cpp; if those fail, these are wrong. Operators
// and helpers below are pure managed math and don't affect layout.

[StructLayout(LayoutKind.Sequential)]
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

    public float Length => MathF.Sqrt(X * X + Y * Y);
    public float LengthSquared => X * X + Y * Y;

    public FVector2 Normalized()
    {
        float L = Length;
        return L > 1e-8f ? this / L : Zero;
    }

    public static float Dot(FVector2 A, FVector2 B)
    {
        return A.X * B.X + A.Y * B.Y;
    }

    public static float Distance(FVector2 A, FVector2 B)
    {
        return (A - B).Length;
    }

    public static FVector2 Lerp(FVector2 A, FVector2 B, float T)
    {
        return A + (B - A) * T;
    }

    public static FVector2 operator +(FVector2 A, FVector2 B) => new(A.X + B.X, A.Y + B.Y);
    public static FVector2 operator -(FVector2 A, FVector2 B) => new(A.X - B.X, A.Y - B.Y);
    public static FVector2 operator -(FVector2 A) => new(-A.X, -A.Y);
    public static FVector2 operator *(FVector2 A, float S) => new(A.X * S, A.Y * S);
    public static FVector2 operator *(float S, FVector2 A) => new(A.X * S, A.Y * S);
    public static FVector2 operator /(FVector2 A, float S) => new(A.X / S, A.Y / S);
    public static bool operator ==(FVector2 A, FVector2 B) => A.X == B.X && A.Y == B.Y;
    public static bool operator !=(FVector2 A, FVector2 B) => !(A == B);

    public bool Equals(FVector2 Other) => this == Other;
    public override bool Equals(object? Obj) => Obj is FVector2 Other && Equals(Other);
    public override int GetHashCode() => HashCode.Combine(X, Y);
    public override string ToString() => $"({X}, {Y})";
}

[StructLayout(LayoutKind.Sequential)]
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

    public static FVector3 Zero => new(0.0f);
    public static FVector3 One => new(1.0f);
    public static FVector3 UnitX => new(1.0f, 0.0f, 0.0f);
    public static FVector3 UnitY => new(0.0f, 1.0f, 0.0f);
    public static FVector3 UnitZ => new(0.0f, 0.0f, 1.0f);

    public float Length => MathF.Sqrt(X * X + Y * Y + Z * Z);
    public float LengthSquared => X * X + Y * Y + Z * Z;

    public FVector3 Normalized()
    {
        float L = Length;
        return L > 1e-8f ? this / L : Zero;
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

    public static FVector3 Lerp(FVector3 A, FVector3 B, float T)
    {
        return A + (B - A) * T;
    }

    public static FVector3 operator +(FVector3 A, FVector3 B) => new(A.X + B.X, A.Y + B.Y, A.Z + B.Z);
    public static FVector3 operator -(FVector3 A, FVector3 B) => new(A.X - B.X, A.Y - B.Y, A.Z - B.Z);
    public static FVector3 operator -(FVector3 A) => new(-A.X, -A.Y, -A.Z);
    public static FVector3 operator *(FVector3 A, float S) => new(A.X * S, A.Y * S, A.Z * S);
    public static FVector3 operator *(float S, FVector3 A) => new(A.X * S, A.Y * S, A.Z * S);
    public static FVector3 operator /(FVector3 A, float S) => new(A.X / S, A.Y / S, A.Z / S);
    public static bool operator ==(FVector3 A, FVector3 B) => A.X == B.X && A.Y == B.Y && A.Z == B.Z;
    public static bool operator !=(FVector3 A, FVector3 B) => !(A == B);

    public bool Equals(FVector3 Other) => this == Other;
    public override bool Equals(object? Obj) => Obj is FVector3 Other && Equals(Other);
    public override int GetHashCode() => HashCode.Combine(X, Y, Z);
    public override string ToString() => $"({X}, {Y}, {Z})";
}

[StructLayout(LayoutKind.Sequential)]
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

    public static FVector4 Zero => new(0.0f);
    public static FVector4 One => new(1.0f);

    public static FVector4 operator +(FVector4 A, FVector4 B) => new(A.X + B.X, A.Y + B.Y, A.Z + B.Z, A.W + B.W);
    public static FVector4 operator -(FVector4 A, FVector4 B) => new(A.X - B.X, A.Y - B.Y, A.Z - B.Z, A.W - B.W);
    public static FVector4 operator *(FVector4 A, float S) => new(A.X * S, A.Y * S, A.Z * S, A.W * S);
    public static bool operator ==(FVector4 A, FVector4 B) => A.X == B.X && A.Y == B.Y && A.Z == B.Z && A.W == B.W;
    public static bool operator !=(FVector4 A, FVector4 B) => !(A == B);

    public bool Equals(FVector4 Other) => this == Other;
    public override bool Equals(object? Obj) => Obj is FVector4 Other && Equals(Other);
    public override int GetHashCode() => HashCode.Combine(X, Y, Z, W);
    public override string ToString() => $"({X}, {Y}, {Z}, {W})";
}

[StructLayout(LayoutKind.Sequential)]
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

    /// <summary>A rotation of <paramref name="AngleRadians"/> about the (normalized) <paramref name="Axis"/>.</summary>
    public static FQuat AngleAxis(float AngleRadians, FVector3 Axis)
    {
        FVector3 N = Axis.Normalized();
        float Half = AngleRadians * 0.5f;
        float S = MathF.Sin(Half);
        return new FQuat(N.X * S, N.Y * S, N.Z * S, MathF.Cos(Half));
    }

    public float Length => MathF.Sqrt(X * X + Y * Y + Z * Z + W * W);

    public FQuat Normalized()
    {
        float L = Length;
        return L > 1e-8f ? new FQuat(X / L, Y / L, Z / L, W / L) : Identity;
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

    /// <summary>Quaternion composition (apply <paramref name="B"/> then <paramref name="A"/>).</summary>
    public static FQuat operator *(FQuat A, FQuat B)
    {
        return new FQuat(
            A.W * B.X + A.X * B.W + A.Y * B.Z - A.Z * B.Y,
            A.W * B.Y - A.X * B.Z + A.Y * B.W + A.Z * B.X,
            A.W * B.Z + A.X * B.Y - A.Y * B.X + A.Z * B.W,
            A.W * B.W - A.X * B.X - A.Y * B.Y - A.Z * B.Z);
    }

    public static bool operator ==(FQuat A, FQuat B) => A.X == B.X && A.Y == B.Y && A.Z == B.Z && A.W == B.W;
    public static bool operator !=(FQuat A, FQuat B) => !(A == B);

    public bool Equals(FQuat Other) => this == Other;
    public override bool Equals(object? Obj) => Obj is FQuat Other && Equals(Other);
    public override int GetHashCode() => HashCode.Combine(X, Y, Z, W);
    public override string ToString() => $"({X}, {Y}, {Z}, {W})";
}
