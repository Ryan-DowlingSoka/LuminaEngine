using Lumina;

namespace LuminaSharp;

/// <summary>
/// Immediate-mode debug drawing for the current world (Dev/Debug only). State-driven like s&amp;box's Gizmo:
/// set <see cref="Color"/> / <see cref="Thickness"/> / <see cref="Duration"/> once, then draw. Forwards to
/// <see cref="Game.World"/>'s draw facade. Duration &lt;= 0 draws for one frame.
/// </summary>
public static class Gizmo
{
    public static Color Color = LuminaSharp.Color.White;
    public static float Thickness = 1.0f;
    public static float Duration = 0.0f;

    public static void Line(FVector3 Start, FVector3 End)
        => Game.World.Draw.Line(Start, End, Color, Thickness, Duration);

    public static void Line(FVector3 Start, FVector3 End, Color Color)
        => Game.World.Draw.Line(Start, End, Color, Thickness, Duration);

    public static void Sphere(FVector3 Center, float Radius)
        => Game.World.Draw.Sphere(Center, Radius, Color, Thickness, Duration);

    public static void Box(FVector3 Center, FVector3 HalfExtents)
        => Game.World.Draw.Box(Center, HalfExtents, FQuat.Identity, Color, Thickness, Duration);

    public static void Box(FVector3 Center, FVector3 HalfExtents, FQuat Rotation)
        => Game.World.Draw.Box(Center, HalfExtents, Rotation, Color, Thickness, Duration);

    /// <summary>Screen-space text for this frame (stacked top-left).</summary>
    public static void Text(string Message)
        => Game.World.Draw.Text(Message, Color);
}
