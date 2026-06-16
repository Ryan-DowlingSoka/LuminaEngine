using System;
using Lumina;

namespace LuminaSharp;

/// <summary>
/// Base class for a world ECS system authored in C#. Unlike an <see cref="EntityScript"/> (one instance
/// per entity), one EntitySystem instance is created per world and ticked once per frame by the native
/// stage scheduler, exactly like a native C++ system. The instance is the strong GCHandle the native
/// FStageSlot stores as its <c>Self</c>; the shared native shim forwards the tick to <see cref="OnUpdate"/>.
///
/// The stage + priority are declared with <see cref="EntitySystemAttribute"/>; without it the type is
/// not discovered as a system.
/// </summary>
public abstract class EntitySystem
{
    /// <summary>The world this system runs in.</summary>
    public Lumina.CWorld World { get; internal set; } = null!;

    /// <summary>Called once when the system is created for a world (before the first OnUpdate).</summary>
    public virtual void OnStartup(SystemContext Context)
    {
    }

    /// <summary>Called every frame on the system's stage while the world ticks.</summary>
    public virtual void OnUpdate(SystemContext Context)
    {
    }

    /// <summary>Called once when the world (or this system) is torn down.</summary>
    public virtual void OnTeardown(SystemContext Context)
    {
    }
}

/// <summary>
/// Declares the update stage + priority for an <see cref="EntitySystem"/>. The native scheduler reads
/// these off the discovered type and schedules the managed instance into the matching stage. Priority
/// follows the engine convention (lower value = higher priority; 128 = default/Medium).
/// </summary>
[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
public sealed class EntitySystemAttribute : Attribute
{
    /// <summary>The stage this system ticks in.</summary>
    public EUpdateStage Stage { get; set; } = EUpdateStage.PrePhysics;

    /// <summary>Tick priority within the stage (lower runs first; 128 = default).</summary>
    public int Priority { get; set; } = 128;
}
