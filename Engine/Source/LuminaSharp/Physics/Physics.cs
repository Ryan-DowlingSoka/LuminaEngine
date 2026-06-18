using System;
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

    /// <summary>Largest overlap/sweep result set a single allocating query returns (extras are dropped).</summary>
    public const int MaxQueryResults = 256;

    /// <summary>
    /// All distinct entities whose physics bodies overlap a sphere at <paramref name="Center"/> -- the core
    /// AI-perception / area-of-effect / trigger primitive. Pass <paramref name="Ignore"/> to exclude one
    /// entity (typically the querier). Capped at <see cref="MaxQueryResults"/>.
    /// </summary>
    public Entity[] OverlapSphere(FVector3 Center, float Radius, Entity? Ignore = null)
    {
        Span<uint> Buffer = stackalloc uint[MaxQueryResults];
        int Count = OverlapSphereRaw(Center, Radius, Ignore?.Id ?? Entity.Null.Id, Buffer);
        return ToEntities(Buffer, Count);
    }

    /// <summary>Writes overlapping entities into a caller buffer; returns the count written. Allocation-free.</summary>
    public int OverlapSphere(FVector3 Center, float Radius, Span<uint> Results, Entity? Ignore = null)
        => OverlapSphereRaw(Center, Radius, Ignore?.Id ?? Entity.Null.Id, Results);

    /// <summary>All distinct entities whose bodies overlap an oriented box. See <see cref="OverlapSphere"/>.</summary>
    public Entity[] OverlapBox(FVector3 Center, FVector3 HalfExtents, FQuat Rotation, Entity? Ignore = null)
    {
        Span<uint> Buffer = stackalloc uint[MaxQueryResults];
        int Count = OverlapBoxRaw(Center, HalfExtents, Rotation, Ignore?.Id ?? Entity.Null.Id, Buffer);
        return ToEntities(Buffer, Count);
    }

    /// <summary>Axis-aligned box overlap (identity rotation).</summary>
    public Entity[] OverlapBox(FVector3 Center, FVector3 HalfExtents, Entity? Ignore = null)
        => OverlapBox(Center, HalfExtents, FQuat.Identity, Ignore);

    /// <summary>
    /// Sweeps a sphere of <paramref name="Radius"/> from <paramref name="Origin"/> along
    /// <paramref name="Direction"/> for <paramref name="Distance"/>; returns every hit sorted near-to-far
    /// (thick raycast). Pass <paramref name="Ignore"/> to exclude one entity's body.
    /// </summary>
    public RaycastHit[] SphereCast(FVector3 Origin, FVector3 Direction, float Distance, float Radius, Entity? Ignore = null)
    {
        FVector3 End = Origin + Direction.Normalized() * Distance;
        Span<RaycastHitWire> Buffer = stackalloc RaycastHitWire[MaxQueryResults];
        int Count = SphereCastRaw(Origin, End, Radius, Ignore?.Id ?? Entity.Null.Id, Buffer);

        RaycastHit[] Out = new RaycastHit[Count];
        for (int i = 0; i < Count; ++i)
        {
            RaycastHitWire W = Buffer[i];
            Out[i] = new RaycastHit(new Entity(W.Entity), W.BodyId, W.Point, W.Normal, W.Distance, W.Fraction);
        }
        return Out;
    }

    /// <summary>
    /// Every body a ray crosses, sorted near-to-far -- a penetrating line trace (bullets through multiple
    /// targets, "what's behind this wall"). Unlike <see cref="Raycast"/> (closest hit only), this returns
    /// one entry per body along the ray. Pass <paramref name="Ignore"/> to exclude one entity's body.
    /// Capped at <see cref="MaxQueryResults"/>.
    /// </summary>
    public RaycastHit[] RaycastAll(FVector3 Origin, FVector3 Direction, float Distance, Entity? Ignore = null)
    {
        FVector3 End = Origin + Direction.Normalized() * Distance;
        Span<RaycastHitWire> Buffer = stackalloc RaycastHitWire[MaxQueryResults];
        int Count = RaycastAllRaw(Origin, End, Ignore?.Id ?? Entity.Null.Id, Buffer);

        RaycastHit[] Out = new RaycastHit[Count];
        for (int i = 0; i < Count; ++i)
        {
            RaycastHitWire W = Buffer[i];
            Out[i] = new RaycastHit(new Entity(W.Entity), W.BodyId, W.Point, W.Normal, W.Distance, W.Fraction);
        }
        return Out;
    }

    /// <summary>
    /// All distinct entities whose physics bodies CONTAIN <paramref name="Point"/> -- volume containment
    /// ("am I inside this trigger / water / zone") without sweeping a shape. Pass <paramref name="Ignore"/>
    /// to exclude one entity. Capped at <see cref="MaxQueryResults"/>.
    /// </summary>
    public Entity[] OverlapPoint(FVector3 Point, Entity? Ignore = null)
    {
        Span<uint> Buffer = stackalloc uint[MaxQueryResults];
        int Count = OverlapPointRaw(Point, Ignore?.Id ?? Entity.Null.Id, Buffer);
        return ToEntities(Buffer, Count);
    }

    private static Entity[] ToEntities(Span<uint> Ids, int Count)
    {
        Entity[] Out = new Entity[Count];
        for (int i = 0; i < Count; ++i)
        {
            Out[i] = new Entity(Ids[i]);
        }
        return Out;
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

    /// <summary>True if the entity's body is awake (active); false if asleep or it has no body. Cheap to
    /// poll each frame -- skip expensive work for bodies at rest. See also EntityScript OnWake/OnSleep.</summary>
    public bool IsAwake(Entity Entity) => IsAwakeRaw(Entity.Id) != 0;

    // --- Constraints / joints ---
    // Each Create* returns an FPhysicsConstraint handle: drive its motor, query break state, or Destroy it.
    // Both bodies need a rigid body; pass null for one side to anchor the joint to the world. All frames are
    // world-space. Optional BreakForce > 0 disables the joint once the force holding it exceeds that many N.

    /// <summary>Welds two bodies (or a body to the world) rigidly at their current relative pose.</summary>
    public FPhysicsConstraint CreateFixed(Entity? BodyA, Entity? BodyB, float BreakForce = 0f)
    {
        FConstraintDescWire W = NewDesc(FPhysicsConstraint.TypeFixed, BodyA, BodyB);
        W.BreakForce = BreakForce;
        return new FPhysicsConstraint(Handle, CreateConstraintRaw(W));
    }

    /// <summary>Ball-and-socket joint pinned at <paramref name="Pivot"/> (3 translation DOF removed).</summary>
    public FPhysicsConstraint CreatePoint(Entity? BodyA, Entity? BodyB, FVector3 Pivot, float BreakForce = 0f)
    {
        FConstraintDescWire W = NewDesc(FPhysicsConstraint.TypePoint, BodyA, BodyB);
        W.Anchor = Pivot;
        W.BreakForce = BreakForce;
        return new FPhysicsConstraint(Handle, CreateConstraintRaw(W));
    }

    /// <summary>Keeps <paramref name="PointA"/> on body A and <paramref name="PointB"/> on body B between
    /// <paramref name="MinDistance"/>..<paramref name="MaxDistance"/> apart (negative = current distance).
    /// A positive <paramref name="Frequency"/> makes the limit a soft spring.</summary>
    public FPhysicsConstraint CreateDistance(Entity? BodyA, Entity? BodyB, FVector3 PointA, FVector3 PointB,
        float MinDistance = -1f, float MaxDistance = -1f, float Frequency = 0f, float Damping = 0f, float BreakForce = 0f)
    {
        FConstraintDescWire W = NewDesc(FPhysicsConstraint.TypeDistance, BodyA, BodyB);
        W.Anchor = PointA;
        W.AnchorB = PointB;
        W.MinLimit = MinDistance;
        W.MaxLimit = MaxDistance;
        W.HasLimits = (MinDistance >= 0f || MaxDistance >= 0f) ? 1 : 0;
        W.LimitFrequency = Frequency;
        W.LimitDamping = Damping;
        W.BreakForce = BreakForce;
        return new FPhysicsConstraint(Handle, CreateConstraintRaw(W));
    }

    /// <summary>Single-axis hinge at <paramref name="Pivot"/> about <paramref name="Axis"/> (door / wheel /
    /// lever). Set <paramref name="Limited"/> with min/max angles (radians) to clamp the swing. Configure a
    /// powered hinge with <paramref name="MotorTorqueLimit"/> then drive it via the returned handle.</summary>
    public FPhysicsConstraint CreateHinge(Entity? BodyA, Entity? BodyB, FVector3 Pivot, FVector3 Axis,
        float MinAngle = 0f, float MaxAngle = 0f, bool Limited = false, float MaxFrictionTorque = 0f,
        float MotorTorqueLimit = 0f, float MotorFrequency = 0f, float MotorDamping = 0f, float BreakForce = 0f)
    {
        FConstraintDescWire W = NewDesc(FPhysicsConstraint.TypeHinge, BodyA, BodyB);
        W.Anchor = Pivot;
        W.Axis = Axis;
        W.MinLimit = MinAngle;
        W.MaxLimit = MaxAngle;
        W.HasLimits = Limited ? 1 : 0;
        W.MaxFriction = MaxFrictionTorque;
        W.MotorTorqueLimit = MotorTorqueLimit;
        W.MotorFrequency = MotorFrequency;
        W.MotorDamping = MotorDamping;
        W.BreakForce = BreakForce;
        return new FPhysicsConstraint(Handle, CreateConstraintRaw(W));
    }

    /// <summary>Prismatic (slider) joint allowing motion along <paramref name="Axis"/> only (piston / drawer
    /// / lift). Set <paramref name="Limited"/> with min/max positions (meters) to clamp travel; configure a
    /// powered slider with <paramref name="MotorForceLimit"/> then drive it via the returned handle.</summary>
    public FPhysicsConstraint CreateSlider(Entity? BodyA, Entity? BodyB, FVector3 Pivot, FVector3 Axis,
        float MinDistance = 0f, float MaxDistance = 0f, bool Limited = false, float MaxFrictionForce = 0f,
        float MotorForceLimit = 0f, float MotorFrequency = 0f, float MotorDamping = 0f, float BreakForce = 0f)
    {
        FConstraintDescWire W = NewDesc(FPhysicsConstraint.TypeSlider, BodyA, BodyB);
        W.Anchor = Pivot;
        W.Axis = Axis;
        W.MinLimit = MinDistance;
        W.MaxLimit = MaxDistance;
        W.HasLimits = Limited ? 1 : 0;
        W.MaxFriction = MaxFrictionForce;
        W.MotorForceLimit = MotorForceLimit;
        W.MotorFrequency = MotorFrequency;
        W.MotorDamping = MotorDamping;
        W.BreakForce = BreakForce;
        return new FPhysicsConstraint(Handle, CreateConstraintRaw(W));
    }

    /// <summary>Cone (swing-limited ball-socket): keeps body B's <paramref name="TwistAxis"/> within
    /// <paramref name="HalfAngleRadians"/> of body A's, pinned at <paramref name="Pivot"/>.</summary>
    public FPhysicsConstraint CreateCone(Entity? BodyA, Entity? BodyB, FVector3 Pivot, FVector3 TwistAxis,
        float HalfAngleRadians, float BreakForce = 0f)
    {
        FConstraintDescWire W = NewDesc(FPhysicsConstraint.TypeCone, BodyA, BodyB);
        W.Anchor = Pivot;
        W.Axis = TwistAxis;
        W.HalfConeAngle = HalfAngleRadians;
        W.BreakForce = BreakForce;
        return new FPhysicsConstraint(Handle, CreateConstraintRaw(W));
    }

    /// <summary>Make this entity's collider act as a conveyor: bodies resting on it are dragged at
    /// <paramref name="Linear"/> (world m/s) without the body moving. Needs a rigid body. Zero clears it.</summary>
    public void SetSurfaceVelocity(Entity Entity, FVector3 Linear) => SetSurfaceVelocity(Entity.Id, Linear, default);

    /// <summary>Conveyor surface velocity with an angular component (rad/s about the body center).</summary>
    public void SetSurfaceVelocity(Entity Entity, FVector3 Linear, FVector3 Angular) => SetSurfaceVelocity(Entity.Id, Linear, Angular);

    /// <summary>Stop this entity behaving as a conveyor.</summary>
    public void ClearSurfaceVelocity(Entity Entity) => SetSurfaceVelocity(Entity.Id, default, default);

    private FConstraintDescWire NewDesc(int Type, Entity? BodyA, Entity? BodyB)
    {
        return new FConstraintDescWire
        {
            Type = Type,
            BodyA = BodyA?.Id ?? Entity.Null.Id,
            BodyB = BodyB?.Id ?? Entity.Null.Id,
            Axis = new FVector3(0f, 1f, 0f),
        };
    }

    // Flat shims (Runtime module). The world Handle is the first native argument; entities are entt ids
    // (uint); FVector3/FVector4/FQuat pass by value. See DotNetGameplay.cpp.

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_Raycast")]
    private partial RaycastHitWire RaycastWire(FVector3 Origin, FVector3 End, uint IgnoreId);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_OverlapSphere")]
    private partial int OverlapSphereRaw(FVector3 Center, float Radius, uint IgnoreId, Span<uint> Results);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_OverlapBox")]
    private partial int OverlapBoxRaw(FVector3 Center, FVector3 HalfExtents, FQuat Rotation, uint IgnoreId, Span<uint> Results);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_SphereCast")]
    private partial int SphereCastRaw(FVector3 Start, FVector3 End, float Radius, uint IgnoreId, Span<RaycastHitWire> Hits);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_RaycastAll")]
    private partial int RaycastAllRaw(FVector3 Start, FVector3 End, uint IgnoreId, Span<RaycastHitWire> Hits);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_OverlapPoint")]
    private partial int OverlapPointRaw(FVector3 Point, uint IgnoreId, Span<uint> Results);

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

    // Constraints. CreateConstraint takes the blittable FConstraintDescWire by value; the others key off the
    // opaque constraint id CreateConstraint returned. Called for you by FPhysicsConstraint / the Create* API.
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_CreateConstraint")]
    private partial uint CreateConstraintRaw(FConstraintDescWire Desc);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_DestroyConstraint")]
    internal partial void DestroyConstraintRaw(uint ConstraintId);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_SetConstraintEnabled")]
    internal partial void SetConstraintEnabledRaw(uint ConstraintId, int Enabled);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_SetConstraintMotor")]
    internal partial void SetConstraintMotorRaw(uint ConstraintId, int Mode, float Target);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_IsConstraintBroken")]
    internal partial int IsConstraintBrokenRaw(uint ConstraintId);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_GetConstraintValue")]
    internal partial float GetConstraintValueRaw(uint ConstraintId);
    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_IsAwake")]
    private partial int IsAwakeRaw(uint Entity);

    [NativeCall(Module = "Runtime", EntryPoint = "LuminaSharp_Physics_SetSurfaceVelocity")]
    private partial void SetSurfaceVelocity(uint Entity, FVector3 Linear, FVector3 Angular);
}
