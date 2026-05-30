#include "pch.h"
#include "JoltPhysicsScene.h"
#include "JoltCharacterHandle.h"
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include "Physics/Ray/RayCast.h"
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#if JPH_DEBUG_RENDERER
#include <Jolt/Renderer/DebugRendererSimple.h>
#include "Core/Utils/Defer.h"
#endif
#include <algorithm>
#include "Core/Math/Math.h"
#include "Core/Math/SIMD/SIMD.h"

#include "JoltPhysics.h"
#include "JoltUtils.h"
#include "Core/Console/ConsoleVariable.h"
#include "Core/Profiler/Profile.h"
#include "Core/Threading/Thread.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "Jolt/Physics/Collision/Shape/MeshShape.h"
#include "Jolt/Physics/Collision/Shape/ConvexHullShape.h"
#include "Jolt/Physics/Collision/Shape/HeightFieldShape.h"
#include "Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h"
#include "Assets/AssetTypes/Mesh/StaticMesh/StaticMesh.h"
#include "Assets/AssetTypes/PhysicsMaterial/PhysicsMaterial.h"
#include "Renderer/MeshData.h"
#include "Renderer/RendererUtils.h"
#include "World/World.h"
#include "World/Entity/Components/CharacterComponent.h"
#include "World/Entity/Components/CharacterControllerComponent.h"
#include "World/Entity/Components/DirtyComponent.h"
#include "World/Entity/Components/PhysicsComponent.h"
#include "World/Entity/Components/ScriptComponent.h"
#include "World/Entity/Components/StaticMeshComponent.h"
#include "World/Entity/Components/TerrainComponent.h"
#include "World/Entity/Components/TransformComponent.h"
#include "world/entity/components/velocitycomponent.h"
#include "World/Entity/Events/CollisionEvent.h"
#include "World/Entity/Events/ImpulseEvent.h"
#include "World/Entity/Events/LuaEventBus.h"
#include "World/Subsystems/WorldSettings.h"
#include "Scripting/Lua/Reference.h"
#include "Scripting/Lua/Scripting.h"
#include "Scripting/Lua/Stack.h"
#include "lua.h"

using namespace JPH::literals;

namespace Lumina::Physics
{
    namespace BroadPhaseLayers
    {
        static constexpr JPH::BroadPhaseLayer STATIC(0);
        static constexpr JPH::BroadPhaseLayer MOVING(1);
        static constexpr uint32 NUM_LAYERS(2);
    };
    
    class FLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
    {
    public:
        virtual uint32 GetNumBroadPhaseLayers() const override
        {
            return BroadPhaseLayers::NUM_LAYERS;
        }

        virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
        {
            ECollisionProfiles LayerBits = (ECollisionProfiles)(uint16)(inLayer & 0xFFFF);

            if ((LayerBits & ECollisionProfiles::Static) != (ECollisionProfiles)0)
            {
                return BroadPhaseLayers::STATIC;
            }

            return BroadPhaseLayers::MOVING;
        }

        #if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
        {
            switch ((JPH::BroadPhaseLayer::Type)inLayer)
            {
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::STATIC:      return "STATIC";
            case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:      return "MOVING";
            default: JPH_ASSERT(false); return "INVALID";
            }
        }
        #endif
    };
    
    constexpr JPH::EMotionType ToJoltMotionType(EBodyType BodyType)
    {
        switch (BodyType)
        {
            case EBodyType::Static:     return JPH::EMotionType::Static;
            case EBodyType::Kinematic:  return JPH::EMotionType::Kinematic;
            case EBodyType::Dynamic:    return JPH::EMotionType::Dynamic;
        }

        UNREACHABLE();
    }
    
    static void CheckJoltError(JPH::EPhysicsUpdateError Error)
    {
        // EPhysicsUpdateError is a bit field; multiple errors can be set in one update.
        if (Error == JPH::EPhysicsUpdateError::None)
        {
            return;
        }

        if ((Error & JPH::EPhysicsUpdateError::ManifoldCacheFull) != JPH::EPhysicsUpdateError::None)
        {
            LOG_ERROR("[Jolt Error] - Manifold Cache is full; increase MaxPhysicsContactConstraints. Some contacts were dropped.");
        }
        if ((Error & JPH::EPhysicsUpdateError::BodyPairCacheFull) != JPH::EPhysicsUpdateError::None)
        {
            LOG_ERROR("[Jolt Error] - Body-Pair Cache is full; increase MaxPhysicsBodyPairs. Some contacts were dropped.");
        }
        if ((Error & JPH::EPhysicsUpdateError::ContactConstraintsFull) != JPH::EPhysicsUpdateError::None)
        {
            LOG_ERROR("[Jolt Error] - Contact Constraints buffer is full; increase MaxPhysicsContactConstraints. Some contacts were dropped.");
        }
    }
    
    class FObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
        
        bool ShouldCollide(JPH::ObjectLayer LayerA, JPH::BroadPhaseLayer LayerB) const override
        {
            ECollisionProfiles LayerBits = (ECollisionProfiles)(uint16)(LayerA & 0xFFFF);

            if ((LayerBits & ECollisionProfiles::Static) != (ECollisionProfiles)0)
            {
                return LayerB == BroadPhaseLayers::STATIC;
            }

            return true;
        }
    };

    class FObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
    {
    public:
        
        bool ShouldCollide(JPH::ObjectLayer ObjectA, JPH::ObjectLayer ObjectB) const override
        {
            ECollisionProfiles LayerA = (ECollisionProfiles)(uint16)(ObjectA & 0xFFFF);
            ECollisionProfiles MaskA  = (ECollisionProfiles)(uint16)(ObjectA >> 16);
            ECollisionProfiles LayerB = (ECollisionProfiles)(uint16)(ObjectB & 0xFFFF);
            ECollisionProfiles MaskB  = (ECollisionProfiles)(uint16)(ObjectB >> 16);

            return (MaskA & LayerB) != (ECollisionProfiles)0 || (MaskB & LayerA) != (ECollisionProfiles)0;
        }
    };
    
    class FIgnoreFilter : public JPH::BodyFilter
    {
    public:
        
        FIgnoreFilter(TSpan<const uint32> InIgnoreBodies)
            : IgnoreBodies(InIgnoreBodies)
        {}
        
        bool ShouldCollide(const JPH::BodyID& inBodyID) const override
        {
            const uint32 Key = inBodyID.GetIndexAndSequenceNumber();
            for (std::size_t i = 0; i < IgnoreBodies.size(); i++)
            {
                if (IgnoreBodies[i] == Key)
                {
                    return false;
                }
            }
            return true;
        }
    
        TSpan<const uint32> IgnoreBodies;
    };

    static FLayerInterfaceImpl                  GJoltLayerInterface;
    static FObjectLayerPairFilterImpl           GObjectVsObjectLayerFilter;
    static FObjectVsBroadPhaseLayerFilterImpl   GObjectVsBroadPhaseLayerFilter;


    namespace
    {
        // Contact callbacks run with all bodies locked; velocities are pre-step.
        FContactRecord BuildContactRecord(EContactEventType Type,
                                          const JPH::Body& InBody1,
                                          const JPH::Body& InBody2,
                                          const JPH::ContactManifold& InManifold)
        {
            FContactRecord Record;
            Record.Type      = Type;
            Record.EntityA   = static_cast<entt::entity>(InBody1.GetUserData());
            Record.EntityB   = static_cast<entt::entity>(InBody2.GetUserData());
            Record.BodyIDA   = InBody1.GetID().GetIndexAndSequenceNumber();
            Record.BodyIDB   = InBody2.GetID().GetIndexAndSequenceNumber();
            Record.bSensorA  = InBody1.IsSensor();
            Record.bSensorB  = InBody2.IsSensor();

            JPH::Vec3 Sum = JPH::Vec3::sZero();
            const uint32 N = InManifold.mRelativeContactPointsOn1.size();
            if (N > 0)
            {
                for (uint32 i = 0; i < N; ++i)
                {
                    Sum += InManifold.mRelativeContactPointsOn1[i];
                }
                Sum /= float(N);
            }
            const JPH::RVec3 WorldPoint = InManifold.mBaseOffset + Sum;
            Record.Point  = JoltUtils::FromJPHVec3(WorldPoint);
            Record.Normal = JoltUtils::FromJPHVec3(InManifold.mWorldSpaceNormal);

            const JPH::Vec3 V1 = InBody1.GetLinearVelocity();
            const JPH::Vec3 V2 = InBody2.GetLinearVelocity();
            Record.VelocityA = JoltUtils::FromJPHVec3(V1);
            Record.VelocityB = JoltUtils::FromJPHVec3(V2);

            const JPH::Vec3 RelVel = V2 - V1;
            Record.ImpactSpeed = std::abs(RelVel.Dot(InManifold.mWorldSpaceNormal));
            return Record;
        }
    }

    void FJoltContactListener::OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings)
    {
        // Apply per-material combine first so the constraint uses the right friction/restitution
        // (Jolt invokes this once per contact pair per step before solving).
        OverrideFrictionAndRestitution(inBody1, inBody2, inManifold, ioSettings);

        if (Scene)
        {
            Scene->EnqueueContactRecord(BuildContactRecord(EContactEventType::Added, inBody1, inBody2, inManifold));
        }
    }

    void FJoltContactListener::OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings)
    {
        // Don't dispatch to the bus (Enter/Exit cover scripts), but material combine must still run each
        // step or resting contacts revert to Jolt's default combiner.
        OverrideFrictionAndRestitution(inBody1, inBody2, inManifold, ioSettings);
    }

    void FJoltContactListener::OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair)
    {
        if (Scene == nullptr || BodyLockInterface == nullptr)
        {
            return;
        }

        // Bodies may have been removed by the time this fires; skip if either side is already gone.
        const JPH::Body* B1 = BodyLockInterface->TryGetBody(inSubShapePair.GetBody1ID());
        const JPH::Body* B2 = BodyLockInterface->TryGetBody(inSubShapePair.GetBody2ID());
        if (B1 == nullptr || B2 == nullptr)
        {
            return;
        }

        // OnContactRemoved: Jolt denies velocity read access here; GetLinearVelocity asserts (UB in release).
        FContactRecord Record;
        Record.Type      = EContactEventType::Removed;
        Record.EntityA   = static_cast<entt::entity>(B1->GetUserData());
        Record.EntityB   = static_cast<entt::entity>(B2->GetUserData());
        Record.BodyIDA   = B1->GetID().GetIndexAndSequenceNumber();
        Record.BodyIDB   = B2->GetID().GetIndexAndSequenceNumber();
        Record.bSensorA  = B1->IsSensor();
        Record.bSensorB  = B2->IsSensor();
        Record.Point     = FVector3(0.0f);
        Record.Normal    = FVector3(0.0f, 1.0f, 0.0f);
        Record.VelocityA = FVector3(0.0f);
        Record.VelocityB = FVector3(0.0f);
        Record.ImpactSpeed = 0.0f;

        Scene->EnqueueContactRecord(Record);
    }

    void FJoltPhysicsScene::EnqueueContactRecord(const FContactRecord& Record)
    {
        FScopeLock Lock(ContactQueueMutex);
        PendingContacts.push_back(Record);
    }

    namespace
    {
        // Orient a contact record for one receiving side; POD fill, no per-event table built downstream.
        SCollisionEvent BuildCollisionEvent(entt::entity SelfEntity,
                                            entt::entity OtherEntity,
                                            uint32 SelfBodyID,
                                            uint32 OtherBodyID,
                                            const FContactRecord& Record,
                                            bool bFlipNormal)
        {
            SCollisionEvent Event;
            Event.Entity      = static_cast<uint32>(entt::to_integral(SelfEntity));
            Event.Other       = static_cast<uint32>(entt::to_integral(OtherEntity));
            Event.BodyID      = SelfBodyID;
            Event.OtherBodyID = OtherBodyID;
            Event.Point       = Record.Point;

            // Convention: Normal points outward from self toward other so scripts
            // can react with `-Normal` to bounce away from the impact.
            Event.Normal           = bFlipNormal ? -Record.Normal : Record.Normal;
            Event.Velocity         = bFlipNormal ? Record.VelocityB : Record.VelocityA;
            Event.OtherVelocity    = bFlipNormal ? Record.VelocityA : Record.VelocityB;
            Event.RelativeVelocity = Event.OtherVelocity - Event.Velocity;
            Event.ImpactSpeed      = Record.ImpactSpeed;

            // bIsTrigger reports whether the OTHER side was a sensor; useful when the
            // same handler covers both kinds.
            Event.bIsTrigger = bFlipNormal ? Record.bSensorA : Record.bSensorB;
            return Event;
        }
    }

    void FJoltPhysicsScene::DispatchContactEvents()
    {
        TVector<FContactRecord> Drain;
        {
            FScopeLock Lock(ContactQueueMutex);
            if (PendingContacts.empty())
            {
                return;
            }
            Drain.swap(PendingContacts);
        }

        lua_State* L = Lua::FScriptingContext::Get().GetVM();
        if (L == nullptr)
        {
            return;
        }

        FEntityRegistry& Registry = World->GetEntityRegistry();

        // Invoke the script's cached FRef if defined; Contact = solid impact, Overlap = a sensor/trigger side.
        auto Deliver = [&](entt::entity Self, entt::entity Other, uint32 SelfBody, uint32 OtherBody,
                           const FContactRecord& Record, bool bFlipNormal, bool bIsAdded, bool bIsOverlap)
        {
            if (Self == entt::null || !Registry.valid(Self))
            {
                return;
            }
            SScriptComponent* Comp = Registry.try_get<SScriptComponent>(Self);
            if (Comp == nullptr || Comp->Script == nullptr)
            {
                return;
            }

            const Lua::FRef& Func = bIsOverlap
                ? (bIsAdded ? Comp->OverlapBeginFunc : Comp->OverlapEndFunc)
                : (bIsAdded ? Comp->ContactBeginFunc : Comp->ContactEndFunc);
            if (!Func.IsInvokable())
            {
                return;
            }

            const SCollisionEvent Event = BuildCollisionEvent(Self, Other, SelfBody, OtherBody, Record, bFlipNormal);

            // Pass `self` first so scripts use `function MyScript:OnContactBegin(Event)`; event is tagged userdata.
            Func.Call(Comp->Script->Reference, Event);
        };

        for (const FContactRecord& Record : Drain)
        {
            const bool bAdded = (Record.Type == EContactEventType::Added);
            const bool bOverlap = Record.bSensorA || Record.bSensorB;

            Deliver(Record.EntityA, Record.EntityB, Record.BodyIDA, Record.BodyIDB,
                    Record, /*bFlipNormal*/ false, bAdded, bOverlap);
            Deliver(Record.EntityB, Record.EntityA, Record.BodyIDB, Record.BodyIDA,
                    Record, /*bFlipNormal*/ true,  bAdded, bOverlap);
        }
    }

    namespace
    {
        // Combine a pair of values under a combine mode. The pair's effective mode is the max of
        // each side's mode (Max > Min > Multiply > Average) so a "sticky" surface always wins.
        FORCEINLINE float CombineUnder(EPhysicsMaterialCombineMode Mode, float A, float B)
        {
            switch (Mode)
            {
            case EPhysicsMaterialCombineMode::Min:      return Math::Min(A, B);
            case EPhysicsMaterialCombineMode::Multiply: return A * B;
            case EPhysicsMaterialCombineMode::Max:      return Math::Max(A, B);
            case EPhysicsMaterialCombineMode::Average:
            default:                                    return (A + B) * 0.5f;
            }
        }

        FORCEINLINE EPhysicsMaterialCombineMode PairMode(uint8 A, uint8 B)
        {
            // Enum order (Average=0, Min=1, Multiply=2, Max=3) chosen so raw max yields the
            // documented precedence.
            return (EPhysicsMaterialCombineMode)Math::Max<uint8>(A, B);
        }
    }

    void FJoltContactListener::OverrideFrictionAndRestitution(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings)
    {
        const auto* M1 = Scene->GetBodyMaterial(inBody1.GetID());
        const auto* M2 = Scene->GetBodyMaterial(inBody2.GetID());

        // No materials on either side: leave Jolt's defaults (already populated from the body's
        // mFriction / mRestitution by the contact solver).
        if ((M1 == nullptr || !M1->bHasMaterial) && (M2 == nullptr || !M2->bHasMaterial))
        {
            return;
        }

        const float F1 = (M1 && M1->bHasMaterial) ? M1->Friction       : inBody1.GetFriction();
        const float R1 = (M1 && M1->bHasMaterial) ? M1->Restitution    : inBody1.GetRestitution();
        const float F2 = (M2 && M2->bHasMaterial) ? M2->Friction       : inBody2.GetFriction();
        const float R2 = (M2 && M2->bHasMaterial) ? M2->Restitution    : inBody2.GetRestitution();

        const uint8 F1Mode = (M1 && M1->bHasMaterial) ? M1->FrictionCombine    : (uint8)EPhysicsMaterialCombineMode::Average;
        const uint8 F2Mode = (M2 && M2->bHasMaterial) ? M2->FrictionCombine    : (uint8)EPhysicsMaterialCombineMode::Average;
        const uint8 R1Mode = (M1 && M1->bHasMaterial) ? M1->RestitutionCombine : (uint8)EPhysicsMaterialCombineMode::Average;
        const uint8 R2Mode = (M2 && M2->bHasMaterial) ? M2->RestitutionCombine : (uint8)EPhysicsMaterialCombineMode::Average;

        ioSettings.mCombinedFriction    = CombineUnder(PairMode(F1Mode, F2Mode), F1, F2);
        ioSettings.mCombinedRestitution = CombineUnder(PairMode(R1Mode, R2Mode), R1, R2);
    }

    void FJoltContactListener::GetFrictionAndRestitution(const JPH::Body& inBody, const JPH::SubShapeID& inSubShapeID, float& outFriction, float& outRestitution) const
    {
        const auto* M = Scene->GetBodyMaterial(inBody.GetID());
        if (M != nullptr && M->bHasMaterial)
        {
            outFriction    = M->Friction;
            outRestitution = M->Restitution;
            return;
        }
        outFriction    = inBody.GetFriction();
        outRestitution = inBody.GetRestitution();
    }
    
    FJoltPhysicsScene::FJoltPhysicsScene(CWorld* InWorld)
        : Allocator(32ull * 1024 * 1024)
        , World(InWorld)
    {
        JoltSystem = MakeUnique<JPH::PhysicsSystem>();

        const SDefaultWorldSettings& InitSettings = World->GetDefaultWorldSettings();
        
        JoltSystem->Init(InitSettings.MaxPhysicsBodies, 0, InitSettings.MaxPhysicsBodyPairs,
            InitSettings.MaxPhysicsContactConstraints, GJoltLayerInterface, GObjectVsBroadPhaseLayerFilter, GObjectVsObjectLayerFilter);

        // Sized once to MaxPhysicsBodies; JPH::BodyID::GetIndex() is bounded by this so the
        // contact-listener lookup is a bare array index with no resize race.
        BodyMaterials.resize(InitSettings.MaxPhysicsBodies);

        FVector3 GravityDir = Math::LengthSquared(InitSettings.GravityDirection) > 0.0f
            ? Math::Normalize(InitSettings.GravityDirection) : FVector3(0.0f, -1.0f, 0.0f);
        JoltSystem->SetGravity(JoltUtils::ToJPHVec3(GravityDir * Math::Abs(GEarthGravity) * InitSettings.GravityScale));

        JPH::PhysicsSettings JoltSettings;
        JoltSettings.mDeterministicSimulation                   = InitSettings.bDeterministicSimulation;
        JoltSettings.mNumVelocitySteps                          = InitSettings.NumVelocitySteps;
        JoltSettings.mNumPositionSteps                          = InitSettings.NumPositionSteps;
        JoltSettings.mBaumgarte                                 = InitSettings.BaumgarteStabilizationFactor;
        JoltSettings.mSpeculativeContactDistance                = InitSettings.SpeculativeContactDistance;
        JoltSettings.mPenetrationSlop                           = InitSettings.PenetrationSlop;
        JoltSettings.mMaxPenetrationDistance                    = InitSettings.MaxPenetrationDistance;
        JoltSettings.mLinearCastThreshold                       = InitSettings.LinearCastThreshold;
        JoltSettings.mLinearCastMaxPenetration                  = InitSettings.LinearCastMaxPenetration;
        JoltSettings.mManifoldTolerance                         = InitSettings.ManifoldTolerance;
        JoltSettings.mMinVelocityForRestitution                 = InitSettings.MinVelocityForRestitution;
        JoltSettings.mTimeBeforeSleep                           = InitSettings.TimeBeforeSleep;
        JoltSettings.mPointVelocitySleepThreshold               = InitSettings.SleepVelocityThreshold;
        JoltSettings.mBodyPairCacheMaxDeltaPositionSq           = InitSettings.BodyPairCacheMaxDeltaPositionSq;
        JoltSettings.mBodyPairCacheCosMaxDeltaRotationDiv2      = InitSettings.BodyPairCacheCosMaxDeltaRotationDiv2;
        JoltSettings.mContactNormalCosMaxDeltaRotation          = InitSettings.ContactNormalCosMaxDeltaRotation;
        JoltSettings.mContactPointPreserveLambdaMaxDistSq       = InitSettings.ContactPointPreserveLambdaMaxDistSq;
        JoltSettings.mMaxInFlightBodyPairs                      = InitSettings.MaxInFlightBodyPairs;
        JoltSettings.mStepListenersBatchSize                    = InitSettings.StepListenersBatchSize;
        JoltSettings.mStepListenerBatchesPerJob                 = InitSettings.StepListenerBatchesPerJob;
        JoltSettings.mConstraintWarmStart                       = InitSettings.bConstraintWarmStart;
        JoltSettings.mUseBodyPairContactCache                   = InitSettings.bUseBodyPairContactCache;
        JoltSettings.mUseManifoldReduction                      = InitSettings.bUseManifoldReduction;
        JoltSettings.mUseLargeIslandSplitter                    = InitSettings.bUseLargeIslandSplitter;
        JoltSettings.mAllowSleeping                             = InitSettings.bAllowSleeping;
        JoltSettings.mCheckActiveEdges                          = InitSettings.bCheckActiveEdges;
        JoltSystem->SetPhysicsSettings(JoltSettings);
        
        entt::dispatcher& Dispatcher = World->GetEntityRegistry().ctx().get<entt::dispatcher&>();
        ContactListener = MakeUnique<FJoltContactListener>(this, Dispatcher, &JoltSystem->GetBodyLockInterfaceNoLock());
        JoltSystem->SetContactListener(ContactListener.get());
        
        FEntityRegistry& Registry = World->GetEntityRegistry();

        Registry.on_construct<SSphereColliderComponent>().connect<&entt::registry::emplace_or_replace<SRigidBodyComponent>>();
        Registry.on_construct<SBoxColliderComponent>().connect<&entt::registry::emplace_or_replace<SRigidBodyComponent>>();
        Registry.on_construct<SCapsuleColliderComponent>().connect<&entt::registry::emplace_or_replace<SRigidBodyComponent>>();
        Registry.on_construct<SCylinderColliderComponent>().connect<&entt::registry::emplace_or_replace<SRigidBodyComponent>>();
        Registry.on_construct<SMeshColliderComponent>().connect<&entt::registry::emplace_or_replace<SRigidBodyComponent>>();
        Registry.on_construct<STerrainColliderComponent>().connect<&entt::registry::emplace_or_replace<SRigidBodyComponent>>();
    }

    FJoltPhysicsScene::~FJoltPhysicsScene()
    {
        FEntityRegistry& Registry = World->GetEntityRegistry();

        Registry.on_construct<SSphereColliderComponent>().disconnect<&entt::registry::emplace_or_replace<SRigidBodyComponent>>();
        Registry.on_construct<SBoxColliderComponent>().disconnect<&entt::registry::emplace_or_replace<SRigidBodyComponent>>();
        Registry.on_construct<SCapsuleColliderComponent>().disconnect<&entt::registry::emplace_or_replace<SRigidBodyComponent>>();
        Registry.on_construct<SCylinderColliderComponent>().disconnect<&entt::registry::emplace_or_replace<SRigidBodyComponent>>();
        Registry.on_construct<SMeshColliderComponent>().disconnect<&entt::registry::emplace_or_replace<SRigidBodyComponent>>();
        Registry.on_construct<STerrainColliderComponent>().disconnect<&entt::registry::emplace_or_replace<SRigidBodyComponent>>();
    }

    void FJoltPhysicsScene::PreUpdate()
    {

    }

    void FJoltPhysicsScene::PostUpdate()
    {
    }

    void FJoltPhysicsScene::DispatchPendingEvents()
    {
        LUMINA_PROFILE_SCOPE();
        // Game thread, post-join: write the physics step's interpolated transforms
        // into the ECS before contact callbacks (scripts read fresh transforms).
        ApplyInterpolatedTransforms();
        DispatchContactEvents();
    }

    void FJoltPhysicsScene::ApplyDirtyTransforms(float FixedDt)
    {
        LUMINA_PROFILE_SCOPE();

        entt::registry& Registry = World->GetEntityRegistry();

        const JPH::BodyLockInterfaceNoLock& LockInterface = JoltSystem->GetBodyLockInterfaceNoLock();
        JPH::BodyInterface& BodyInterface = JoltSystem->GetBodyInterface();

        
        auto BodySyncView = Registry.view<SRigidBodyComponent, FNeedsPhysicsBodyUpdate>();
        auto Handle = BodySyncView.handle();
        auto&& Storage = Registry.storage<STransformComponent>();
        Task::ParallelFor(Handle->size(), [&] (uint32 Index)
        {
            entt::entity Entity = (*Handle)[Index];
            if (!BodySyncView.contains(Entity))
            {
                return;
            }
         
            const auto& BodyComponent       = BodySyncView.get<SRigidBodyComponent>(Entity);
            const auto& TransformComponent  = Storage.get(Entity);
            const auto& Update              = BodySyncView.get<FNeedsPhysicsBodyUpdate>(Entity);

            JPH::BodyID BodyID = JPH::BodyID(BodyComponent.BodyID);
            
            const JPH::Body* Body = LockInterface.TryGetBody(BodyID);
            if (Body == nullptr)
            {
                return;
            }
            
            JPH::RVec3 Location = JoltUtils::ToJPHRVec3(TransformComponent.GetLocation());
            JPH::Quat  Rotation = JoltUtils::ToJPHQuat(TransformComponent.GetRotation());
            JPH::EActivation Activation = Update.bActivate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;

            if (Body->IsStatic())
            {
                BodyInterface.SetPositionAndRotationWhenChanged(BodyID, Location, Rotation, Activation);
            }
            else if (Body->IsKinematic())
            {
                BodyInterface.MoveKinematic(BodyID, Location, Rotation, FixedDt);
            }
            else if (Body->IsDynamic())
            {
                switch (Update.MoveMode)
                {
                    case EMoveMode::Teleport:
                    {
                        BodyInterface.SetPositionAndRotation(BodyID, Location, Rotation, Activation);
                        break;
                    }
                    case EMoveMode::MoveKinematic:
                    {
                        JPH::RVec3 CurrentPos = Body->GetPosition();
                        JPH::Quat  CurrentRot = Body->GetRotation();

                        JPH::Vec3 LinearVel = (Location - CurrentPos) / FixedDt;
                        BodyInterface.SetLinearVelocity(BodyID, LinearVel);

                        JPH::Quat DeltaRot = Rotation * CurrentRot.Conjugated();
                        JPH::Vec3 Axis;
                        float Angle;
                        DeltaRot.GetAxisAngle(Axis, Angle);

                        if (Angle > JPH::JPH_PI)
                        {
                            Angle -= 2.0f * JPH::JPH_PI;
                        }

                        JPH::Vec3 AngularVel = Axis * (Angle / FixedDt);
                        BodyInterface.SetAngularVelocity(BodyID, AngularVel);

                        if (Update.bActivate)
                        {
                            BodyInterface.ActivateBody(BodyID);
                        }
                        break;
                    }
                    case EMoveMode::ActivateOnly:
                    {
                        BodyInterface.ActivateBody(BodyID);
                        break;
                    }
                }
            }
            
        });
        
        Registry.clear<FNeedsPhysicsBodyUpdate>();
    }
    

    void FJoltPhysicsScene::Update(double DeltaTime)
    {
        LUMINA_PROFILE_SCOPE();
        
        // Drain pending body creations before the step; release lock per pop so re-enqueue is safe.
        for (;;)
        {
            entt::entity Entity;
            {
                FScopeLock Lock(PendingRigidBodyMutex);
                if (PendingRigidBodyCreations.empty())
                {
                    break;
                }
                Entity = PendingRigidBodyCreations.front();
                PendingRigidBodyCreations.pop();
            }

            if (World->GetEntityRegistry().valid(Entity))
            {
                CreateRigidBodyImmediate(World->GetEntityRegistry(), Entity);
            }
        }
        
        const SDefaultWorldSettings& WorldSettings = World->GetDefaultWorldSettings();

        // Re-apply global physics settings each frame so editor changes take effect immediately.
        {
            FVector3 GravityDir = Math::LengthSquared(WorldSettings.GravityDirection) > 0.0f
                ? Math::Normalize(WorldSettings.GravityDirection) : FVector3(0.0f, -1.0f, 0.0f);
            JoltSystem->SetGravity(JoltUtils::ToJPHVec3(GravityDir * Math::Abs(GEarthGravity) * WorldSettings.GravityScale));

            JPH::PhysicsSettings JoltSettings                           = JoltSystem->GetPhysicsSettings();
            JoltSettings.mDeterministicSimulation                       = WorldSettings.bDeterministicSimulation;
            JoltSettings.mNumVelocitySteps                              = WorldSettings.NumVelocitySteps;
            JoltSettings.mNumPositionSteps                              = WorldSettings.NumPositionSteps;
            JoltSettings.mBaumgarte                                     = WorldSettings.BaumgarteStabilizationFactor;
            JoltSettings.mSpeculativeContactDistance                    = WorldSettings.SpeculativeContactDistance;
            JoltSettings.mPenetrationSlop                               = WorldSettings.PenetrationSlop;
            JoltSettings.mMaxPenetrationDistance                        = WorldSettings.MaxPenetrationDistance;
            JoltSettings.mLinearCastThreshold                           = WorldSettings.LinearCastThreshold;
            JoltSettings.mLinearCastMaxPenetration                      = WorldSettings.LinearCastMaxPenetration;
            JoltSettings.mManifoldTolerance                             = WorldSettings.ManifoldTolerance;
            JoltSettings.mMinVelocityForRestitution                     = WorldSettings.MinVelocityForRestitution;
            JoltSettings.mTimeBeforeSleep                               = WorldSettings.TimeBeforeSleep;
            JoltSettings.mPointVelocitySleepThreshold                   = WorldSettings.SleepVelocityThreshold;
            JoltSettings.mBodyPairCacheMaxDeltaPositionSq               = WorldSettings.BodyPairCacheMaxDeltaPositionSq;
            JoltSettings.mBodyPairCacheCosMaxDeltaRotationDiv2          = WorldSettings.BodyPairCacheCosMaxDeltaRotationDiv2;
            JoltSettings.mContactNormalCosMaxDeltaRotation              = WorldSettings.ContactNormalCosMaxDeltaRotation;
            JoltSettings.mContactPointPreserveLambdaMaxDistSq           = WorldSettings.ContactPointPreserveLambdaMaxDistSq;
            JoltSettings.mMaxInFlightBodyPairs                          = WorldSettings.MaxInFlightBodyPairs;
            JoltSettings.mStepListenersBatchSize                        = WorldSettings.StepListenersBatchSize;
            JoltSettings.mStepListenerBatchesPerJob                     = WorldSettings.StepListenerBatchesPerJob;
            JoltSettings.mConstraintWarmStart                           = WorldSettings.bConstraintWarmStart;
            JoltSettings.mUseBodyPairContactCache                       = WorldSettings.bUseBodyPairContactCache;
            JoltSettings.mUseManifoldReduction                          = WorldSettings.bUseManifoldReduction;
            JoltSettings.mUseLargeIslandSplitter                        = WorldSettings.bUseLargeIslandSplitter;
            JoltSettings.mAllowSleeping                                 = WorldSettings.bAllowSleeping;
            JoltSettings.mCheckActiveEdges                              = WorldSettings.bCheckActiveEdges;
            JoltSystem->SetPhysicsSettings(JoltSettings);
        }

        const float PhysicsRateHz  = eastl::max(10.0f, WorldSettings.PhysicsHz);
        const float FixedTimestep  = 1.0f / PhysicsRateHz;
        const float MaxAccumulation = (float)WorldSettings.MaxPhysicsSteps * FixedTimestep;

        Accumulator += static_cast<float>(DeltaTime);

        // Clamp to prevent spiral-of-death on heavy frames.
        Accumulator = std::min(Accumulator, MaxAccumulation);

        if (Accumulator >= FixedTimestep)
        {
            // Cap at MaxPhysicsSteps to bound work per frame.
            CollisionSteps = Math::Min((uint32)WorldSettings.MaxPhysicsSteps, (uint32)(Accumulator / FixedTimestep));
        }
        else
        {
            CollisionSteps = 0;
        }

        #if JPH_DEBUG_RENDERER
        if (FConsoleRegistry::Get().GetAs<bool>("Jolt.Debug.Draw"))
        {
            FJoltDebugRenderer* DebugRenderer = FJoltPhysicsContext::GetDebugRenderer();
            DebugRenderer->DrawBodies(JoltSystem.get(), World);
        }
        #endif

        if (CollisionSteps > 0)
        {
            // Apply game-thread transform writes once before stepping; this also
            // clears the FNeedsPhysicsBodyUpdate flags for the frame.
            ApplyDirtyTransforms(FixedTimestep);
            LatchCharacterInput();
            
            for (uint32 Step = 0; Step < CollisionSteps; ++Step)
            {
                if (Step == CollisionSteps - 1)
                {
                    SnapshotBodyStates();
                }

                CheckJoltError(JoltSystem->Update(FixedTimestep, 1, &Allocator, FJoltPhysicsContext::GetThreadPool()));
                UpdateCharacters(FixedTimestep);
            }

            // Contact records drained on game thread via DispatchPendingEvents.

            Accumulator -= (float)CollisionSteps * FixedTimestep;
        }
        
        const float InterpolationAlpha = WorldSettings.bEnablePhysicsInterpolation
            ? Math::Clamp(Accumulator / FixedTimestep, 0.0f, 1.0f)
            : 1.0f;
        
        BuildInterpolatedTransforms(InterpolationAlpha);

        #if JPH_DEBUG_RENDERER
        FJoltPhysicsContext::GetDebugRenderer()->NextFrame();
        #endif
    }
    
    void FJoltPhysicsScene::Simulate()
    {
        entt::registry& Registry = World->GetEntityRegistry();
        
        BulkCreateRigidBodies(Registry);

        auto CharacterView = Registry.view<SCharacterPhysicsComponent>();
        
        CharacterView.each([&] (entt::entity EntityID, SCharacterPhysicsComponent&)
        {
            OnCharacterComponentConstructed(Registry, EntityID);
        });
        
        JoltSystem->OptimizeBroadPhase();
        
        Registry.on_construct<SCharacterPhysicsComponent>().connect<&FJoltPhysicsScene::OnCharacterComponentConstructed>(this);
        Registry.on_destroy<SCharacterPhysicsComponent>().connect<&FJoltPhysicsScene::OnCharacterComponentDestroyed>(this);

        Registry.on_update<SRigidBodyComponent>().connect<&FJoltPhysicsScene::OnRigidBodyComponentUpdated>(this);
        Registry.on_construct<SRigidBodyComponent>().connect<&FJoltPhysicsScene::OnRigidBodyComponentConstructed>(this);
        Registry.on_destroy<SRigidBodyComponent>().connect<&FJoltPhysicsScene::OnRigidBodyComponentDestroyed>(this);
        
        entt::dispatcher& Dispatcher = World->GetEntityRegistry().ctx().get<entt::dispatcher&>();
        Dispatcher.sink<SImpulseEvent>().connect<&FJoltPhysicsScene::OnImpulseEvent>(this);
        Dispatcher.sink<SForceEvent>().connect<&FJoltPhysicsScene::OnForceEvent>(this);
        Dispatcher.sink<STorqueEvent>().connect<&FJoltPhysicsScene::OnTorqueEvent>(this);
        Dispatcher.sink<SAngularImpulseEvent>().connect<&FJoltPhysicsScene::OnAngularImpulseEvent>(this);
        Dispatcher.sink<SSetVelocityEvent>().connect<&FJoltPhysicsScene::OnSetVelocityEvent>(this);
        Dispatcher.sink<SSetAngularVelocityEvent>().connect<&FJoltPhysicsScene::OnSetAngularVelocityEvent>(this);
        Dispatcher.sink<SAddImpulseAtPositionEvent>().connect<&FJoltPhysicsScene::OnAddImpulseAtPositionEvent>(this);
        Dispatcher.sink<SAddForceAtPositionEvent>().connect<&FJoltPhysicsScene::OnAddForceAtPositionEvent>(this);
        Dispatcher.sink<SSetGravityFactorEvent>().connect<&FJoltPhysicsScene::OnSetGravityFactorEvent>(this);
    }

    void FJoltPhysicsScene::StopSimulate()
    {
        entt::registry& Registry = World->GetEntityRegistry();

        Registry.on_construct<SCharacterPhysicsComponent>().disconnect<&FJoltPhysicsScene::OnCharacterComponentConstructed>(this);
        Registry.on_destroy<SCharacterPhysicsComponent>().disconnect<&FJoltPhysicsScene::OnCharacterComponentDestroyed>(this);

        Registry.on_construct<SRigidBodyComponent>().disconnect<&FJoltPhysicsScene::OnRigidBodyComponentConstructed>(this);
        Registry.on_destroy<SRigidBodyComponent>().disconnect<&FJoltPhysicsScene::OnRigidBodyComponentDestroyed>(this);
        
        entt::dispatcher& Dispatcher = World->GetEntityRegistry().ctx().get<entt::dispatcher&>();
        Dispatcher.sink<SImpulseEvent>().disconnect<&FJoltPhysicsScene::OnImpulseEvent>(this);
        Dispatcher.sink<SForceEvent>().disconnect<&FJoltPhysicsScene::OnForceEvent>(this);
        Dispatcher.sink<STorqueEvent>().disconnect<&FJoltPhysicsScene::OnTorqueEvent>(this);
        Dispatcher.sink<SAngularImpulseEvent>().disconnect<&FJoltPhysicsScene::OnAngularImpulseEvent>(this);
        Dispatcher.sink<SSetVelocityEvent>().disconnect<&FJoltPhysicsScene::OnSetVelocityEvent>(this);
        Dispatcher.sink<SSetAngularVelocityEvent>().disconnect<&FJoltPhysicsScene::OnSetAngularVelocityEvent>(this);
        Dispatcher.sink<SAddImpulseAtPositionEvent>().disconnect<&FJoltPhysicsScene::OnAddImpulseAtPositionEvent>(this);
        Dispatcher.sink<SAddForceAtPositionEvent>().disconnect<&FJoltPhysicsScene::OnAddForceAtPositionEvent>(this);
        Dispatcher.sink<SSetGravityFactorEvent>().disconnect<&FJoltPhysicsScene::OnSetGravityFactorEvent>(this);

        auto View = Registry.view<SRigidBodyComponent>();
        View.each([&] (entt::entity EntityID, SRigidBodyComponent&)
        {
            OnRigidBodyComponentDestroyed(Registry, EntityID); 
        });
    }

    void FJoltPhysicsScene::ActivateBody(uint32 BodyID)
    {
        JPH::BodyInterface& BodyInterface = JoltSystem->GetBodyInterface();
        BodyInterface.ActivateBody(JPH::BodyID(BodyID));
    }

    void FJoltPhysicsScene::DeactivateBody(uint32 BodyID)
    {
        JPH::BodyInterface& BodyInterface = JoltSystem->GetBodyInterface();
        BodyInterface.DeactivateBody(JPH::BodyID(BodyID));
    }

    void FJoltPhysicsScene::ChangeBodyMotionType(uint32 BodyID, EBodyType NewType)
    {
        JPH::BodyInterface& BodyInterface = JoltSystem->GetBodyInterface();
        BodyInterface.SetMotionType(JPH::BodyID(BodyID), JoltUtils::ToJoltMotionType(NewType), JPH::EActivation::Activate);
    }

    void FJoltPhysicsScene::SnapshotBodyStates()
    {
        LUMINA_PROFILE_SCOPE();

        const JPH::BodyLockInterfaceNoLock& LockInterface = JoltSystem->GetBodyLockInterfaceNoLock();
        entt::registry& Registry = World->GetEntityRegistry();
        
        auto RigidView = Registry.view<SRigidBodyComponent>();
        auto RigidHandle = RigidView.handle();
        Task::ParallelFor(RigidHandle->size(), [&](uint32 Index)
        {
            entt::entity Entity = (*RigidHandle)[Index];

            // Reflected components are in_place_delete: the packed array has tombstone holes that raw
            // indexing/get() walks out of bounds. each() skips them, manual indexing must too. Do NOT remove.
            if (Entity == entt::tombstone)
            {
                return;
            }

            SRigidBodyComponent& BodyComponent = RigidView.get<SRigidBodyComponent>(Entity);

            const JPH::Body* Body = LockInterface.TryGetBody(JPH::BodyID(BodyComponent.BodyID));
            if (Body == nullptr || Body->IsStatic())
            {
                return;
            }

            BodyComponent.LastBodyPosition = JoltUtils::FromJPHVec3(Body->GetPosition());
            BodyComponent.LastBodyRotation = JoltUtils::FromJPHQuat(Body->GetRotation());
        });

        auto CharView = Registry.view<SCharacterPhysicsComponent>();
        CharView.each([&](SCharacterPhysicsComponent& CharComponent)
        {
            if (CharComponent.Character == nullptr)
            {
                return;
            }

            CharComponent.LastBodyPosition = JoltUtils::FromJPHVec3(CharComponent.Character->Ref->GetPosition());
            CharComponent.LastBodyRotation = JoltUtils::FromJPHQuat(CharComponent.Character->Ref->GetRotation());
        });
    }

    // In-place SoA quaternion nlerp toward Prev along the shortest arc, 8 quats per iteration (Curr* in/out).
    // nlerp not slerp: per-frame alpha is tiny so the difference is invisible, and it drops the per-body acos/sin.
    static void NlerpQuatsSoA(float* Qx, float* Qy, float* Qz, float* Qw,
                              const float* Px, const float* Py, const float* Pz, const float* Pw,
                              uint32 Count, float Alpha)
    {
        using namespace SIMD;
        const VFloat8 A         = VFloat8::Broadcast(Alpha);
        const VFloat8 OneMinusA = VFloat8::Broadcast(1.0f - Alpha);
        const VFloat8 VZero     = VFloat8::Zero();

        uint32 i = 0;
        for (; i + 8 <= Count; i += 8)
        {
            const VFloat8 px = VFloat8::Load(Px + i), py = VFloat8::Load(Py + i),
                          pz = VFloat8::Load(Pz + i), pw = VFloat8::Load(Pw + i);
            VFloat8 cx = VFloat8::Load(Qx + i), cy = VFloat8::Load(Qy + i),
                    cz = VFloat8::Load(Qz + i), cw = VFloat8::Load(Qw + i);

            const VFloat8 Dot  = MulAdd(px, cx, MulAdd(py, cy, MulAdd(pz, cz, pw * cw)));
            const VFloat8 Flip = CmpLt(Dot, VZero);
            cx = Select(Flip, -cx, cx); cy = Select(Flip, -cy, cy);
            cz = Select(Flip, -cz, cz); cw = Select(Flip, -cw, cw);

            VFloat8 ox = MulAdd(px, OneMinusA, cx * A);
            VFloat8 oy = MulAdd(py, OneMinusA, cy * A);
            VFloat8 oz = MulAdd(pz, OneMinusA, cz * A);
            VFloat8 ow = MulAdd(pw, OneMinusA, cw * A);

            const VFloat8 Inv = InvSqrt(MulAdd(ox, ox, MulAdd(oy, oy, MulAdd(oz, oz, ow * ow))));
            (ox * Inv).Store(Qx + i); (oy * Inv).Store(Qy + i);
            (oz * Inv).Store(Qz + i); (ow * Inv).Store(Qw + i);
        }

        const float OneMinus = 1.0f - Alpha;
        for (; i < Count; ++i)
        {
            const float px = Px[i], py = Py[i], pz = Pz[i], pw = Pw[i];
            float cx = Qx[i], cy = Qy[i], cz = Qz[i], cw = Qw[i];
            if (px * cx + py * cy + pz * cz + pw * cw < 0.0f) { cx = -cx; cy = -cy; cz = -cz; cw = -cw; }

            const float ox = px * OneMinus + cx * Alpha, oy = py * OneMinus + cy * Alpha,
                        oz = pz * OneMinus + cz * Alpha, ow = pw * OneMinus + cw * Alpha;
            const float Inv = 1.0f / std::sqrt(ox * ox + oy * oy + oz * oz + ow * ow);
            Qx[i] = ox * Inv; Qy[i] = oy * Inv; Qz[i] = oz * Inv; Qw[i] = ow * Inv;
        }
    }

    void FJoltPhysicsScene::BuildInterpolatedTransforms(float Alpha)
    {
        LUMINA_PROFILE_SCOPE();

        const float KillHeight = World->GetDefaultWorldSettings().WorldKillHeight;
        const JPH::BodyLockInterfaceNoLock& LockInterface = JoltSystem->GetBodyLockInterfaceNoLock();
        entt::registry& Registry = World->GetEntityRegistry();

        auto RigidView   = Registry.view<SRigidBodyComponent>();
        auto RigidHandle = RigidView.handle();
        const uint32 RigidCount = (uint32)RigidHandle->size();

        auto CharView = Registry.view<SCharacterPhysicsComponent>();
        const uint32 CharCount = (uint32)CharView.handle()->size();

        const uint32 Total = RigidCount + CharCount;
        InterpStaging.Resize(Total);

        // Identity-fill a skipped/killed slot so the bulk interp never sees NaNs
        // (physics builds enable FP exceptions).
        auto WriteIdentity = [&](uint32 i)
        {
            InterpStaging.PrevPos[i] = FVector3(0.0f);
            InterpStaging.CurrPos[i] = FVector3(0.0f);
            InterpStaging.PrevQx[i] = InterpStaging.CurrQx[i] = 0.0f;
            InterpStaging.PrevQy[i] = InterpStaging.CurrQy[i] = 0.0f;
            InterpStaging.PrevQz[i] = InterpStaging.CurrQz[i] = 0.0f;
            InterpStaging.PrevQw[i] = InterpStaging.CurrQw[i] = 1.0f;
        };

        auto StoreState = [&](uint32 i, const FVector3& Prev, const FVector3& Curr, const FQuat& PrevR, const FQuat& CurrR)
        {
            InterpStaging.Flags[i]   = EInterpFlag::Interpolate;
            InterpStaging.PrevPos[i] = Prev;   InterpStaging.CurrPos[i] = Curr;
            InterpStaging.PrevQx[i] = PrevR.x; InterpStaging.PrevQy[i] = PrevR.y; InterpStaging.PrevQz[i] = PrevR.z; InterpStaging.PrevQw[i] = PrevR.w;
            InterpStaging.CurrQx[i] = CurrR.x; InterpStaging.CurrQy[i] = CurrR.y; InterpStaging.CurrQz[i] = CurrR.z; InterpStaging.CurrQw[i] = CurrR.w;
        };

        // Gather prev (component snapshot) + curr (Jolt) into SoA; no interp math, this pass is body-lookup bound.
        Task::ParallelFor(RigidCount, [&](uint32 i)
        {
            const entt::entity Entity = (*RigidHandle)[i];
            InterpStaging.Entities[i] = Entity;

            // In_place_delete leaves tombstone holes in the packed array (see SnapshotBodyStates);
            // skip them, identity-filling the slot so the bulk SIMD pass never sees NaNs.
            if (Entity == entt::tombstone)
            {
                InterpStaging.Flags[i] = EInterpFlag::Skip;
                WriteIdentity(i);
                return;
            }

            const SRigidBodyComponent& BodyComponent = RigidView.get<SRigidBodyComponent>(Entity);
            const JPH::Body* Body = LockInterface.TryGetBody(JPH::BodyID(BodyComponent.BodyID));
            if (Body == nullptr || Body->IsStatic())
            {
                InterpStaging.Flags[i] = EInterpFlag::Skip;
                WriteIdentity(i);
                return;
            }

            const JPH::RVec3 CurrPos = Body->GetPosition();
            if (CurrPos.GetY() < KillHeight)
            {
                InterpStaging.Flags[i] = EInterpFlag::BelowKill;
                WriteIdentity(i);
                return;
            }

            StoreState(i, BodyComponent.LastBodyPosition, JoltUtils::FromJPHVec3(CurrPos),
                          BodyComponent.LastBodyRotation, JoltUtils::FromJPHQuat(Body->GetRotation()));
        });

        // Characters are few -- gather them serially into the tail of the batch.
        uint32 ci = RigidCount;
        CharView.each([&](entt::entity Entity, SCharacterPhysicsComponent& CharacterComponent)
        {
            const uint32 i = ci++;
            InterpStaging.Entities[i] = Entity;

            if (CharacterComponent.Character == nullptr)
            {
                InterpStaging.Flags[i] = EInterpFlag::Skip;
                WriteIdentity(i);
                return;
            }

            const JPH::RVec3 CurrPos = CharacterComponent.Character->Ref->GetPosition();
            if (CurrPos.GetY() < KillHeight)
            {
                InterpStaging.Flags[i] = EInterpFlag::BelowKill;
                WriteIdentity(i);
                return;
            }

            StoreState(i, CharacterComponent.LastBodyPosition, JoltUtils::FromJPHVec3(CurrPos),
                          CharacterComponent.LastBodyRotation, JoltUtils::FromJPHQuat(CharacterComponent.Character->Ref->GetRotation()));
        });

        // Interp: one SIMD pass over the whole batch -- position lerp + quat nlerp,
        // both in place (Curr* become the result).
        if (Total > 0)
        {
            SIMD::LerpArray(reinterpret_cast<float*>(InterpStaging.CurrPos.data()),
                            reinterpret_cast<const float*>(InterpStaging.PrevPos.data()),
                            reinterpret_cast<const float*>(InterpStaging.CurrPos.data()),
                            int(Total) * 3, Alpha);

            NlerpQuatsSoA(InterpStaging.CurrQx.data(), InterpStaging.CurrQy.data(),
                          InterpStaging.CurrQz.data(), InterpStaging.CurrQw.data(),
                          InterpStaging.PrevQx.data(), InterpStaging.PrevQy.data(),
                          InterpStaging.PrevQz.data(), InterpStaging.PrevQw.data(),
                          Total, Alpha);
        }
    }

    void FJoltPhysicsScene::ApplyInterpolatedTransforms()
    {
        LUMINA_PROFILE_SCOPE();

        const uint32 Count = (uint32)InterpStaging.Entities.size();
        if (Count == 0)
        {
            return;
        }

        entt::registry& Registry = World->GetEntityRegistry();
        auto& TransformStorage = Registry.storage<STransformComponent>();

        for (uint32 i = 0; i < Count; ++i)
        {
            const EInterpFlag Flag    = InterpStaging.Flags[i];
            const entt::entity Entity = InterpStaging.Entities[i];

            if (Flag == EInterpFlag::Skip || !Registry.valid(Entity))
            {
                continue;
            }

            if (Flag == EInterpFlag::BelowKill)
            {
                Registry.destroy(Entity);
                continue;
            }

            if (!TransformStorage.contains(Entity))
            {
                continue;
            }

            STransformComponent& TransformComponent = TransformStorage.get(Entity);
            TransformComponent.SetFromPhysics(InterpStaging.CurrPos[i],
                FQuat(InterpStaging.CurrQw[i], InterpStaging.CurrQx[i], InterpStaging.CurrQy[i], InterpStaging.CurrQz[i]));
            Registry.emplace_or_replace<FNeedsTransformUpdate>(Entity);
        }

        ECS::Utils::ResolveAllDirtyTransforms(Registry);
    }

    void FJoltPhysicsScene::LatchCharacterInput()
    {
        LUMINA_PROFILE_SCOPE();

        entt::registry& Registry = World->GetEntityRegistry();
        auto View = Registry.view<SCharacterControllerComponent, SCharacterMovementComponent>();

        View.each([&](SCharacterControllerComponent& Controller, SCharacterMovementComponent& Movement)
        {
            if (Math::LengthSquared(Controller.MoveInput) > LE_SMALL_NUMBER)
            {
                FVector3 Forward = RenderUtils::GetForwardVector(Controller.LookInput.x, 0.0f);
                FVector3 Right   = RenderUtils::GetRightVector(Controller.LookInput.x);
                FVector3 Up      = Math::Cross(Right, Forward);

                FVector3 Direction = Right * Controller.MoveInput.x + Up * Controller.MoveInput.y + Forward * Controller.MoveInput.z;
                const float Magnitude = Math::Length(Direction);
                if (Magnitude > LE_SMALL_NUMBER)
                {
                    Movement.PendingMoveDirection = Direction / Magnitude;
                    Movement.PendingMoveThrottle  = Math::Min(Magnitude, 1.0f);
                    Movement.bHasPendingMoveInput = true;
                }
                else
                {
                    Movement.PendingMoveDirection = FVector3(0.0f);
                    Movement.bHasPendingMoveInput = false;
                }
            }
            else
            {
                Movement.PendingMoveDirection = FVector3(0.0f);
                Movement.bHasPendingMoveInput = false;
            }

            Movement.PendingLookYaw = Controller.LookInput.x;
            Controller.MoveInput    = {};

            if (Controller.bJumpPressed)
            {
                Movement.bPendingJump    = true;
                Controller.bJumpPressed  = false;
            }

            if (Controller.bLaunchRequested)
            {
                Controller.bLaunchRequested        = false;
                Movement.PendingLaunchVelocity     = Controller.PendingLaunchVelocity;
                Movement.bLaunchOverrideHorizontal = Controller.bLaunchOverrideHorizontal;
                Movement.bLaunchOverrideVertical   = Controller.bLaunchOverrideVertical;
                Movement.bPendingLaunch            = true;
            }

            if (Controller.bTeleportRequested)
            {
                Controller.bTeleportRequested    = false;
                Movement.PendingTeleportLocation = Controller.PendingTeleportLocation;
                Movement.bPendingTeleport        = true;
            }
        });
    }

    void FJoltPhysicsScene::UpdateCharacters(float FixedDt)
    {
        LUMINA_PROFILE_SCOPE();

        entt::registry& Registry = World->GetEntityRegistry();
        JPH::PhysicsSystem* PhysicsSystem = JoltSystem.get();

        auto View = Registry.view<SCharacterPhysicsComponent, SCharacterMovementComponent>();
        auto Handle = View.handle();
        const uint32 Count = (uint32)Handle->size();
        if (Count == 0)
        {
            return;
        }
        
        constexpr uint32 ParallelMinCount = 8;

        auto SubStep = [&](entt::entity Entity, JPH::TempAllocator& InAllocator)
        {
            LUMINA_PROFILE_SECTION("Character Sub-Step");

            if (!View.contains(Entity))
            {
                return;
            }

            SCharacterPhysicsComponent&  Physics  = View.get<SCharacterPhysicsComponent>(Entity);
            SCharacterMovementComponent& Movement = View.get<SCharacterMovementComponent>(Entity);

            JPH::CharacterVirtual* Character = Physics.Character ? Physics.Character->Ref.GetPtr() : nullptr;
            if (Character == nullptr)
            {
                return;
            }

            // Teleport before integration: move capsule, zero velocity, reseed interp snapshot so the render
            // transform doesn't streak across the jump.
            if (Movement.bPendingTeleport)
            {
                Movement.bPendingTeleport = false;

                const FVector3 TeleportLocation = Movement.PendingTeleportLocation;
                Character->SetPosition(JoltUtils::ToJPHRVec3(TeleportLocation));
                Character->SetLinearVelocity(JPH::Vec3::sZero());
                Movement.Velocity        = FVector3(0.0f);
                Physics.LastBodyPosition = TeleportLocation;
                return;
            }

            const bool bHasMovementInput = Movement.bHasPendingMoveInput;
            const FVector3 DesiredDirection = Movement.PendingMoveDirection;

            const float    TargetSpeed    = bHasMovementInput ? Movement.MoveSpeed * Movement.PendingMoveThrottle : 0.0f;
            const FVector3 TargetVelocity = DesiredDirection * TargetSpeed;

            JPH::CharacterVirtual::ExtendedUpdateSettings UpdateSettings;
            UpdateSettings.mStickToFloorStepDown = JPH::Vec3(0.0f, -0.5f, 0.0f);
            UpdateSettings.mWalkStairsStepUp     = JPH::Vec3(0.0f, Physics.StepHeight, 0.0f);

            const JPH::CharacterVirtual::EGroundState GroundState = Character->GetGroundState();
            const bool bWasGrounded = Movement.bGrounded;
            Movement.bGrounded = (GroundState == JPH::CharacterVirtual::EGroundState::OnGround);

            if (!bWasGrounded && Movement.bGrounded)
            {
                Movement.JumpCount = 0;
            }
            
            FQuat TargetRotation = JoltUtils::FromJPHQuat(Character->GetRotation());
            if (Movement.bUseControllerRotation)
            {
                TargetRotation = FQuat(FVector3(0.0f, Math::Radians(Movement.PendingLookYaw), 0.0f));
            }
            else if (Movement.bOrientRotationToMovement && bHasMovementInput)
            {
                float TargetYaw    = Math::Atan2(DesiredDirection.x, DesiredDirection.z);
                FQuat Rotation = FQuat(FVector3(0.0f, TargetYaw, 0.0f));
                TargetRotation     = Math::Slerp(TargetRotation, Rotation, Math::Clamp(Movement.RotationRate * FixedDt, 0.0f, 1.0f));
            }

            // Horizontal velocity integration with accel / friction / air drag.
            FVector3 HorizontalVelocity(Movement.Velocity.x, 0.0f, Movement.Velocity.z);
            const float CurrentSpeed = Math::Length(HorizontalVelocity);

            if (bHasMovementInput)
            {
                // Airborne steering is scaled by AirControl (0 = no air steering).
                const float Accel = Movement.bGrounded ? Movement.Acceleration
                                                       : Movement.Acceleration * Movement.AirControl;
                const float Blend = Math::Clamp(Accel * FixedDt, 0.0f, 1.0f);
                HorizontalVelocity = Math::Mix(HorizontalVelocity, TargetVelocity, Blend);
            }
            else if (Movement.bGrounded)
            {
                const float DecelerationAmount = Movement.Deceleration * FixedDt;
                const float NewSpeed           = Math::Max(0.0f, CurrentSpeed - DecelerationAmount);

                if (CurrentSpeed > 0.001f)
                {
                    HorizontalVelocity = Math::Normalize(HorizontalVelocity) * NewSpeed;
                }
                else
                {
                    HorizontalVelocity = FVector3(0.0f);
                }

                const float Friction = Math::Max(0.0f, 1.0f - Movement.GroundFriction * FixedDt);
                HorizontalVelocity *= Friction;
            }
            else
            {
                const float AirFriction = Math::Max(0.0f, 1.0f - (Movement.GroundFriction * 0.1f) * FixedDt);
                HorizontalVelocity *= AirFriction;
            }

            Movement.Velocity.x = HorizontalVelocity.x;
            Movement.Velocity.z = HorizontalVelocity.z;

            if (Movement.bGrounded)
            {
                const JPH::Vec3 GroundVelocity = Character->GetGroundVelocity();
                Movement.Velocity.x += GroundVelocity.GetX();
                Movement.Velocity.y  = GroundVelocity.GetY();
                Movement.Velocity.z += GroundVelocity.GetZ();
            }
            else
            {
                Movement.Velocity.y += Movement.Gravity * FixedDt;
            }

            if (Movement.bPendingJump)
            {
                Movement.bPendingJump = false;

                if (Movement.JumpCount < Movement.MaxJumpCount)
                {
                    Movement.Velocity.y = Movement.JumpSpeed;
                    Movement.JumpCount++;
                }
            }

            // Launch applied after the ground-velocity block and jump so an
            // upward impulse survives even when the character is still grounded.
            if (Movement.bPendingLaunch)
            {
                Movement.bPendingLaunch = false;

                if (Movement.bLaunchOverrideHorizontal)
                {
                    Movement.Velocity.x = Movement.PendingLaunchVelocity.x;
                    Movement.Velocity.z = Movement.PendingLaunchVelocity.z;
                }
                else
                {
                    Movement.Velocity.x += Movement.PendingLaunchVelocity.x;
                    Movement.Velocity.z += Movement.PendingLaunchVelocity.z;
                }

                if (Movement.bLaunchOverrideVertical)
                {
                    Movement.Velocity.y = Movement.PendingLaunchVelocity.y;
                }
                else
                {
                    Movement.Velocity.y += Movement.PendingLaunchVelocity.y;
                }
            }

            Character->SetRotation(JoltUtils::ToJPHQuat(TargetRotation));
            Character->SetLinearVelocity(JoltUtils::ToJPHVec3(Movement.Velocity));

            const JPH::ObjectLayer Layer = JoltUtils::PackToObjectLayer(Physics.CollisionProfile);

            Character->ExtendedUpdate(
                FixedDt,
                JPH::Vec3(0.0f, Movement.Gravity, 0.0f),
                UpdateSettings,
                PhysicsSystem->GetDefaultBroadPhaseLayerFilter(Layer),
                PhysicsSystem->GetDefaultLayerFilter(Layer),
                {},
                {},
                InAllocator);

            Movement.Velocity = JoltUtils::FromJPHVec3(Character->GetLinearVelocity());
        };

        if (Count < ParallelMinCount || CharacterAllocators.empty())
        {
            for (uint32 i = 0; i < Count; ++i)
            {
                SubStep((*Handle)[i], Allocator);
            }
            return;
        }

        const uint32 NumWorkers = (uint32)CharacterAllocators.size();
        Task::ParallelFor(Count, [&](uint32 Index, uint32 ThreadNum)
        {
            const uint32 AllocIdx = ThreadNum < NumWorkers ? ThreadNum : 0u;
            SubStep((*Handle)[Index], *CharacterAllocators[AllocIdx]);
        });
    }

    uint32 FJoltPhysicsScene::GetEntityBodyID(entt::entity Entity)
    {
        auto* RigidBody = World->GetEntityRegistry().try_get<SRigidBodyComponent>(Entity);
        return RigidBody ? RigidBody->BodyID : JPH::BodyID::cInvalidBodyID;
    }

    TOptional<SRayResult> FJoltPhysicsScene::CastRay(const SRayCastSettings& Settings)
    {
        LUMINA_PROFILE_SCOPE();

        JPH::Vec3 JPHStart  = JoltUtils::ToJPHVec3(Settings.Start);
        JPH::Vec3 JPHEnd    = JoltUtils::ToJPHVec3(Settings.End);
        JPH::Vec3 Direction = JPHEnd - JPHStart;
        
        if (Direction.Length() < LE_SMALL_NUMBER)
        {
            return eastl::nullopt;
        }
        
        JPH::RRayCast Ray;
        Ray.mOrigin = JPHStart;
        Ray.mDirection = Direction;
        
        class LayerMaskFilter : public JPH::ObjectLayerFilter
        {
        public:
            LayerMaskFilter(uint32 InLayerMask) 
                : LayerMask(InLayerMask) {}

            bool ShouldCollide(JPH::ObjectLayer InLayer) const override
            {
                ECollisionProfiles LayerA = (ECollisionProfiles)(uint16)(LayerMask & 0xFFFF);
                ECollisionProfiles MaskA  = (ECollisionProfiles)(uint16)(LayerMask >> 16);
    
                ECollisionProfiles LayerB = (ECollisionProfiles)(uint16)(InLayer & 0xFFFF);
                ECollisionProfiles MaskB  = (ECollisionProfiles)(uint16)(InLayer >> 16);

                return (MaskA & LayerB) != (ECollisionProfiles)0 || (MaskB & LayerA) != (ECollisionProfiles)0;
            }

            uint32 LayerMask;
        };
        
        FIgnoreFilter IgnoreFilter{Settings.IgnoreBodies};
        
        JPH::RayCastResult Hit;
        
        LayerMaskFilter LayerFilter{(uint32)Settings.LayerMask};

        bool bHit = JoltSystem->GetNarrowPhaseQuery().CastRay(Ray, Hit, {}, LayerFilter, IgnoreFilter);
        if (!bHit)
        {
            return eastl::nullopt;
        }
        
        const JPH::BodyLockInterfaceNoLock& LockInterface = JoltSystem->GetBodyLockInterfaceNoLock();
        
        JPH::Body* Body = LockInterface.TryGetBody(Hit.mBodyID);
        if (!Body)
        {
            return eastl::nullopt;
        }
        
        JPH::Vec3 SurfaceNormal = Body->GetWorldSpaceSurfaceNormal(Hit.mSubShapeID2, Ray.GetPointOnRay(Hit.mFraction));
        
        FVector3 Distance  = (Settings.Start - Settings.End);
        float Length        = Math::Length(Distance);
        
        SRayResult Result
        {
            .BodyID     = Hit.mBodyID.GetIndexAndSequenceNumber(),
            .Entity     = static_cast<uint32>(Body->GetUserData()),
            .Start      = Settings.Start,
            .End        = Settings.End,
            .Location   = JoltUtils::FromJPHRVec3(Ray.GetPointOnRay(Hit.mFraction)),
            .Normal     = Math::Normalize(JoltUtils::FromJPHVec3(SurfaceNormal)),
            .Fraction   = Hit.mFraction,
            .Distance   = Hit.mFraction * Length,
        };
        
        return Result;
    }

    TVector<SRayResult> FJoltPhysicsScene::CastSphere(const SSphereCastSettings& Settings)
    {
        LUMINA_PROFILE_SCOPE();
        
        TVector<SRayResult> Results;
        if (Math::IsNearlyZero(Settings.Radius))
        {
            return Results;
        }
        
        JPH::RVec3 JPHStart  = JoltUtils::ToJPHRVec3(Settings.Start);
        JPH::RVec3 JPHEnd    = JoltUtils::ToJPHRVec3(Settings.End);
        JPH::Vec3 Direction = (JPHEnd - JPHStart);
        
        class FMyCollector : public JPH::CastShapeCollector
        {
        public:
            
            FMyCollector(TVector<SRayResult>& OutResults, const SSphereCastSettings& InSettings, const JPH::BodyLockInterfaceNoLock& NoLock)
                : Out(OutResults)
                , Settings(InSettings)
                , Lock(NoLock)
            {}
            
            TVector<SRayResult>& Out;
            const SSphereCastSettings& Settings;
            const JPH::BodyLockInterfaceNoLock& Lock;
            
            void AddHit(const JPH::ShapeCastResult& Hit) override
            {
                const JPH::Body* Body = Lock.TryGetBody(Hit.mBodyID2);
                
                SRayResult R;
                R.BodyID   = Hit.mBodyID2.GetIndexAndSequenceNumber();
                R.Entity   = (uint32)Body->GetUserData();
                R.Start    = Settings.Start;
                R.End      = Settings.End;
                R.Location = JoltUtils::FromJPHVec3(Hit.mContactPointOn2);
                R.Normal   = Math::Normalize(JoltUtils::FromJPHVec3(Hit.mPenetrationAxis.Normalized()));
                R.Fraction = Hit.mFraction;

                Out.emplace_back(Move(R));
            }
        };
        
        const JPH::BodyLockInterfaceNoLock& Lock = JoltSystem->GetBodyLockInterfaceNoLock();
        
        
        FMyCollector Collector(Results, Settings, Lock);
        FIgnoreFilter Filter{Settings.IgnoreBodies};
        
        JPH::SphereShape QuerySphere(Settings.Radius);
        JPH::RShapeCast ShapeCast = JPH::RShapeCast::sFromWorldTransform(&QuerySphere, JPH::Vec3::sReplicate(1.0f), JPH::RMat44::sTranslation(JPHStart), Direction);
        
        JPH::ShapeCastSettings ShapeSettings;
        ShapeSettings.mBackFaceModeTriangles    = JPH::EBackFaceMode::CollideWithBackFaces;
        ShapeSettings.mBackFaceModeConvex       = JPH::EBackFaceMode::CollideWithBackFaces;
        ShapeSettings.mReturnDeepestPoint       = false;
        
        
        JoltSystem->GetNarrowPhaseQuery().CastShape(
            ShapeCast,
            ShapeSettings,
            JPH::RVec3::sZero(),
            Collector,
            JPH::BroadPhaseLayerFilter(),
            JPH::ObjectLayerFilter(),
            Filter,
            JPH::ShapeFilter()
        );
        
        #if JPH_DEBUG_RENDERER
        DEFER 
        { 
            FJoltPhysicsContext::GetDebugRenderer()->SetDrawDuration(0.0f); 
        };
        
        FJoltPhysicsContext::GetDebugRenderer()->SetWorld(World);
        FJoltPhysicsContext::GetDebugRenderer()->SetDrawDuration(Settings.DebugDuration);
        
        if (Settings.bDrawDebug)
        {
            QuerySphere.Draw(FJoltPhysicsContext::GetDebugRenderer(),
                JPH::RMat44::sTranslation(JPHStart), 
                JPH::Vec3::sReplicate(1.0f), 
                JPH::Color(0, 255, 0, 255),
                false, 
                true);
            
            if (Results.empty())
            {
                QuerySphere.Draw(FJoltPhysicsContext::GetDebugRenderer(),
                JPH::RMat44::sTranslation(JPHStart), 
                JPH::Vec3::sReplicate(1.0f), 
                JPH::Color(255, 0, 0, 255),
                false, 
                true);
            
                return Results;
            }
        }
        #endif
        
        eastl::sort(Results.begin(), Results.end(), [](const SRayResult& A, const SRayResult& B)
        {
            return A.Fraction < B.Fraction;
        });
        
        return Results;
    }

    void FJoltPhysicsScene::EnsureCharacterAllocators()
    {
        if (!CharacterAllocators.empty())
        {
            return;
        }

        // First character in this scene: stand up the per-worker substep allocator pool. 4 MiB
        // per worker is comfortably above a single CharacterVirtual::ExtendedUpdate's high-water.
        const uint32 NumWorkers = GTaskSystem ? GTaskSystem->GetScheduler().GetNumTaskThreads() : 1u;
        CharacterAllocators.reserve(NumWorkers);
        for (uint32 i = 0; i < NumWorkers; ++i)
        {
            CharacterAllocators.push_back(MakeUnique<JPH::TempAllocatorImpl>(4ull * 1024 * 1024));
        }
    }

    void FJoltPhysicsScene::OnCharacterComponentConstructed(entt::registry& Registry, entt::entity Entity)
    {
        LUMINA_PROFILE_SCOPE();

        EnsureCharacterAllocators();

        SCharacterPhysicsComponent& CharacterComponent = Registry.get<SCharacterPhysicsComponent>(Entity);
        STransformComponent& TransformComponent = Registry.get<STransformComponent>(Entity);
        
        auto Result = JPH::RotatedTranslatedShapeSettings(
            JPH::Vec3(0, 0, 0),
            JPH::Quat::sIdentity(),
            Memory::New<JPH::CapsuleShape>(CharacterComponent.HalfHeight, CharacterComponent.Radius * TransformComponent.MaxScale())).Create();

        if (Result.HasError())
        {
            LOG_ERROR("Failed to create Character for entity: {} - {}", entt::to_integral(Entity), Result.GetError());
            return;
        }

        const JPH::Ref<JPH::Shape>& StandingShape = Result.Get();
        
        JPH::Ref Settings                       = Memory::New<JPH::CharacterVirtualSettings>();
        Settings->mShape                        = StandingShape;
        Settings->mInnerBodyShape               = StandingShape;
        Settings->mInnerBodyLayer               = JoltUtils::PackToObjectLayer(CharacterComponent.CollisionProfile);
        Settings->mMass                         = CharacterComponent.Mass;
        Settings->mMaxStrength                  = CharacterComponent.MaxStrength;
        Settings->mMinTimeRemaining             = CharacterComponent.MinTimeRemaining;
        Settings->mMaxConstraintIterations      = CharacterComponent.MaxConstraintIterations;
        Settings->mMaxCollisionIterations       = CharacterComponent.MaxCollisionIterations;
        Settings->mCollisionTolerance           = CharacterComponent.CollisionTolerance;
        Settings->mMaxNumHits                   = CharacterComponent.MaxNumHits;
        Settings->mHitReductionCosMaxAngle      = CharacterComponent.HitReductionCosMaxAngle;
        Settings->mCharacterPadding             = CharacterComponent.Padding;
        Settings->mPenetrationRecoverySpeed     = CharacterComponent.PenetrationRecoverySpeed;
        Settings->mPredictiveContactDistance    = CharacterComponent.PredictiveContactDistance;
        Settings->mSupportingVolume             = JPH::Plane(JPH::Vec3::sAxisY(), 0.0f);
        
        JPH::Ref<JPH::CharacterVirtual> Character = Memory::New<JPH::CharacterVirtual>(Settings,
            JoltUtils::ToJPHRVec3(TransformComponent.GetLocation()),
            JoltUtils::ToJPHQuat(TransformComponent.GetRotation()),
            0,
            JoltSystem.get());

        JPH::BodyInterface& BodyInterface = JoltSystem->GetBodyInterface();
        BodyInterface.SetUserData(Character->GetInnerBodyID(), entt::to_integral(Entity));

        // Wrap into the pimpl handle the component owns (component header
        // doesn't see <Jolt/...> directly).
        CharacterComponent.Character        = MakeShared<FJoltCharacterHandle>();
        CharacterComponent.Character->Ref   = Move(Character);

        // Seed interpolation state so the first frame blends from a valid quaternion.
        CharacterComponent.LastBodyPosition = TransformComponent.GetLocation();
        CharacterComponent.LastBodyRotation = TransformComponent.GetRotation();
    }

    void FJoltPhysicsScene::OnCharacterComponentDestroyed(entt::registry& Registry, entt::entity Entity)
    {
        
    }

    void FJoltPhysicsScene::OnRigidBodyComponentUpdated(entt::registry& Registry, entt::entity Entity)
    {
        
    }

    static JPH::ShapeRefC BuildMeshColliderShape(const CMesh* Mesh, const FVector3& Scale, bool bConvex)
    {
        const FMeshResource& Resource = Mesh->GetMeshResource();
        const FMeshletData&  MD       = Resource.MeshletData;

        if (MD.IsEmpty() || Resource.bSkinnedMesh)
        {
            return nullptr;
        }

        JPH::VertexList         Vertices;
        JPH::IndexedTriangleList Triangles;

        Mesh->ForEachSurface([&](const FGeometrySurface& Surface, uint32)
        {
            const uint32 Offset = Surface.LODMeshletOffset[0];
            const uint32 Count  = Surface.LODMeshletCount[0];
            for (uint32 i = 0; i < Count; ++i)
            {
                const FMeshlet& M       = MD.Meshlets[Offset + i];
                const uint32 BaseVertex = (uint32)Vertices.size();

                for (uint32 v = 0; v < M.VertexCount; ++v)
                {
                    const FMeshletVertex& MV = MD.MeshletVertices[M.VertexOffset + v];
                    const uint32 P  = MV.Position;
                    const float qx  = (float)( P        & 0x3FFu);
                    const float qy  = (float)((P >> 10) & 0x3FFu);
                    const float qz  = (float)((P >> 20) & 0x3FFu);
                    FVector3 Pos = MD.MeshOrigin[M.LODIndex] + (FVector3(M.LoInt) + FVector3(qx, qy, qz)) * MD.MeshGridStep[M.LODIndex];
                    Pos *= Scale;
                    Vertices.push_back(JPH::Float3(Pos.x, Pos.y, Pos.z));
                }

                for (uint32 t = 0; t < M.TriangleCount; ++t)
                {
                    const uint32 Packed = MD.MeshletTriangles[M.TriangleOffset + t];
                    const uint32 i0 = (Packed      ) & 0xFFu;
                    const uint32 i1 = (Packed >>  8) & 0xFFu;
                    const uint32 i2 = (Packed >> 16) & 0xFFu;
                    Triangles.emplace_back(BaseVertex + i0, BaseVertex + i1, BaseVertex + i2, 0);
                }
            }
        });

        if (Vertices.empty() || Triangles.empty())
        {
            return nullptr;
        }

        if (bConvex)
        {
            JPH::Array<JPH::Vec3> Points;
            Points.reserve(Vertices.size());
            for (const JPH::Float3& V : Vertices)
            {
                Points.emplace_back(V.x, V.y, V.z);
            }

            JPH::ConvexHullShapeSettings Settings(Points);
            Settings.SetEmbedded();
            auto Result = Settings.Create();
            if (Result.HasError())
            {
                LOG_ERROR("Failed to build convex hull from mesh '{}': {}", Mesh->GetName().ToString(), Result.GetError());
                return nullptr;
            }
            return Result.Get();
        }

        JPH::MeshShapeSettings Settings(std::move(Vertices), std::move(Triangles));
        Settings.SetEmbedded();
        auto Result = Settings.Create();
        if (Result.HasError())
        {
            LOG_ERROR("Failed to build triangle mesh from mesh '{}': {}", Mesh->GetName().ToString(), Result.GetError());
            return nullptr;
        }
        return Result.Get();
    }

    static JPH::ShapeRefC BuildTerrainHeightFieldShape(const STerrainComponent& Terrain)
    {
        const int32 Res = Terrain.Resolution;
        if (Res < 2 || (int64)Terrain.Heightmap.size() < (int64)Res * (int64)Res)
        {
            return nullptr;
        }

        // Jolt rounds mSampleCount up to mBlockSize multiples; buffer must match or Jolt reads OOB.
        constexpr uint32 BlockSize = 2;
        const uint32 SampleCount   = ((uint32(Res) + BlockSize - 1) / BlockSize) * BlockSize;
        const float   Stride       = Terrain.TileWorldSize / float(Res - 1);
        const float   HalfSize     = Terrain.TileWorldSize * 0.5f;

        TVector<float> Samples;
        Samples.resize(size_t(SampleCount) * size_t(SampleCount));

        // Copy heightmap, scaling normalized [0,1] to world Y; replicate edge for padding rows/cols.
        for (uint32 Y = 0; Y < SampleCount; ++Y)
        {
            const uint32 SrcY = Y < uint32(Res) ? Y : uint32(Res - 1);
            for (uint32 X = 0; X < SampleCount; ++X)
            {
                const uint32 SrcX = X < uint32(Res) ? X : uint32(Res - 1);
                Samples[size_t(Y) * SampleCount + X] = Terrain.Heightmap[size_t(SrcY) * size_t(Res) + size_t(SrcX)] * Terrain.MaxHeight;
            }
        }

        JPH::HeightFieldShapeSettings Settings(
            Samples.data(),
            JPH::Vec3(-HalfSize, 0.0f, -HalfSize),
            JPH::Vec3(Stride, 1.0f, Stride),
            SampleCount);
        Settings.mBlockSize = BlockSize;
        Settings.SetEmbedded();

        auto Result = Settings.Create();
        if (Result.HasError())
        {
            LOG_ERROR("Failed to build terrain heightfield shape: {}", Result.GetError());
            return nullptr;
        }
        return Result.Get();
    }

    // Outcome of TryBuildRigidBodyCreationSettings: drives whether the caller
    // commits the body, retries later, or drops the entity.
    enum class EBodyBuildStatus : uint8
    {
        Success,        // Result populated; safe to CreateBody and add.
        Defer,          // Asset/transform not ready; push to PendingRigidBodyCreations.
        AlreadyExists,  // Component already owns a BodyID; skip.
        NoCollider,     // No collider component attached; nothing to build (caller decides whether to defer).
        Error,          // Shape build failed and was logged; do not retry.
    };

    struct FRigidBodyBuildResult
    {
        JPH::BodyCreationSettings   Settings;
        FVector3                   LastBodyPosition = FVector3(0.0f);
        FQuat                   LastBodyRotation = FQuat::Identity();
        // Resolved from the collider's PhysicsMaterial (if any), copied into BodyMaterials on commit.
        // Cached on the build result so the parallel build never touches the registry post-commit.
        bool                       bHasMaterial = false;
        float                      MaterialFriction = 0.0f;
        float                      MaterialRestitution = 0.0f;
        uint8                      MaterialFrictionCombine = 0;
        uint8                      MaterialRestitutionCombine = 0;
    };

    // Thread-safe: reads only registry + loaded assets; no PhysicsSystem mutation.
    static EBodyBuildStatus TryBuildRigidBodyCreationSettings(FJoltPhysicsScene* Scene, entt::registry& Registry, entt::entity Entity, FRigidBodyBuildResult& Out)
    {
        LUMINA_PROFILE_SCOPE();

        const SRigidBodyComponent* RigidBodyComponent = Registry.try_get<SRigidBodyComponent>(Entity);
        if (RigidBodyComponent == nullptr)
        {
            return EBodyBuildStatus::Error;
        }

        if (RigidBodyComponent->BodyID != JPH::BodyID::cInvalidBodyID)
        {
            return EBodyBuildStatus::AlreadyExists;
        }

        const STransformComponent* TransformComponent = Registry.try_get<STransformComponent>(Entity);
        if (TransformComponent == nullptr)
        {
            return EBodyBuildStatus::Defer;
        }

        JPH::ShapeRefC Shape;
        FVector3 ColliderTranslationOffset(0.0f);
        FVector3 ColliderRotationOffset(0.0f);
        bool      bIsTriangleMesh = false;
        bool      bColliderIsTrigger = false;
        const CPhysicsMaterial* ResolvedMaterial = nullptr;

        if (const SBoxColliderComponent* BC = Registry.try_get<SBoxColliderComponent>(Entity))
        {
            ColliderTranslationOffset       = BC->TranslationOffset;
            ColliderRotationOffset          = BC->RotationOffset;
            bColliderIsTrigger              = BC->bIsTrigger;
            ResolvedMaterial                = BC->PhysicsMaterial.Get();

            Shape = Scene->GetOrCreateBoxShape(BC->HalfExtent * TransformComponent->GetScale());
            if (Shape == nullptr)
            {
                LOG_ERROR("Failed to create BoxCollider Shape for Entity: {}", entt::to_integral(Entity));
                return EBodyBuildStatus::Error;
            }
        }
        else if (const SSphereColliderComponent* SC = Registry.try_get<SSphereColliderComponent>(Entity))
        {
            ColliderTranslationOffset           = SC->TranslationOffset;
            bColliderIsTrigger                  = SC->bIsTrigger;
            ResolvedMaterial                    = SC->PhysicsMaterial.Get();

            Shape = Scene->GetOrCreateSphereShape(SC->Radius * TransformComponent->MaxScale());
            if (Shape == nullptr)
            {
                LOG_ERROR("Failed to create SphereCollider Shape for Entity: {}", entt::to_integral(Entity));
                return EBodyBuildStatus::Error;
            }
        }
        else if (const SCapsuleColliderComponent* CC = Registry.try_get<SCapsuleColliderComponent>(Entity))
        {
            ColliderTranslationOffset           = CC->TranslationOffset;
            ColliderRotationOffset              = CC->RotationOffset;
            bColliderIsTrigger                  = CC->bIsTrigger;
            ResolvedMaterial                    = CC->PhysicsMaterial.Get();

            // Capsule scales uniformly with the entity (Jolt has no non-uniform capsule).
            const float ScaleFactor = TransformComponent->MaxScale();
            Shape = Scene->GetOrCreateCapsuleShape(CC->Radius * ScaleFactor, CC->HalfHeight * ScaleFactor);
            if (Shape == nullptr)
            {
                LOG_ERROR("Failed to create CapsuleCollider Shape for Entity: {}", entt::to_integral(Entity));
                return EBodyBuildStatus::Error;
            }
        }
        else if (const SCylinderColliderComponent* CyC = Registry.try_get<SCylinderColliderComponent>(Entity))
        {
            ColliderTranslationOffset           = CyC->TranslationOffset;
            ColliderRotationOffset              = CyC->RotationOffset;
            bColliderIsTrigger                  = CyC->bIsTrigger;
            ResolvedMaterial                    = CyC->PhysicsMaterial.Get();

            const float ScaleFactor = TransformComponent->MaxScale();
            Shape = Scene->GetOrCreateCylinderShape(
                CyC->Radius     * ScaleFactor,
                CyC->HalfHeight * ScaleFactor,
                CyC->CapRadius  * ScaleFactor);
            if (Shape == nullptr)
            {
                LOG_ERROR("Failed to create CylinderCollider Shape for Entity: {}", entt::to_integral(Entity));
                return EBodyBuildStatus::Error;
            }
        }
        else if (const SMeshColliderComponent* MC = Registry.try_get<SMeshColliderComponent>(Entity))
        {
            ColliderTranslationOffset = MC->TranslationOffset;
            ColliderRotationOffset    = MC->RotationOffset;
            bColliderIsTrigger        = MC->bIsTrigger;
            ResolvedMaterial          = MC->PhysicsMaterial.Get();

            CStaticMesh* Mesh = MC->Mesh.Get();
            if (Mesh == nullptr)
            {
                if (const SStaticMeshComponent* SMC = Registry.try_get<SStaticMeshComponent>(Entity))
                {
                    Mesh = SMC->StaticMesh.Get();
                }
            }

            if (Mesh == nullptr || Mesh->HasAnyFlag(OF_NeedsLoad) || Mesh->GetMeshResource().MeshletData.IsEmpty())
            {
                return EBodyBuildStatus::Defer;
            }

            Shape = Scene->GetOrCreateMeshShape(Mesh, TransformComponent->GetScale(), MC->bConvex);
            if (Shape == nullptr)
            {
                LOG_ERROR("Failed to create MeshCollider Shape for Entity: {}", entt::to_integral(Entity));
                return EBodyBuildStatus::Error;
            }

            bIsTriangleMesh = !MC->bConvex;
        }
        else if (const STerrainColliderComponent* TC = Registry.try_get<STerrainColliderComponent>(Entity))
        {
            bColliderIsTrigger = TC->bIsTrigger;
            ResolvedMaterial   = TC->PhysicsMaterial.Get();

            const STerrainComponent* Terrain = Registry.try_get<STerrainComponent>(Entity);
            if (Terrain == nullptr || Terrain->Heightmap.empty())
            {
                return EBodyBuildStatus::Defer;
            }

            Shape = BuildTerrainHeightFieldShape(*Terrain);
            if (Shape == nullptr)
            {
                LOG_ERROR("Failed to create TerrainCollider Shape for Entity: {}", entt::to_integral(Entity));
                return EBodyBuildStatus::Error;
            }

            // HeightFieldShape::MustBeStatic() -> true; the body must be Static.
            bIsTriangleMesh = true;
        }
        else
        {
            return EBodyBuildStatus::NoCollider;
        }

        JPH::ObjectLayer Layer      = JoltUtils::PackToObjectLayer(RigidBodyComponent->CollisionProfile);
        JPH::EMotionType MotionType = ToJoltMotionType(RigidBodyComponent->BodyType);

        // Jolt's triangle mesh shape only supports Static / Kinematic bodies.
        if (bIsTriangleMesh && MotionType == JPH::EMotionType::Dynamic)
        {
            LOG_WARN("MeshCollider on Entity {} is a non-convex triangle mesh; forcing motion type to Static.", entt::to_integral(Entity));
            MotionType = JPH::EMotionType::Static;
        }

        FQuat Rotation      = TransformComponent->GetRotation();
        FVector3 Position      = TransformComponent->GetLocation();

        // Skip the wrapper when there's no offset; unoffset bodies can share the bare cached shape.
        const bool bHasColliderOffset = Math::LengthSquared(ColliderTranslationOffset) > LE_SMALL_NUMBER
                                     || Math::LengthSquared(ColliderRotationOffset)    > LE_SMALL_NUMBER;
        if (bHasColliderOffset)
        {
            FQuat QuatRotation(ColliderRotationOffset);
            JPH::RotatedTranslatedShapeSettings RTS(JoltUtils::ToJPHVec3(ColliderTranslationOffset), JoltUtils::ToJPHQuat(QuatRotation), Shape);
            auto RTSResult = RTS.Create();
            if (RTSResult.HasError())
            {
                LOG_ERROR("Failed to create offset shape for Entity: {} - {}", entt::to_integral(Entity), RTSResult.GetError());
                return EBodyBuildStatus::Error;
            }

            Shape = RTSResult.Get();
        }

        if (Math::Dot(RigidBodyComponent->CenterOfMassOffset, RigidBodyComponent->CenterOfMassOffset) > 0.0f)
        {
            JPH::OffsetCenterOfMassShapeSettings COMS(JoltUtils::ToJPHVec3(RigidBodyComponent->CenterOfMassOffset), Shape);
            auto COMResult = COMS.Create();
            if (COMResult.HasError())
            {
                LOG_ERROR("Failed to apply CenterOfMassOffset for Entity: {} - {}", entt::to_integral(Entity), COMResult.GetError());
                return EBodyBuildStatus::Error;
            }
            Shape = COMResult.Get();
        }

        Out.Settings = JPH::BodyCreationSettings(
            Shape,
            JoltUtils::ToJPHRVec3(Position),
            JoltUtils::ToJPHQuat(Rotation),
            MotionType,
            Layer);

        Out.Settings.mNumPositionStepsOverride  = RigidBodyComponent->NumPositionStepsOverride;
        Out.Settings.mNumVelocityStepsOverride  = RigidBodyComponent->NumVelocityStepsOverride;
        // Either the rigid body's own bIsSensor or the collider's bIsTrigger turns this body
        // into a query-only sensor. The collider flag is the friendlier "trigger box" path.
        Out.Settings.mIsSensor                  = RigidBodyComponent->bIsSensor || bColliderIsTrigger;
        Out.Settings.mUseManifoldReduction      = RigidBodyComponent->bUseManifoldReduction;
        Out.Settings.mApplyGyroscopicForce      = RigidBodyComponent->bApplyGyroscopicForce;
        Out.Settings.mMotionQuality             = RigidBodyComponent->MotionQualityLevel == 0 ? JPH::EMotionQuality::Discrete : JPH::EMotionQuality::LinearCast;
        Out.Settings.mMaxLinearVelocity         = RigidBodyComponent->MaxLinearVelocity;
        Out.Settings.mMaxAngularVelocity        = RigidBodyComponent->MaxAngularVelocity;
        // PhysicsMaterial on the collider, when present, supersedes the rigid body's *Override fields.
        // The Override fields stay as the fallback for un-materialed bodies so existing scenes don't shift.
        if (ResolvedMaterial != nullptr)
        {
            Out.Settings.mFriction              = ResolvedMaterial->Friction;
            Out.Settings.mRestitution           = ResolvedMaterial->Restitution;
            Out.bHasMaterial                    = true;
            Out.MaterialFriction                = ResolvedMaterial->Friction;
            Out.MaterialRestitution             = ResolvedMaterial->Restitution;
            Out.MaterialFrictionCombine         = (uint8)ResolvedMaterial->FrictionCombine;
            Out.MaterialRestitutionCombine      = (uint8)ResolvedMaterial->RestitutionCombine;
        }
        else
        {
            Out.Settings.mRestitution           = RigidBodyComponent->RestitutionOverride;
            Out.Settings.mFriction              = RigidBodyComponent->FrictionOverride;
        }
        Out.Settings.mAngularDamping            = RigidBodyComponent->AngularDamping;
        Out.Settings.mLinearDamping             = RigidBodyComponent->LinearDamping;

        if (RigidBodyComponent->bOverrideMass && MotionType == JPH::EMotionType::Dynamic)
        {
            Out.Settings.mOverrideMassProperties        = JPH::EOverrideMassProperties::CalculateInertia;
            Out.Settings.mMassPropertiesOverride.mMass  = RigidBodyComponent->Mass;
        }

        Out.LastBodyPosition = Position;
        Out.LastBodyRotation = Rotation;

        return EBodyBuildStatus::Success;
    }

    void FJoltPhysicsScene::OnRigidBodyComponentConstructed(entt::registry& Registry, entt::entity Entity)
    {
        LUMINA_PROFILE_SCOPE();

        // Inside a game-thread batch (e.g. fracture): collect and create together in EndBodyBatch.
        if (bBatchingBodies && !Threading::IsPhysicsThread())
        {
            BatchedBodyCreations.push_back(Entity);
            return;
        }

        // Jolt forbids CreateBody/AddBody during a step; defer if on the physics thread.
        if (Threading::IsPhysicsThread())
        {
            FScopeLock Lock(PendingRigidBodyMutex);
            PendingRigidBodyCreations.push(Entity);
            return;
        }

        CreateRigidBodyImmediate(Registry, Entity);
    }

    void FJoltPhysicsScene::BeginBodyBatch()
    {
        // Game-thread only; fracture is synchronous so no nesting is expected.
        bBatchingBodies = true;
        BatchedBodyCreations.clear();
    }

    void FJoltPhysicsScene::EndBodyBatch()
    {
        bBatchingBodies = false;
        CreateRigidBodiesBatched(BatchedBodyCreations);
        BatchedBodyCreations.clear();
    }

    void FJoltPhysicsScene::CreateRigidBodyImmediate(entt::registry& Registry, entt::entity Entity)
    {
        LUMINA_PROFILE_SCOPE();

        FRigidBodyBuildResult BuildResult;
        const EBodyBuildStatus Status = TryBuildRigidBodyCreationSettings(this, Registry, Entity, BuildResult);

        switch (Status)
        {
        case EBodyBuildStatus::Defer:
        case EBodyBuildStatus::NoCollider:
        {
            FScopeLock Lock(PendingRigidBodyMutex);
            PendingRigidBodyCreations.push(Entity);
            return;
        }
        case EBodyBuildStatus::AlreadyExists:
        case EBodyBuildStatus::Error:
            return;
        case EBodyBuildStatus::Success:
            break;
        }

        JPH::BodyInterface& BodyInterface = JoltSystem->GetBodyInterface();
        JPH::Body* Body                   = BodyInterface.CreateBody(BuildResult.Settings);

        if (Body == nullptr)
        {
            LOG_ERROR("Failed to create body for Entity: {}", entt::to_integral(Entity));
            return;
        }

        Body->SetUserData(static_cast<uint64>(Entity));

        SRigidBodyComponent& RigidBodyComponent = Registry.get<SRigidBodyComponent>(Entity);
        RigidBodyComponent.BodyID               = Body->GetID().GetIndexAndSequenceNumber();
        RigidBodyComponent.LastBodyPosition     = BuildResult.LastBodyPosition;
        RigidBodyComponent.LastBodyRotation     = BuildResult.LastBodyRotation;

        StoreBodyMaterial(Body->GetID(), BuildResult);

        BodyInterface.AddBody(Body->GetID(), JPH::EActivation::Activate);
    }

    JPH::ShapeRefC FJoltPhysicsScene::GetOrCreateSphereShape(float Radius)
    {
        const FShapeKey Key{ 0, Radius, 0.0f, 0.0f };

        FScopeLock Lock(ShapeCacheMutex);
        auto It = ShapeCache.find(Key);
        if (It != ShapeCache.end())
        {
            return It->second;
        }

        JPH::SphereShapeSettings Settings(Radius);
        Settings.SetEmbedded();
        auto Result = Settings.Create();
        if (Result.HasError())
        {
            LOG_ERROR("Failed to create cached sphere shape (radius {}): {}", Radius, Result.GetError());
            return {};
        }

        JPH::ShapeRefC Shape = Result.Get();
        ShapeCache.emplace(Key, Shape);
        return Shape;
    }

    JPH::ShapeRefC FJoltPhysicsScene::GetOrCreateBoxShape(const FVector3& HalfExtent)
    {
        const FShapeKey Key{ 1, HalfExtent.x, HalfExtent.y, HalfExtent.z };

        FScopeLock Lock(ShapeCacheMutex);
        auto It = ShapeCache.find(Key);
        if (It != ShapeCache.end())
        {
            return It->second;
        }

        JPH::BoxShapeSettings Settings(JoltUtils::ToJPHVec3(HalfExtent));
        Settings.SetEmbedded();
        auto Result = Settings.Create();
        if (Result.HasError())
        {
            LOG_ERROR("Failed to create cached box shape ({}, {}, {}): {}", HalfExtent.x, HalfExtent.y, HalfExtent.z, Result.GetError());
            return {};
        }

        JPH::ShapeRefC Shape = Result.Get();
        ShapeCache.emplace(Key, Shape);
        return Shape;
    }

    JPH::ShapeRefC FJoltPhysicsScene::GetOrCreateCapsuleShape(float Radius, float HalfHeight)
    {
        // Kind = 2 (capsule). Z unused.
        const FShapeKey Key{ 2, Radius, HalfHeight, 0.0f };

        FScopeLock Lock(ShapeCacheMutex);
        auto It = ShapeCache.find(Key);
        if (It != ShapeCache.end())
        {
            return It->second;
        }

        JPH::CapsuleShapeSettings Settings(HalfHeight, Radius);
        Settings.SetEmbedded();
        auto Result = Settings.Create();
        if (Result.HasError())
        {
            LOG_ERROR("Failed to create cached capsule shape (r {}, hh {}): {}", Radius, HalfHeight, Result.GetError());
            return {};
        }

        JPH::ShapeRefC Shape = Result.Get();
        ShapeCache.emplace(Key, Shape);
        return Shape;
    }

    JPH::ShapeRefC FJoltPhysicsScene::GetOrCreateCylinderShape(float Radius, float HalfHeight, float CapRadius)
    {
        // Kind = 3 (cylinder). Z = cap (edge-rounding) radius so two cylinders with different
        // bevels don't share a cached shape.
        const FShapeKey Key{ 3, Radius, HalfHeight, CapRadius };

        FScopeLock Lock(ShapeCacheMutex);
        auto It = ShapeCache.find(Key);
        if (It != ShapeCache.end())
        {
            return It->second;
        }

        // Jolt requires CapRadius <= min(Radius, HalfHeight); clamp defensively so authoring an
        // oversized bevel never trips an internal assert.
        const float ClampedCap = Math::Clamp(CapRadius, 0.0f, Math::Min(Radius, HalfHeight));
        JPH::CylinderShapeSettings Settings(HalfHeight, Radius, ClampedCap);
        Settings.SetEmbedded();
        auto Result = Settings.Create();
        if (Result.HasError())
        {
            LOG_ERROR("Failed to create cached cylinder shape (r {}, hh {}, cap {}): {}", Radius, HalfHeight, ClampedCap, Result.GetError());
            return {};
        }

        JPH::ShapeRefC Shape = Result.Get();
        ShapeCache.emplace(Key, Shape);
        return Shape;
    }

    void FJoltPhysicsScene::StoreBodyMaterial(JPH::BodyID BodyID, const FRigidBodyBuildResult& Build)
    {
        const uint32 Index = BodyID.GetIndex();
        if (Index >= BodyMaterials.size())
        {
            return;
        }
        FBodyMaterialEntry& Entry  = BodyMaterials[Index];
        Entry.bHasMaterial         = Build.bHasMaterial;
        Entry.Friction             = Build.MaterialFriction;
        Entry.Restitution          = Build.MaterialRestitution;
        Entry.FrictionCombine      = Build.MaterialFrictionCombine;
        Entry.RestitutionCombine   = Build.MaterialRestitutionCombine;
    }

    void FJoltPhysicsScene::ClearBodyMaterial(JPH::BodyID BodyID)
    {
        const uint32 Index = BodyID.GetIndex();
        if (Index >= BodyMaterials.size())
        {
            return;
        }
        BodyMaterials[Index] = FBodyMaterialEntry{};
    }

    const FJoltPhysicsScene::FBodyMaterialEntry* FJoltPhysicsScene::GetBodyMaterial(JPH::BodyID BodyID) const
    {
        const uint32 Index = BodyID.GetIndex();
        if (Index >= BodyMaterials.size())
        {
            return nullptr;
        }
        return &BodyMaterials[Index];
    }

    JPH::ShapeRefC FJoltPhysicsScene::GetOrCreateMeshShape(const CMesh* Mesh, const FVector3& Scale, bool bConvex)
    {
        const FMeshShapeKey Key{ Mesh, Scale.x, Scale.y, Scale.z, (uint8)(bConvex ? 1 : 0) };

        {
            FScopeLock Lock(MeshShapeCacheMutex);
            auto It = MeshShapeCache.find(Key);
            if (It != MeshShapeCache.end())
            {
                return It->second;
            }
        }

        // Build outside the lock so parallel QuickHull on distinct meshes isn't serialized; the
        // try_emplace below only guards the rare same-key race.
        JPH::ShapeRefC Shape = BuildMeshColliderShape(Mesh, Scale, bConvex);
        if (Shape == nullptr)
        {
            return {};
        }

        FScopeLock Lock(MeshShapeCacheMutex);
        return MeshShapeCache.try_emplace(Key, Shape).first->second;
    }

    void FJoltPhysicsScene::BulkCreateRigidBodies(entt::registry& Registry)
    {
        LUMINA_PROFILE_SCOPE();

        TVector<entt::entity> Candidates;
        auto View = Registry.view<SRigidBodyComponent>();
        Candidates.reserve(View.size_hint());
        for (entt::entity Entity : View)
        {
            Candidates.push_back(Entity);
        }

        CreateRigidBodiesBatched(Candidates);
    }

    void FJoltPhysicsScene::CreateRigidBodiesBatched(const TVector<entt::entity>& Candidates)
    {
        LUMINA_PROFILE_SCOPE();

        const uint32 Count = (uint32)Candidates.size();
        if (Count == 0)
        {
            return;
        }

        entt::registry& Registry = World->GetEntityRegistry();

        TVector<FRigidBodyBuildResult> Results(Count);
        TVector<EBodyBuildStatus>      Statuses(Count, EBodyBuildStatus::Error);

        Task::ParallelFor(Count, [&](uint32 Index)
        {
            Statuses[Index] = TryBuildRigidBodyCreationSettings(this, Registry, Candidates[Index], Results[Index]);
        });

        JPH::BodyInterface& BodyInterface = JoltSystem->GetBodyInterface();
        TVector<JPH::BodyID> BodyIDsToAdd;
        BodyIDsToAdd.reserve(Count);

        for (uint32 i = 0; i < Count; ++i)
        {
            const entt::entity Entity = Candidates[i];
            switch (Statuses[i])
            {
            case EBodyBuildStatus::Defer:
            case EBodyBuildStatus::NoCollider:
            {
                FScopeLock Lock(PendingRigidBodyMutex);
                PendingRigidBodyCreations.push(Entity);
                continue;
            }
            case EBodyBuildStatus::AlreadyExists:
            case EBodyBuildStatus::Error:
                continue;
            case EBodyBuildStatus::Success:
                break;
            }

            JPH::Body* Body = BodyInterface.CreateBody(Results[i].Settings);
            if (Body == nullptr)
            {
                LOG_ERROR("Failed to create body for Entity: {}", entt::to_integral(Entity));
                continue;
            }

            Body->SetUserData(static_cast<uint64>(Entity));

            SRigidBodyComponent& RigidBodyComponent = Registry.get<SRigidBodyComponent>(Entity);
            RigidBodyComponent.BodyID               = Body->GetID().GetIndexAndSequenceNumber();
            RigidBodyComponent.LastBodyPosition     = Results[i].LastBodyPosition;
            RigidBodyComponent.LastBodyRotation     = Results[i].LastBodyRotation;

            StoreBodyMaterial(Body->GetID(), Results[i]);

            BodyIDsToAdd.push_back(Body->GetID());
        }

        if (!BodyIDsToAdd.empty())
        {
            JPH::BodyInterface::AddState AddState = BodyInterface.AddBodiesPrepare(BodyIDsToAdd.data(), (int)BodyIDsToAdd.size());
            BodyInterface.AddBodiesFinalize(BodyIDsToAdd.data(), (int)BodyIDsToAdd.size(), AddState, JPH::EActivation::Activate);
        }
    }

    void FJoltPhysicsScene::OnRigidBodyComponentDestroyed(entt::registry& Registry, entt::entity Entity)
    {
        SRigidBodyComponent& RigidBodyComponent = Registry.get<SRigidBodyComponent>(Entity);
        JPH::BodyInterface& BodyInterface = JoltSystem->GetBodyInterface();
        JPH::BodyID BodyID(RigidBodyComponent.BodyID);
        
        if (BodyID.IsInvalid())
        {
            return;
        }

        ClearBodyMaterial(BodyID);

        BodyInterface.RemoveBody(BodyID);
        BodyInterface.DestroyBody(BodyID);
    }

    void FJoltPhysicsScene::OnColliderComponentAdded(entt::registry& Registry, entt::entity Entity)
    {
    }

    void FJoltPhysicsScene::OnColliderComponentRemoved(entt::registry& Registry, entt::entity Entity)
    {
    }

    void FJoltPhysicsScene::OnImpulseEvent(const SImpulseEvent& Impulse)
    {
        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID BodyID = JPH::BodyID(Impulse.BodyID);
        JPH::RVec3 VecImpulse = JoltUtils::ToJPHRVec3(Impulse.Impulse);
        
        if (VecImpulse.IsNaN() || VecImpulse.IsNearZero())
        {
            return;
        }

        Interface.AddImpulse(BodyID, VecImpulse);
    }
    
    void FJoltPhysicsScene::OnForceEvent(const SForceEvent& Force)
    {
        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID BodyID = JPH::BodyID(Force.BodyID);
        
        Interface.AddForce(BodyID, JoltUtils::ToJPHRVec3(Force.Force));
    }
    
    void FJoltPhysicsScene::OnTorqueEvent(const STorqueEvent& Torque)
    {
        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID BodyID = JPH::BodyID(Torque.BodyID);
        
        Interface.AddTorque(BodyID, JoltUtils::ToJPHRVec3(Torque.Torque));
    }
    
    void FJoltPhysicsScene::OnAngularImpulseEvent(const SAngularImpulseEvent& AngularImpulse)
    {
        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID BodyID = JPH::BodyID(AngularImpulse.BodyID);
        
        Interface.AddAngularImpulse(BodyID, JoltUtils::ToJPHRVec3(AngularImpulse.AngularImpulse));
    }
    
    void FJoltPhysicsScene::OnSetVelocityEvent(const SSetVelocityEvent& Velocity)
    {
        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID BodyID = JPH::BodyID(Velocity.BodyID);
        
        Interface.SetLinearVelocity(BodyID, JoltUtils::ToJPHRVec3(Velocity.Velocity));
    }
    
    void FJoltPhysicsScene::OnSetAngularVelocityEvent(const SSetAngularVelocityEvent& AngularVelocity)
    {
        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID BodyID = JPH::BodyID(AngularVelocity.BodyID);
        
        Interface.SetAngularVelocity(BodyID, JoltUtils::ToJPHRVec3(AngularVelocity.AngularVelocity));
    }
    
    void FJoltPhysicsScene::OnAddImpulseAtPositionEvent(const SAddImpulseAtPositionEvent& Event)
    {
        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID BodyID = JPH::BodyID(Event.BodyID);
        
        Interface.AddImpulse(BodyID, JoltUtils::ToJPHRVec3(Event.Impulse), JoltUtils::ToJPHRVec3(Event.Position));
    }
    
    void FJoltPhysicsScene::OnAddForceAtPositionEvent(const SAddForceAtPositionEvent& Event)
    {
        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID BodyID = JPH::BodyID(Event.BodyID);
        
        Interface.AddForce(BodyID, JoltUtils::ToJPHRVec3(Event.Force), JoltUtils::ToJPHRVec3(Event.Position));
    }
    
    void FJoltPhysicsScene::OnSetGravityFactorEvent(const SSetGravityFactorEvent& Event)
    {
        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID BodyID = JPH::BodyID(Event.BodyID);
        
        Interface.SetGravityFactor(BodyID, Event.GravityFactor);
    }

    FVector3 FJoltPhysicsScene::GetVelocityAtPoint(uint32 BodyID, const FVector3& Point)
    {
        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID JPHBodyID = JPH::BodyID(BodyID);
        
        JPH::Vec3 PointVelocity = Interface.GetPointVelocity(JPHBodyID, JoltUtils::ToJPHRVec3(Point));
        
        return JoltUtils::FromJPHVec3(PointVelocity);
    }

    FVector3 FJoltPhysicsScene::GetLinearVelocity(uint32 BodyID)
    {
        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID JPHBodyID = JPH::BodyID(BodyID);
        
        return JoltUtils::FromJPHVec3(Interface.GetLinearVelocity(JPHBodyID));
    }

    FVector3 FJoltPhysicsScene::GetAngularVelocity(uint32 BodyID)
    {
        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID JPHBodyID = JPH::BodyID(BodyID);
        
        return JoltUtils::FromJPHVec3(Interface.GetAngularVelocity(JPHBodyID));
    }

    FVector3 FJoltPhysicsScene::GetCenterOfMass(uint32 BodyID)
    {
        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID JPHBodyID = JPH::BodyID(BodyID);

        return JoltUtils::FromJPHVec3(Interface.GetCenterOfMassPosition(JPHBodyID));
    }

    FVector3 FJoltPhysicsScene::GetBodyPosition(uint32 BodyID)
    {
        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        return JoltUtils::FromJPHVec3(Interface.GetPosition(JPH::BodyID(BodyID)));
    }

    FQuat FJoltPhysicsScene::GetBodyRotation(uint32 BodyID)
    {
        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        return JoltUtils::FromJPHQuat(Interface.GetRotation(JPH::BodyID(BodyID)));
    }

    uint32 FJoltPhysicsScene::GetBodyCount()
    {
        return (uint32)JoltSystem->GetNumBodies();
    }

    uint32 FJoltPhysicsScene::GetMaxBodyCount()
    {
        return (uint32)JoltSystem->GetMaxBodies();
    }
}
