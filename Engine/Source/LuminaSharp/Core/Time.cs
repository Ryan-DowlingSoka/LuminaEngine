namespace LuminaSharp;

/// <summary>Frame timing for the current world. Valid inside any gameplay callback (reads <see cref="Game.World"/>).</summary>
public static class Time
{
    /// <summary>Seconds since the last frame.</summary>
    public static float Delta => Game.World.DeltaTime;

    /// <summary>Alias of <see cref="Delta"/>.</summary>
    public static float DeltaTime => Game.World.DeltaTime;

    /// <summary>Seconds since the world was created.</summary>
    public static double Now => Game.World.ElapsedTime;
}
