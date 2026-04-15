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
    };
}
