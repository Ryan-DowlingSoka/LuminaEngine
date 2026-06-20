namespace LuminaSharp;

/// <summary>The physics phase an <see cref="EntityScript"/>'s OnUpdate runs in: PrePhysics (default) before the
/// physics step, PostPhysics after (read settled results, e.g. follow cameras).</summary>
public enum EScriptPhase
{
    PrePhysics,
    PostPhysics,
}

/// <summary>Declares which physics phase a script's OnUpdate runs in (default PrePhysics).</summary>
[System.AttributeUsage(System.AttributeTargets.Class, AllowMultiple = false, Inherited = true)]
public sealed class UpdatePhaseAttribute : System.Attribute
{
    public EScriptPhase Phase { get; }

    public UpdatePhaseAttribute(EScriptPhase Phase)
    {
        this.Phase = Phase;
    }
}

/// <summary>Base class for a script attached to a single entity.</summary>
public abstract class EntityScript
{
    internal TypeDescription Description = null!; // set at Create; cached labels + callback flags, no per-frame reflection

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

    /// <summary>This entity's transform, resolved once and cached (avoids a per-frame Get crossing + alloc).</summary>
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

    /// <summary>Called at the fixed physics timestep (0..N times/frame, before physics) for framerate-independent
    /// physics logic. <paramref name="FixedDeltaTime"/> is the fixed step (1 / physics Hz), not the frame delta.</summary>
    public virtual void OnFixedUpdate(float FixedDeltaTime)
    {
    }

    /// <summary>Called per discrete input event while the entity is receiving input (needs <see cref="EnableInput"/>).</summary>
    public virtual void OnInput(Lumina.InputEvent Event)
    {
    }

    /// <summary>Add (idempotent) and return this entity's SInputComponent so it can receive input. Call in OnReady.</summary>
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

    // Collision callbacks (after the physics step). Contact = solid impact; Overlap = sensor/trigger.

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

    // Physics sleep/wake callbacks (OnWake also fires on spawn).

    /// <summary>The entity's physics body just became active (woke up or spawned).</summary>
    public virtual void OnWake()
    {
    }

    /// <summary>The entity's physics body just went to sleep (came to rest).</summary>
    public virtual void OnSleep()
    {
    }

    // AI perception callbacks: this entity's SPerceptionComponent gained/lost awareness of another (self-oriented event).

    /// <summary>This entity first became aware of Event.Target (Event.Sense is the sense that detected it).</summary>
    public virtual void OnTargetPerceived(Lumina.SPerceptionEvent Event)
    {
    }

    /// <summary>This entity lost awareness of Event.Target (Event.Location is its last known position).</summary>
    public virtual void OnTargetLost(Lumina.SPerceptionEvent Event)
    {
    }
}
