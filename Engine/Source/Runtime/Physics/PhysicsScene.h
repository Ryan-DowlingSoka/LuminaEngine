#pragma once
#include "PhysicsTypes.h"
#include "Core/Templates/Optional.h"
#include "Ray/RayCast.h"
#include "World/Entity/Events/ImpulseEvent.h"

namespace Lumina::Physics
{
    class IPhysicsScene
    {
    public:

        virtual ~IPhysicsScene() { }
        virtual void PreUpdate() = 0;
        virtual void Update(double DeltaTime) = 0;
        virtual void PostUpdate() = 0;
        virtual void Simulate() = 0;
        virtual void StopSimulate() = 0;

        // Game-thread drain of step-side events (Lua, entt::dispatcher). Pair with Update.
        virtual void DispatchPendingEvents() {}
        
        virtual void DeactivateBody(uint32 BodyID) = 0;
        virtual void ActivateBody(uint32 BodyID) = 0;
        virtual void ChangeBodyMotionType(uint32 BodyID, EBodyType NewType) = 0;
        
        virtual uint32 GetEntityBodyID(entt::entity Entity) = 0;
        
        virtual TOptional<SRayResult> CastRay(const SRayCastSettings& Settings) = 0;
        virtual TVector<SRayResult> CastSphere(const SSphereCastSettings& Settings) = 0;
        
        virtual void OnImpulseEvent(const SImpulseEvent& Impulse) = 0;
        virtual void OnForceEvent(const SForceEvent& Force) = 0;
        virtual void OnTorqueEvent(const STorqueEvent& Torque) = 0;
        virtual void OnAngularImpulseEvent(const SAngularImpulseEvent& AngularImpulse) = 0;
        virtual void OnSetVelocityEvent(const SSetVelocityEvent& Velocity) = 0;
        virtual void OnSetAngularVelocityEvent(const SSetAngularVelocityEvent& AngularVelocity) = 0;
        virtual void OnAddImpulseAtPositionEvent(const SAddImpulseAtPositionEvent& Event) = 0;
        virtual void OnAddForceAtPositionEvent(const SAddForceAtPositionEvent& Event) = 0;
        virtual void OnSetGravityFactorEvent(const SSetGravityFactorEvent& Event) = 0;
        
        virtual FVector3 GetVelocityAtPoint(uint32 BodyID, const FVector3& Point) = 0;
        virtual FVector3 GetLinearVelocity(uint32 BodyID) = 0;
        virtual FVector3 GetAngularVelocity(uint32 BodyID) = 0;
        virtual FVector3 GetCenterOfMass(uint32 BodyID)= 0;

        // Actual current body pose, NOT the interpolated render transform (STransformComponent is lagged).
        virtual FVector3 GetBodyPosition(uint32 BodyID) = 0;
        virtual FQuat GetBodyRotation(uint32 BodyID) = 0;

        /** Current live body count and the configured ceiling. Lets bulk spawners (fracture) clamp to capacity instead of overflowing Jolt's body buffer. */
        virtual uint32 GetBodyCount() = 0;
        virtual uint32 GetMaxBodyCount() = 0;

        // Between Begin/End, body constructions are queued and inserted by End in one AddBodiesPrepare/Finalize.
        // Game-thread only, must be balanced; BodyIDs are valid after EndBodyBatch.
        virtual void BeginBodyBatch() = 0;
        virtual void EndBodyBatch() = 0;

        // ---- Entity-based facet API (script-facing) -------------------------------------------
        // Non-virtual conveniences over the body-id interface above: resolve the entity's body,
        // then apply or read. Commands fill a parameter POD and call the handler directly (safe --
        // scripts never overlap the step). Reads return the latched snapshot. No-op if the entity
        // has no body. This is the single method set behind self.Physics and World.Physics.
        void AddForce(entt::entity E, const FVector3& Force)                   { SForceEvent Ev; Ev.BodyID = GetEntityBodyID(E); Ev.Force = Force; OnForceEvent(Ev); }
        void AddImpulse(entt::entity E, const FVector3& Impulse)               { SImpulseEvent Ev; Ev.BodyID = GetEntityBodyID(E); Ev.Impulse = Impulse; OnImpulseEvent(Ev); }
        void AddTorque(entt::entity E, const FVector3& Torque)                 { STorqueEvent Ev; Ev.BodyID = GetEntityBodyID(E); Ev.Torque = Torque; OnTorqueEvent(Ev); }
        void AddAngularImpulse(entt::entity E, const FVector3& AngularImpulse) { SAngularImpulseEvent Ev; Ev.BodyID = GetEntityBodyID(E); Ev.AngularImpulse = AngularImpulse; OnAngularImpulseEvent(Ev); }
        void AddForceAtPosition(entt::entity E, const FVector3& Force, const FVector3& Position)     { SAddForceAtPositionEvent Ev; Ev.BodyID = GetEntityBodyID(E); Ev.Force = Force; Ev.Position = Position; OnAddForceAtPositionEvent(Ev); }
        void AddImpulseAtPosition(entt::entity E, const FVector3& Impulse, const FVector3& Position) { SAddImpulseAtPositionEvent Ev; Ev.BodyID = GetEntityBodyID(E); Ev.Impulse = Impulse; Ev.Position = Position; OnAddImpulseAtPositionEvent(Ev); }
        void SetLinearVelocity(entt::entity E, const FVector3& Velocity)         { SSetVelocityEvent Ev; Ev.BodyID = GetEntityBodyID(E); Ev.Velocity = Velocity; OnSetVelocityEvent(Ev); }
        void SetAngularVelocity(entt::entity E, const FVector3& AngularVelocity) { SSetAngularVelocityEvent Ev; Ev.BodyID = GetEntityBodyID(E); Ev.AngularVelocity = AngularVelocity; OnSetAngularVelocityEvent(Ev); }
        void SetGravityFactor(entt::entity E, float Factor)                     { SSetGravityFactorEvent Ev; Ev.BodyID = GetEntityBodyID(E); Ev.GravityFactor = Factor; OnSetGravityFactorEvent(Ev); }

        FVector3 GetLinearVelocity(entt::entity E)                         { return GetLinearVelocity(GetEntityBodyID(E)); }
        FVector3 GetAngularVelocity(entt::entity E)                        { return GetAngularVelocity(GetEntityBodyID(E)); }
        FVector3 GetVelocityAtPoint(entt::entity E, const FVector3& Point) { return GetVelocityAtPoint(GetEntityBodyID(E), Point); }
        FVector3 GetCenterOfMass(entt::entity E)                           { return GetCenterOfMass(GetEntityBodyID(E)); }
        FVector3 GetBodyPosition(entt::entity E)                           { return GetBodyPosition(GetEntityBodyID(E)); }
        FQuat    GetBodyRotation(entt::entity E)                           { return GetBodyRotation(GetEntityBodyID(E)); }
    };
}
