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
        
        virtual glm::vec3 GetVelocityAtPoint(uint32 BodyID, const glm::vec3& Point) = 0;
        virtual glm::vec3 GetLinearVelocity(uint32 BodyID) = 0;
        virtual glm::vec3 GetAngularVelocity(uint32 BodyID) = 0;
        virtual glm::vec3 GetCenterOfMass(uint32 BodyID)= 0;

        // Body's actual current pose, NOT the interpolated render transform.
        // Use this when a script reads the body in PrePhysics and needs the
        // position physics will integrate from this frame -- the cached
        // STransformComponent is the lagged display value.
        virtual glm::vec3 GetBodyPosition(uint32 BodyID) = 0;
        virtual glm::quat GetBodyRotation(uint32 BodyID) = 0;

        /** Current live body count and the configured ceiling. Lets bulk spawners (fracture) clamp to capacity instead of overflowing Jolt's body buffer. */
        virtual uint32 GetBodyCount() = 0;
        virtual uint32 GetMaxBodyCount() = 0;

        /**
         * Batch body creation. Between Begin/End, rigid-body constructions (e.g. collider
         * on_construct during a fracture) are queued instead of created immediately; End builds
         * them all and inserts them with one AddBodiesPrepare/Finalize instead of N individual
         * AddBody calls. Game-thread only; must be balanced. BodyIDs are valid after EndBodyBatch.
         */
        virtual void BeginBodyBatch() = 0;
        virtual void EndBodyBatch() = 0;
    };
}
