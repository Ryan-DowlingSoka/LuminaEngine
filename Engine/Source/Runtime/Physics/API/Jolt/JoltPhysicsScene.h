#pragma once

// Jolt/Jolt.h must come first -- it sets up the JPH_* macros every other
// Jolt header expects, and was previously force-included via pch.h.
#include <Jolt/Jolt.h>
#include "Jolt/Physics/PhysicsSystem.h"
#include "Jolt/Physics/Collision/Shape/Shape.h"
#include "Containers/Array.h"
#include <cstring>
#include "Core/Threading/Thread.h"
#include "Memory/SmartPtr.h"
#include "Physics/PhysicsScene.h"
#include "World/Entity/Events/ImpulseEvent.h"


namespace Lumina
{
	struct SImpulseEvent;
	class CWorld;
	class CMesh;
}

namespace Lumina::Physics
{
	class FJoltPhysicsScene;

	enum class EContactEventType : uint8
	{
		Added,
		Removed,
	};

	// Contact snapshot recorded on the physics thread, drained game-thread; pre-resolves entity ids and
	// velocities so dispatch never touches a (possibly destroyed) Jolt body.
	struct FContactRecord
	{
		EContactEventType	Type;
		entt::entity		EntityA;
		entt::entity		EntityB;
		uint32				BodyIDA;
		uint32				BodyIDB;
		FVector3			Point;          // average manifold point (world space)
		FVector3			Normal;         // contact normal in world space (points from A toward B)
		FVector3			VelocityA;      // pre-step linear velocity of A
		FVector3			VelocityB;      // pre-step linear velocity of B
		float				ImpactSpeed;    // |relative velocity along normal|
		bool				bSensorA;
		bool				bSensorB;
	};

	class FJoltContactListener : public JPH::ContactListener
	{
	public:
		FJoltContactListener(FJoltPhysicsScene* InScene, entt::dispatcher& InDispatcher, const JPH::BodyLockInterfaceNoLock* InBodyLockInterface)
			: Scene(InScene)
			, EventDispatcher(InDispatcher)
			, BodyLockInterface(InBodyLockInterface)
		{ }

		// Jolt: reject a contact pair before it's added; called with all bodies locked, so no locking.
		virtual JPH::ValidateResult	OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::CollideShapeResult& inCollisionResult) { return JPH::ValidateResult::AcceptAllContactsForThisBodyPair; }

		// Jolt: new contact point; called with all bodies locked, so no locking. Velocities are pre-solve.
		virtual void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings);

		// Jolt: contact also present last update; called with all bodies locked, so no locking.
		virtual void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings);

		// Jolt: contact gone since last update; uses BodyIDs (bodies may be removed). Called locked, so no locking.
		virtual void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair);
    	
	private:
		void OverrideFrictionAndRestitution(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings);
		void GetFrictionAndRestitution(const JPH::Body& inBody, const JPH::SubShapeID& inSubShapeID, float& outFriction, float& outRestitution) const;

	private:

		FJoltPhysicsScene* Scene = nullptr;
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

        void DispatchPendingEvents() override;

    	void ActivateBody(uint32 BodyID) override;
    	void DeactivateBody(uint32 BodyID) override;
    	void ChangeBodyMotionType(uint32 BodyID, EBodyType NewType) override;

    	// Push game-thread transform edits into Jolt bodies once per frame,
    	// before the fixed-step loop.
    	void ApplyDirtyTransforms(float FixedDt);

    	// Step every CharacterVirtual on the fixed timestep, alongside the
    	// Jolt rigid-body update.
    	void UpdateCharacters(float FixedDt);

    	// Latch controller input into SCharacterMovementComponent once per Update() so all substeps see it.
    	void LatchCharacterInput();
    	
    	// Physics thread: stage interpolated transforms in InterpolatedTransforms; does NOT touch the registry.
    	void BuildInterpolatedTransforms(float Alpha);

    	// Game thread: write staged transforms into the ECS, resolve hierarchy, process kill-height destroys.
    	// Sole writer of physics-driven transforms, so it must run on the game thread.
    	void ApplyInterpolatedTransforms();
    	
    	uint32 GetEntityBodyID(entt::entity Entity) override;
    	
    	TOptional<SRayResult> CastRay(const SRayCastSettings& Settings) override;
		TVector<SRayResult> CastSphere(const SSphereCastSettings& Settings) override;
    	
    	// Lazily stands up the per-worker character-substep allocator pool on the first character;
    	// character-free worlds never allocate it.
    	void EnsureCharacterAllocators();
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
    	
    	FVector3 GetVelocityAtPoint(uint32 BodyID, const FVector3& Point) override;
    	FVector3 GetLinearVelocity(uint32 BodyID) override;
    	FVector3 GetAngularVelocity(uint32 BodyID) override;
    	FVector3 GetCenterOfMass(uint32 BodyID) override;
    	FVector3 GetBodyPosition(uint32 BodyID) override;
    	FQuat GetBodyRotation(uint32 BodyID) override;

    	uint32 GetBodyCount() override;
    	uint32 GetMaxBodyCount() override;

    	void BeginBodyBatch() override;
    	void EndBodyBatch() override;

    	JPH::PhysicsSystem* GetPhysicsSystem() const { return JoltSystem.get(); }
    	
    	void EnqueueContactRecord(const FContactRecord& Record);

    	// Shared collision shape, built on first request so N identical bodies share one JPH::Shape.
    	// Thread-safe: called from the parallel body build.
    	JPH::ShapeRefC GetOrCreateSphereShape(float Radius);
    	JPH::ShapeRefC GetOrCreateBoxShape(const FVector3& HalfExtent);
    	JPH::ShapeRefC GetOrCreateCapsuleShape(float Radius, float HalfHeight);
    	JPH::ShapeRefC GetOrCreateCylinderShape(float Radius, float HalfHeight, float CapRadius);

    	// Mesh-collider shape cached by mesh+scale+convexity so shards reuse one hull instead of re-running
    	// QuickHull; builds outside the lock so parallel distinct-mesh builds don't serialize.
    	JPH::ShapeRefC GetOrCreateMeshShape(const CMesh* Mesh, const FVector3& Scale, bool bConvex);

    	// Per-body material data consumed by the contact listener's OverrideFrictionAndRestitution.
    	struct FBodyMaterialEntry
    	{
    		float	Friction            = 0.0f;
    		float	Restitution         = 0.0f;
    		uint8	FrictionCombine     = 0;    // EPhysicsMaterialCombineMode
    		uint8	RestitutionCombine  = 0;
    		bool	bHasMaterial        = false;
    	};

    	// Side-table accessors for the contact listener. GetBodyMaterial is hot-path (called per
    	// contact pair during the physics step) and never resizes; Store/Clear run game-thread only.
    	void StoreBodyMaterial(JPH::BodyID BodyID, const struct FRigidBodyBuildResult& Build);
    	void ClearBodyMaterial(JPH::BodyID BodyID);
    	const FBodyMaterialEntry* GetBodyMaterial(JPH::BodyID BodyID) const;

    private:
    	
    	void DispatchContactEvents();
    	
    	void SnapshotBodyStates();
    	
    	void BulkCreateRigidBodies(entt::registry& Registry);

    	// Build the given entities' bodies in parallel, then insert them all with a single
    	// AddBodiesPrepare/Finalize. Game-thread only (outside JoltSystem->Update()).
    	void CreateRigidBodiesBatched(const TVector<entt::entity>& Entities);

    	// Build + add a single body; physics thread, outside Update(). on_construct enqueues, the drain calls this.
    	void CreateRigidBodyImmediate(entt::registry& Registry, entt::entity Entity);


    private:

    	// Shared-shape cache keyed by kind + dimensions (bit-exact float compare).
    	struct FShapeKey
    	{
    		uint8	Kind;       // 0 = sphere (X = radius), 1 = box (XYZ = half extent)
    		float	X, Y, Z;
    		bool operator==(const FShapeKey& Other) const
    		{
    			return Kind == Other.Kind && X == Other.X && Y == Other.Y && Z == Other.Z;
    		}
    	};
    	struct FShapeKeyHash
    	{
    		size_t operator()(const FShapeKey& Key) const
    		{
    			size_t Hash = Key.Kind;
    			auto Mix = [&Hash](float Value)
    			{
    				uint32 Bits;
    				std::memcpy(&Bits, &Value, sizeof(Bits));
    				Hash = (Hash * 1099511628211ull) ^ Bits;
    			};
    			Mix(Key.X); Mix(Key.Y); Mix(Key.Z);
    			return Hash;
    		}
    	};
    	FMutex												ShapeCacheMutex;
    	THashMap<FShapeKey, JPH::ShapeRefC, FShapeKeyHash>	ShapeCache;

    	// Mesh-collider shape cache, keyed by mesh pointer + scale + convexity. Pointer-keyed: valid
    	// because piece meshes outlive their bodies; cleared with the (per-world) scene on teardown.
    	struct FMeshShapeKey
    	{
    		const void*	Mesh;
    		float		SX, SY, SZ;
    		uint8		Convex;
    		bool operator==(const FMeshShapeKey& Other) const
    		{
    			return Mesh == Other.Mesh && SX == Other.SX && SY == Other.SY && SZ == Other.SZ && Convex == Other.Convex;
    		}
    	};
    	struct FMeshShapeKeyHash
    	{
    		size_t operator()(const FMeshShapeKey& Key) const
    		{
    			size_t Hash = 1469598103934665603ull;
    			auto Mix = [&Hash](uint64 Value) { Hash = (Hash ^ Value) * 1099511628211ull; };
    			Mix(reinterpret_cast<uint64>(Key.Mesh));
    			auto MixF = [&](float Value) { uint32 Bits; std::memcpy(&Bits, &Value, sizeof(Bits)); Mix(Bits); };
    			MixF(Key.SX); MixF(Key.SY); MixF(Key.SZ);
    			Mix(Key.Convex);
    			return Hash;
    		}
    	};
    	FMutex													MeshShapeCacheMutex;
    	THashMap<FMeshShapeKey, JPH::ShapeRefC, FMeshShapeKeyHash>	MeshShapeCache;

    	FMutex										PendingRigidBodyMutex;
    	TQueue<entt::entity>						PendingRigidBodyCreations;

    	// When set (between BeginBodyBatch/EndBodyBatch on the game thread), rigid-body
    	// constructions are collected here and created together instead of one at a time.
    	bool										bBatchingBodies = false;
    	TVector<entt::entity>						BatchedBodyCreations;
    	// Base buffer covers typical steps with no per-frame alloc; heavy frames fall back to malloc.
    	JPH::TempAllocatorImplWithMallocFallback	Allocator;
    	TVector<TUniquePtr<JPH::TempAllocatorImpl>>	CharacterAllocators;
    	TUniquePtr<FJoltContactListener>			ContactListener;
        TUniquePtr<JPH::PhysicsSystem>				JoltSystem;
        CWorld*										World = nullptr;
    	
    	FMutex										ContactQueueMutex;
    	TVector<FContactRecord>						PendingContacts;

    	// Material side table indexed by BodyID, sized once to MaxPhysicsBodies so the listener reads lock-free;
    	// writes are game-thread-only between steps.
    	TVector<FBodyMaterialEntry>					BodyMaterials;

    	float										Accumulator = 0.0f;
    	uint32										CollisionSteps = 0;

    	// Interp transforms staged by the physics thread, read game-thread (FrameStart join orders them).
    	// SoA, grow-only: positions are flat float3, rotations deinterleaved to x/y/z/w so nlerp vectorizes.
    	enum class EInterpFlag : uint8 { Interpolate = 0, Skip = 1, BelowKill = 2 };
    	struct FInterpStaging
    	{
    		TVector<entt::entity>	Entities;
    		TVector<EInterpFlag>	Flags;

    		TVector<FVector3>		PrevPos;
    		TVector<FVector3>		CurrPos;   // overwritten with the interpolated result

    		TVector<float>			PrevQx, PrevQy, PrevQz, PrevQw;
    		TVector<float>			CurrQx, CurrQy, CurrQz, CurrQw;  // overwritten with result

    		void Resize(uint32 N)
    		{
    			Entities.resize(N); Flags.resize(N);
    			PrevPos.resize(N);  CurrPos.resize(N);
    			PrevQx.resize(N); PrevQy.resize(N); PrevQz.resize(N); PrevQw.resize(N);
    			CurrQx.resize(N); CurrQy.resize(N); CurrQz.resize(N); CurrQw.resize(N);
    		}
    	};
    	FInterpStaging								InterpStaging;
    };
}
