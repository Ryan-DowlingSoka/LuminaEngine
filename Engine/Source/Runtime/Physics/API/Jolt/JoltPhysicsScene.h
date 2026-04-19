#pragma once

#include "Jolt/Physics/PhysicsSystem.h"
#include "Memory/SmartPtr.h"
#include "Physics/PhysicsScene.h"
#include "World/Entity/Events/ImpulseEvent.h"


namespace Lumina
{
	struct SImpulseEvent;
	class CWorld;
}

namespace Lumina::Physics
{
	class FJoltContactListener : public JPH::ContactListener
	{
	public:
		FJoltContactListener(entt::dispatcher& InDispatcher, const JPH::BodyLockInterfaceNoLock* InBodyLockInterface)
			: EventDispatcher(InDispatcher)
			, BodyLockInterface(InBodyLockInterface)
		{ }

		/// Called after detecting a collision between a body pair, but before calling OnContactAdded and before adding the contact constraint.
		/// If the function returns false, the contact will not be added and any other contacts between this body pair will not be processed.
		/// This function will only be called once per PhysicsSystem::Update per body pair and may not be called again the next update
		/// if a contact persists and no new contact pairs between sub shapes are found.
		/// This is a rather expensive time to reject a contact point since a lot of the collision detection has happened already, make sure you
		/// filter out the majority of undesired body pairs through the ObjectLayerPairFilter that is registered on the PhysicsSystem.
		/// Note that this callback is called when all bodies are locked, so don't use any locking functions!
		/// The order of body 1 and 2 is undefined, but when one of the two bodies is dynamic it will be body 1
		virtual JPH::ValidateResult	OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::CollideShapeResult& inCollisionResult) { return JPH::ValidateResult::AcceptAllContactsForThisBodyPair; }

		/// Called whenever a new contact point is detected.
		/// Note that this callback is called when all bodies are locked, so don't use any locking functions!
		/// Body 1 and 2 will be sorted such that body 1 ID < body 2 ID, so body 1 may not be dynamic.
		/// Note that only active bodies will report contacts, as soon as a body goes to sleep the contacts between that body and all other
		/// bodies will receive an OnContactRemoved callback, if this is the case then Body::IsActive() will return false during the callback.
		/// When contacts are added, the constraint solver has not run yet, so the collision impulse is unknown at that point.
		/// The velocities of inBody1 and inBody2 are the velocities before the contact has been resolved, so you can use this to
		/// estimate the collision impulse to e.g. determine the volume of the impact sound to play.
		virtual void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings);

		/// Called whenever a contact is detected that was also detected last update.
		/// Note that this callback is called when all bodies are locked, so don't use any locking functions!
		/// Body 1 and 2 will be sorted such that body 1 ID < body 2 ID, so body 1 may not be dynamic.
		virtual void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings);

		/// Called whenever a contact was detected last update but is not detected anymore.
		/// Note that this callback is called when all bodies are locked, so don't use any locking functions!
		/// Note that we're using BodyID's since the bodies may have been removed at the time of callback.
		/// Body 1 and 2 will be sorted such that body 1 ID < body 2 ID, so body 1 may not be dynamic.
		virtual void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair);
    	
	private:
		void OverrideFrictionAndRestitution(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings);
		void GetFrictionAndRestitution(const JPH::Body& inBody, const JPH::SubShapeID& inSubShapeID, float& outFriction, float& outRestitution) const;

	private:
		
		entt::dispatcher& EventDispatcher;
		const JPH::BodyLockInterfaceNoLock* BodyLockInterface = nullptr;
	};
    
    class FJoltPhysicsScene : public IPhysicsScene
    {
    public:

        FJoltPhysicsScene(CWorld* InWorld);
        ~FJoltPhysicsScene() override;
    	
    	LE_NO_COPYMOVE(FJoltPhysicsScene);


        void PreUpdate() override;
        void Update(double DeltaTime) override;
        void PostUpdate() override;
        void Simulate() override;
        void StopSimulate() override;

    	void ActivateBody(uint32 BodyID) override;
    	void DeactivateBody(uint32 BodyID) override;
    	void ChangeBodyMotionType(uint32 BodyID, EBodyType NewType) override;

    	// Push game-thread transform edits into Jolt bodies once per frame,
    	// before the fixed-step loop.
    	void ApplyDirtyTransforms(float FixedDt);

    	// Step every CharacterVirtual on the fixed timestep, alongside the
    	// Jolt rigid-body update.
    	void UpdateCharacters(float FixedDt);

    	// Pull per-frame controller input into SCharacterMovementComponent's
    	// pending fields. Runs once per Update() so every substep in the frame
    	// observes the same input.
    	void LatchCharacterInput();
    	
    	void SyncPhysicsTransforms(float Alpha);
    	
    	uint32 GetEntityBodyID(entt::entity Entity) override;
    	
    	TOptional<SRayResult> CastRay(const SRayCastSettings& Settings) override;
		TVector<SRayResult> CastSphere(const SSphereCastSettings& Settings) override;
    	
    	void OnCharacterComponentConstructed(entt::registry& Registry, entt::entity Entity);
    	void OnCharacterComponentDestroyed(entt::registry& Registry, entt::entity Entity);
    	
    	void OnRigidBodyComponentUpdated(entt::registry& Registry, entt::entity Entity);
    	void OnRigidBodyComponentConstructed(entt::registry& Registry, entt::entity Entity);
    	void OnRigidBodyComponentDestroyed(entt::registry& Registry, entt::entity Entity);
    	void OnColliderComponentAdded(entt::registry& Registry, entt::entity Entity);
    	void OnColliderComponentRemoved(entt::registry& Registry, entt::entity Entity);
    	
    	void OnImpulseEvent(const SImpulseEvent& Impulse) override;
        void OnForceEvent(const SForceEvent& Force) override;
        void OnTorqueEvent(const STorqueEvent& Torque) override;
        void OnAngularImpulseEvent(const SAngularImpulseEvent& AngularImpulse) override;
        void OnSetVelocityEvent(const SSetVelocityEvent& Velocity) override;
        void OnSetAngularVelocityEvent(const SSetAngularVelocityEvent& AngularVelocity) override;
        void OnAddImpulseAtPositionEvent(const SAddImpulseAtPositionEvent& Event) override;
        void OnAddForceAtPositionEvent(const SAddForceAtPositionEvent& Event) override;
        void OnSetGravityFactorEvent(const SSetGravityFactorEvent& Event) override;
    	
    	JPH::PhysicsSystem* GetPhysicsSystem() const { return JoltSystem.get(); }

    private:

    	// Snapshot all active body positions before stepping, used as the
    	// interpolation "previous" endpoint in SyncPhysicsTransforms.
    	void SnapshotBodyStates();

    	TQueue<entt::entity>				PendingRigidBodyCreations;
    	JPH::TempAllocatorImpl				Allocator;
    	TUniquePtr<FJoltContactListener>	ContactListener;
        TUniquePtr<JPH::PhysicsSystem>		JoltSystem;
        CWorld*								World = nullptr;

    	float	Accumulator = 0.0f;
    	uint32	CollisionSteps = 0;
    };
}
