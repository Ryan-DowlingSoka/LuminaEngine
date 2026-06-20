using System;
using Lumina;

namespace LuminaSharp;

/// Base for a world ECS system authored in C#: one instance per world, ticked per frame by the native stage scheduler. Declare stage/priority with EntitySystemAttribute or the type is not discovered.
public abstract class EntitySystem
{
    /// The world this system runs in.
    public Lumina.CWorld World { get; internal set; } = null!;

    /// Called once when the system is created for a world (before the first OnUpdate).
    public virtual void OnStartup(SystemContext Context)
    {
    }

    /// Called every frame on the system's stage while the world ticks.
    public virtual void OnUpdate(SystemContext Context)
    {
    }

    /// Called once when the world (or this system) is torn down.
    public virtual void OnTeardown(SystemContext Context)
    {
    }
}

/// Declares the update stage + priority for an EntitySystem (lower priority value runs first; 128 = default).
[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
public sealed class EntitySystemAttribute : Attribute
{
    /// The stage this system ticks in.
    public EUpdateStage Stage { get; set; } = EUpdateStage.PrePhysics;

    /// Tick priority within the stage (lower runs first; 128 = default).
    public int Priority { get; set; } = 128;
}

/// Base for Reads/Writes access declarations that let an EntitySystem run concurrently with non-conflicting systems; conflict is at the component-type level.
/// CONTRACT: a system declaring access must do synchronous compute over the declared components only, declare honestly (under-declaring races), and make no structural changes or blocking/parking calls (runs on a job-system fiber). A system declaring NO access runs exclusive.
public abstract class ComponentAccessAttribute : Attribute
{
    /// The component wrapper types this access covers.
    public Type[] Components { get; }

    protected ComponentAccessAttribute(params Type[] Components)
    {
        this.Components = Components ?? Array.Empty<Type>();
    }
}

/// Declares the component types an EntitySystem only READS; reads are concurrent, a reader serializes only against a writer of the same type. Stackable.
[AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = true)]
public sealed class ReadsAttribute : ComponentAccessAttribute
{
    public ReadsAttribute(params Type[] Components) : base(Components)
    {
    }
}

/// Declares the component types an EntitySystem WRITES; a writer serializes against any system reading or writing the same type. Stackable.
[AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = true)]
public sealed class WritesAttribute : ComponentAccessAttribute
{
    public WritesAttribute(params Type[] Components) : base(Components)
    {
    }
}
