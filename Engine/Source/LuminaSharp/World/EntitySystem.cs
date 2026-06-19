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

/// <summary>
/// Base for the <see cref="ReadsAttribute"/>/<see cref="WritesAttribute"/> access declarations that let a
/// C# <see cref="EntitySystem"/> run CONCURRENTLY with other non-conflicting systems, exactly like a native
/// C++ system that declares an <c>FSystemAccess</c>. Each listed type is a component wrapper type whose name
/// matches its C++ CStruct (the same identity <c>Registry.View&lt;T&gt;</c> uses).
///
/// The native stage scheduler batches consecutive systems whose accesses do not conflict and ticks each
/// batch in parallel on the job system. Two systems conflict (and serialize) if their writes overlap, or
/// one writes a component the other reads. Conflict is at the COMPONENT-TYPE level: two systems writing the
/// same type serialize even on disjoint entities (conservative correctness).
///
/// CONTRACT for a system that declares any access (i.e. runs in parallel): its <see cref="EntitySystem.OnUpdate"/>
/// must be synchronous compute over the declared components only. Declare honestly — an under-declared write
/// races silently. Do NOT make structural changes (create/destroy entities, add/remove components) from a
/// system that declares access, and do NOT call blocking/parking work (Task.ParallelFor, await, long native
/// locks): the body runs on a job-system fiber and yielding it breaks CLR thread-affinity. A system that
/// declares NO access is treated as EXCLUSIVE (runs alone) — the safe default for structural or unknown work.
/// </summary>
public abstract class ComponentAccessAttribute : Attribute
{
    /// <summary>The component wrapper types this access covers.</summary>
    public Type[] Components { get; }

    protected ComponentAccessAttribute(params Type[] Components)
    {
        this.Components = Components ?? Array.Empty<Type>();
    }
}

/// <summary>Declares the component types an <see cref="EntitySystem"/> only READS. Reads of the same type by
/// several systems are concurrent; a reader serializes only against a writer of the same type. Stackable.</summary>
[AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = true)]
public sealed class ReadsAttribute : ComponentAccessAttribute
{
    public ReadsAttribute(params Type[] Components) : base(Components)
    {
    }
}

/// <summary>Declares the component types an <see cref="EntitySystem"/> WRITES (mutates in place). A writer
/// serializes against any other system reading or writing the same type. Stackable.</summary>
[AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = true)]
public sealed class WritesAttribute : ComponentAccessAttribute
{
    public WritesAttribute(params Type[] Components) : base(Components)
    {
    }
}
