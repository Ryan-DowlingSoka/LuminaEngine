using System;
using Lumina;

namespace LuminaSharp;

/// <summary>
/// Fluent physics query (s&amp;box-style <c>Trace.Ray(a, b).Ignore(self).Run()</c>). Composes over the world's
/// physics facade; no allocation until <see cref="Run"/>/<see cref="RunAll"/>. A mutable builder — chain and
/// run on one line. Uses the ambient <see cref="Game.World"/>.
/// </summary>
public struct Trace
{
    private CWorld World;
    private FVector3 From;
    private FVector3 To;
    private float Radius;
    private uint IgnoreId;
    private bool Masked;
    private ECollisionProfiles Mask;

    private static Trace Begin(FVector3 From, FVector3 To)
    {
        Trace T = default;
        T.World = Game.World;
        T.From = From;
        T.To = To;
        T.IgnoreId = Entity.Null.Id;
        return T;
    }

    /// <summary>A ray between two world points.</summary>
    public static Trace Ray(FVector3 From, FVector3 To) => Begin(From, To);

    /// <summary>A ray from an origin along a direction for a distance.</summary>
    public static Trace Ray(FVector3 Origin, FVector3 Direction, float Distance) => Begin(Origin, Origin + Direction.Normalized() * Distance);

    /// <summary>A swept sphere (thick ray) between two points.</summary>
    public static Trace Sphere(float Radius, FVector3 From, FVector3 To)
    {
        Trace T = Begin(From, To);
        T.Radius = Radius;
        return T;
    }

    /// <summary>Skip one entity's body (typically the caster's).</summary>
    public Trace Ignore(Entity Entity)
    {
        IgnoreId = Entity.Id;
        return this;
    }

    /// <summary>Skip the entity whose callback is running.</summary>
    public Trace IgnoreSelf()
    {
        IgnoreId = Game.CurrentEntity.Id;
        return this;
    }

    /// <summary>Only hit bodies whose collision layer intersects <paramref name="Mask"/> (ray traces).</summary>
    public Trace WithMask(ECollisionProfiles Mask)
    {
        Masked = true;
        this.Mask = Mask;
        return this;
    }

    /// <summary>Run the query and return the closest hit, or null.</summary>
    public RaycastHit? Run()
    {
        FVector3 Delta = To - From;
        float Distance = Delta.Length;
        if (Distance <= 0.0f)
        {
            return null;
        }
        FVector3 Direction = Delta.Normalized();
        Physics Physics = World.Physics;
        Entity? Ignore = IgnoreId == Entity.Null.Id ? null : new Entity(IgnoreId);
        if (Radius > 0.0f)
        {
            RaycastHit[] Hits = Physics.SphereCast(From, Direction, Distance, Radius, Ignore);
            return Hits.Length > 0 ? Hits[0] : null;
        }
        if (Masked)
        {
            return Physics.RaycastFiltered(From, Direction, Distance, Mask, Ignore);
        }
        return Physics.Raycast(From, Direction, Distance, Ignore);
    }

    /// <summary>Run the query and return every hit, near to far.</summary>
    public RaycastHit[] RunAll()
    {
        FVector3 Delta = To - From;
        float Distance = Delta.Length;
        if (Distance <= 0.0f)
        {
            return Array.Empty<RaycastHit>();
        }
        FVector3 Direction = Delta.Normalized();
        Physics Physics = World.Physics;
        Entity? Ignore = IgnoreId == Entity.Null.Id ? null : new Entity(IgnoreId);
        if (Radius > 0.0f)
        {
            return Physics.SphereCast(From, Direction, Distance, Radius, Ignore);
        }
        if (Masked)
        {
            return Physics.RaycastAllFiltered(From, Direction, Distance, Mask, Ignore);
        }
        return Physics.RaycastAll(From, Direction, Distance, Ignore);
    }
}
