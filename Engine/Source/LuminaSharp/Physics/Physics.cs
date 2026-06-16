using Lumina;

namespace LuminaSharp;

/// <summary>
/// A world's physics interface (<c>World.Physics</c>), the C# mirror of Lua's <c>World.Physics:*</c> facade.
/// Forces, velocities, and queries are keyed by <see cref="Entity"/>; the native scene resolves the rigid
/// body. Operations on an entity without a physics body are safe (mutators no-op, getters return zero).
/// Game thread only. Every member is a <c>[NativeCall] partial</c> forwarding to a flat
/// <c>LuminaSharp_Physics_*</c> shim in the Runtime module (DotNetGameplay.cpp), with the world Handle
/// passed first.
/// </summary>
public readonly unsafe partial struct Physics
{
    internal readonly ulong Handle;

    internal Physics(ulong Handle)
    {
        this.Handle = Handle;
    }

    public bool IsValid => Handle != 0;

    /// <summary>
    /// Casts a ray of length <paramref name="Distance"/> from <paramref name="Origin"/> along
    /// <paramref name="Direction"/> (which is normalized internally); returns the closest hit, or null.
    /// Pass <paramref name="Ignore"/> to exclude one entity's body (typically the caster's own).
    /// </summary>
    public RaycastHit? Raycast(FVector3 Origin, FVector3 Direction, float Distance, Entity? Ignore = null)
    {
        FVector3 End = Origin + Direction.Normalized() * Distance;
        uint IgnoreId = Ignore?.Id ?? Entity.Null.Id;
        RaycastHitWire Result = RaycastWire(Origin, End, IgnoreId);
        if (Result.Hit == 0)
        {
            return null;
        }
        return new RaycastHit(new Entity(Result.Entity), Result.BodyId, Result.Point, Result.Normal, Result.Distance, Result.Fraction);
    }

    public void AddForce(Entity Entity, FVector3 Force) => AddForce(Entity.Id, Force);
    public void AddImpulse(Entity Entity, FVector3 Impulse) => AddImpulse(Entity.Id, Impulse);
    public void AddTorque(Entity Entity, FVector3 Torque) => AddTorque(Entity.Id, Torque);
    public void AddAngularImpulse(Entity Entity, FVector3 Impulse) => AddAngularImpulse(Entity.Id, Impulse);
    public void AddForceAtPosition(Entity Entity, FVector3 Force, FVector3 Position) => AddForceAtPosition(Entity.Id, Force, Position);
    public void AddImpulseAtPosition(Entity Entity, FVector3 Impulse, FVector3 Position) => AddImpulseAtPosition(Entity.Id, Impulse, Position);

    public FVector3 GetLinearVelocity(Entity Entity) => GetLinearVelocity(Entity.Id);
    public void SetLinearVelocity(Entity Entity, FVector3 Velocity) => SetLinearVelocity(Entity.Id, Velocity);
    public FVector3 GetAngularVelocity(Entity Entity) => GetAngularVelocity(Entity.Id);
    public void SetAngularVelocity(Entity Entity, FVector3 Velocity) => SetAngularVelocity(Entity.Id, Velocity);
    public FVector3 GetVelocityAtPoint(Entity Entity, FVector3 Point) => GetVelocityAtPoint(Entity.Id, Point);

    public FVector3 GetBodyPosition(Entity Entity) => GetBodyPosition(Entity.Id);
    public FQuat GetBodyRotation(Entity Entity) => GetBodyRotation(Entity.Id);
    public FVector3 GetCenterOfMass(Entity Entity) => GetCenterOfMass(Entity.Id);
    public void SetGravityFactor(Entity Entity, float Factor) => SetGravityFactor(Entity.Id, Factor);
    public void ActivateBody(Entity Entity) => ActivateBody(Entity.Id);
    public void DeactivateBody(Entity Entity) => DeactivateBody(Entity.Id);

    /// <summary>The entity's native body id, or 0xFFFFFFFF if it has no rigid body.</summary>
    public uint GetBodyId(Entity Entity) => GetBodyId(Entity.Id);

    // Flat shims (Runtime module). The world Handle is the first native argument; entities are entt ids
    // (uint); FVector3/FVector4/FQuat pass by value. See DotNetGameplay.cpp.

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_Raycast")]
    private partial RaycastHitWire RaycastWire(FVector3 Origin, FVector3 End, uint IgnoreId);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_AddForce")]
    private partial void AddForce(uint Entity, FVector3 Force);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_AddImpulse")]
    private partial void AddImpulse(uint Entity, FVector3 Impulse);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_AddTorque")]
    private partial void AddTorque(uint Entity, FVector3 Torque);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_AddAngularImpulse")]
    private partial void AddAngularImpulse(uint Entity, FVector3 Impulse);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_AddForceAtPosition")]
    private partial void AddForceAtPosition(uint Entity, FVector3 Force, FVector3 Position);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_AddImpulseAtPosition")]
    private partial void AddImpulseAtPosition(uint Entity, FVector3 Impulse, FVector3 Position);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_GetLinearVelocity")]
    private partial FVector3 GetLinearVelocity(uint Entity);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_SetLinearVelocity")]
    private partial void SetLinearVelocity(uint Entity, FVector3 Velocity);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_GetAngularVelocity")]
    private partial FVector3 GetAngularVelocity(uint Entity);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_SetAngularVelocity")]
    private partial void SetAngularVelocity(uint Entity, FVector3 Velocity);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_GetVelocityAtPoint")]
    private partial FVector3 GetVelocityAtPoint(uint Entity, FVector3 Point);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_GetBodyPosition")]
    private partial FVector3 GetBodyPosition(uint Entity);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_GetBodyRotation")]
    private partial FQuat GetBodyRotation(uint Entity);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_GetCenterOfMass")]
    private partial FVector3 GetCenterOfMass(uint Entity);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_SetGravityFactor")]
    private partial void SetGravityFactor(uint Entity, float Factor);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_ActivateBody")]
    private partial void ActivateBody(uint Entity);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_DeactivateBody")]
    private partial void DeactivateBody(uint Entity);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_GetBodyId")]
    private partial uint GetBodyId(uint Entity);
}
