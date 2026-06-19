namespace LuminaSharp;

/// <summary>The physics phase an <see cref="EntityScript"/>'s <see cref="EntityScript.OnUpdate"/> runs in.
/// PrePhysics (the default) runs before the physics step, apply input, movement, forces. PostPhysics runs
/// after, read the settled physics results, e.g. follow cameras or sync visuals to bodies.</summary>
public enum EScriptPhase
{
    PrePhysics,
    PostPhysics,
}

/// <summary>Declares which physics phase an <see cref="EntityScript"/>'s OnUpdate runs in. Without it a script
/// updates in <see cref="EScriptPhase.PrePhysics"/>. Scripts are dispatched in two groups (pre- and post-physics)
/// so you can choose to act before or after the physics step:
/// <code>[UpdatePhase(EScriptPhase.PostPhysics)] public class FollowCamera : EntityScript { ... }</code></summary>
[System.AttributeUsage(System.AttributeTargets.Class, AllowMultiple = false, Inherited = true)]
public sealed class UpdatePhaseAttribute : System.Attribute
{
    public EScriptPhase Phase { get; }

    public UpdatePhaseAttribute(EScriptPhase Phase)
    {
        this.Phase = Phase;
    }
}

/// <summary>
/// Base class for a script attached to a single entity.
/// </summary>
public abstract class EntityScript
{
    /// <summary>This script's entity (mirrors C++ entt::entity).</summary>
    public Entity Entity { get; internal set; }

    /// <summary>The world this script lives in.</summary>
    public Lumina.CWorld World { get; internal set; } = null!;

    /// <summary>The world's component store (mirrors C++ FEntityRegistry / entt::registry).</summary>
    public EntityRegistry Registry => World.Registry;

    private System.Threading.CancellationTokenSource? DestroyCts;

    /// <summary>Cancelled when this script is detached/destroyed. Pass to <see cref="GameTask"/> calls so a
    /// pending <c>await</c> stops cleanly when the entity goes away.</summary>
    protected System.Threading.CancellationToken DestroyToken => (DestroyCts ??= new System.Threading.CancellationTokenSource()).Token;

    internal void CancelDestroyToken()
    {
        if (DestroyCts != null)
        {
            DestroyCts.Cancel();
            DestroyCts.Dispose();
            DestroyCts = null;
        }
    }

    private Lumina.STransformComponent? CachedTransform;

    /// <summary>This entity's transform, resolved once and cached (every entity has one). Reusing the wrapper
    /// avoids a per-frame Registry.Get crossing and allocation.</summary>
    public Lumina.STransformComponent Transform => CachedTransform ??= Registry.Get<Lumina.STransformComponent>(Entity);

    /// <summary>Get the script of type T on another entity (or this one), or null.</summary>
    protected T? GetScript<T>(Entity Target) where T : EntityScript
    {
        return Registry.GetScript<T>(Target);
    }

    /// <summary>Called once when the script instance is attached to its entity.</summary>
    public virtual void OnAttach()
    {
    }

    /// <summary>Called once after OnAttach, before the first OnUpdate (all siblings are attached).</summary>
    public virtual void OnReady()
    {
    }

    /// <summary>Called every frame on the game thread while the owning entity is enabled.</summary>
    public virtual void OnUpdate(float DeltaTime)
    {
    }

    /// <summary>Called at the FIXED physics timestep (0..N times per frame, before the physics step), so it
    /// stays in step with the simulation and is framerate-independent. Use it for physics-affecting logic,
    /// applying forces/impulses, character movement. <paramref name="FixedDeltaTime"/> is the fixed step
    /// (1 / physics Hz), NOT the frame delta. Independent of <see cref="OnUpdate"/> (which runs once per frame).</summary>
    public virtual void OnFixedUpdate(float FixedDeltaTime)
    {
    }

    /// <summary>Called for each discrete input event (key/mouse) this frame while the entity is RECEIVING
    /// input. Override to LISTEN for input as it happens; or poll the SInputComponent in OnUpdate. Needs an
    /// enabled SInputComponent on the entity (see <see cref="EnableInput"/>).</summary>
    public virtual void OnInput(Lumina.InputEvent Event)
    {
    }

    /// <summary>Add an SInputComponent to this entity (idempotent) and return it, so the script can read
    /// input. Call in OnReady. Required both for OnInput to fire and for the SInputComponent poll queries
    /// (IsActionPressed, IsKeyDown, GetMouseDeltaX, ...).</summary>
    protected Lumina.SInputComponent EnableInput()
    {
        return Registry.Emplace<Lumina.SInputComponent>(Entity) ?? Registry.Get<Lumina.SInputComponent>(Entity);
    }

    /// <summary>Remove this entity's SInputComponent, stopping OnInput and the input queries.</summary>
    protected void DisableInput()
    {
        Registry.Remove<Lumina.SInputComponent>(Entity);
    }

    /// <summary>Called once when the script/entity is detached or destroyed.</summary>
    public virtual void OnDetach()
    {
    }

    // Collision callbacks (game thread, after the physics step). Contact = solid impact; Overlap = a
    // sensor/trigger on either side. The event is oriented for this entity.

    public virtual void OnContactBegin(Lumina.SCollisionEvent Event)
    {
    }

    public virtual void OnContactEnd(Lumina.SCollisionEvent Event)
    {
    }

    public virtual void OnOverlapBegin(Lumina.SCollisionEvent Event)
    {
    }

    public virtual void OnOverlapEnd(Lumina.SCollisionEvent Event)
    {
    }

    // Sleep/wake callbacks (game thread). The physics body went active (started moving / was disturbed) or
    // went to sleep (came to rest). Useful for AI/audio/VFX level-of-detail: only do expensive work for
    // awake bodies, or react to the moment something settles. OnWake also fires when the body first spawns.

    /// <summary>The entity's physics body just became active (woke up or spawned).</summary>
    public virtual void OnWake()
    {
    }

    /// <summary>The entity's physics body just went to sleep (came to rest).</summary>
    public virtual void OnSleep()
    {
    }

    // AI perception callbacks (game thread, during the world update). Fire when this entity's
    // SPerceptionComponent gains or loses awareness of another entity. The event is self-oriented
    // (Perceiver is this entity).

    /// <summary>This entity first became aware of Event.Target (Event.Sense is the sense that detected it).</summary>
    public virtual void OnTargetPerceived(Lumina.SPerceptionEvent Event)
    {
    }

    /// <summary>This entity lost awareness of Event.Target (Event.Location is its last known position).</summary>
    public virtual void OnTargetLost(Lumina.SPerceptionEvent Event)
    {
    }
}
