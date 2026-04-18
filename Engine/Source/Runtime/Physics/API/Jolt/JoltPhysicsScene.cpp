#include "pch.h"
#include "JoltPhysicsScene.h"
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include "Physics/Ray/RayCast.h"
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#if JPH_DEBUG_RENDERER
#include <Jolt/Renderer/DebugRendererSimple.h>
#include "Core/Utils/Defer.h"
#endif
#include <glm/gtx/norm.hpp>

#include "JoltPhysics.h"
#include "JoltUtils.h"
#include "Core/Console/ConsoleVariable.h"
#include "Core/Profiler/Profile.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "Renderer/RendererUtils.h"
#include "World/World.h"
#include "World/Entity/Components/CharacterComponent.h"
#include "World/Entity/Components/CharacterControllerComponent.h"
#include "World/Entity/Components/DirtyComponent.h"
#include "World/Entity/Components/PhysicsComponent.h"
#include "World/Entity/Components/TransformComponent.h"
#include "world/entity/components/velocitycomponent.h"
#include "World/Entity/Events/ImpulseEvent.h"
#include "World/Subsystems/WorldSettings.h"

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
        
        FIgnoreFilter(TSpan<const int64> InIgnoreBodies)
            : IgnoreBodies(InIgnoreBodies)
        {}
        
        bool ShouldCollide(const JPH::BodyID& inBodyID) const override
        {
            const int64 Key = inBodyID.GetIndexAndSequenceNumber();
            for (std::size_t i = 0; i < IgnoreBodies.size(); i++)
            {
                if (IgnoreBodies[i] == Key)
                {
                    return false;
                }
            }
            return true;
        }
    
        TSpan<const int64> IgnoreBodies;
    };

    static FLayerInterfaceImpl                  GJoltLayerInterface;
    static FObjectLayerPairFilterImpl           GObjectVsObjectLayerFilter;
    static FObjectVsBroadPhaseLayerFilterImpl   GObjectVsBroadPhaseLayerFilter;


    void FJoltContactListener::OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings)
    {
        
    }

    void FJoltContactListener::OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings)
    {
        ContactListener::OnContactPersisted(inBody1, inBody2, inManifold, ioSettings);
    }

    void FJoltContactListener::OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair)
    {
        ContactListener::OnContactRemoved(inSubShapePair);
    }

    void FJoltContactListener::OverrideFrictionAndRestitution(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings)
    {
    }

    void FJoltContactListener::GetFrictionAndRestitution(const JPH::Body& inBody, const JPH::SubShapeID& inSubShapeID, float& outFriction, float& outRestitution) const
    {
        
    }
    
    FJoltPhysicsScene::FJoltPhysicsScene(CWorld* InWorld)
        : Allocator(300ull * 1024 * 1024)
        , World(InWorld)
    {
        JoltSystem = MakeUnique<JPH::PhysicsSystem>();
        
        JoltSystem->Init(65536, 0, 131072, 262144, GJoltLayerInterface, GObjectVsBroadPhaseLayerFilter, GObjectVsObjectLayerFilter);
        JoltSystem->SetGravity(JPH::Vec3Arg(0.0f, GEarthGravity * World->GetDefaultWorldSettings().GravityScale, 0.0f));

        JPH::PhysicsSettings JoltSettings;
        JoltSystem->SetPhysicsSettings(JoltSettings);
        
        entt::dispatcher& Dispatcher = World->GetEntityRegistry().ctx().get<entt::dispatcher&>();
        ContactListener = MakeUnique<FJoltContactListener>(Dispatcher, &JoltSystem->GetBodyLockInterfaceNoLock());
        JoltSystem->SetContactListener(ContactListener.get());
        
        FEntityRegistry& Registry = World->GetEntityRegistry();
        
        Registry.on_construct<SSphereColliderComponent>().connect<&entt::registry::emplace_or_replace<SRigidBodyComponent>>();
        Registry.on_construct<SBoxColliderComponent>().connect<&entt::registry::emplace_or_replace<SRigidBodyComponent>>();
    }

    FJoltPhysicsScene::~FJoltPhysicsScene()
    {
        FEntityRegistry& Registry = World->GetEntityRegistry();

        Registry.on_construct<SSphereColliderComponent>().disconnect<&entt::registry::emplace_or_replace<SRigidBodyComponent>>();
        Registry.on_construct<SBoxColliderComponent>().disconnect<&entt::registry::emplace_or_replace<SRigidBodyComponent>>();
    }

    void FJoltPhysicsScene::PreUpdate()
    {
        
    }

    void FJoltPhysicsScene::PostUpdate()
    {
    }

    void FJoltPhysicsScene::ApplyDirtyTransforms(float FixedDt)
    {
        LUMINA_PROFILE_SCOPE();

        entt::registry& Registry = World->GetEntityRegistry();

        const JPH::BodyLockInterfaceNoLock& LockInterface = JoltSystem->GetBodyLockInterfaceNoLock();
        JPH::BodyInterface& BodyInterface = JoltSystem->GetBodyInterface();

        // Jolt bodies are moved using the fixed step duration so kinematic /
        // velocity-matched motion lands exactly in one sub-step, regardless of
        // the current frame's wall-clock delta.
        auto BodySyncView = Registry.view<SRigidBodyComponent, STransformComponent, FNeedsPhysicsBodyUpdate>();
        BodySyncView.each([&](SRigidBodyComponent& BodyComponent, const STransformComponent& TransformComponent, const FNeedsPhysicsBodyUpdate& Update)
        {
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

        // Reset character interpolation after manual transform edits. The
        // character needs to snap to the new pose instead of sweeping through.
        auto CharacterResetView = Registry.view<SCharacterPhysicsComponent, FNeedsPhysicsBodyUpdate>();
        CharacterResetView.each([&](SCharacterPhysicsComponent& CharacterComponent, FNeedsPhysicsBodyUpdate&)
        {
            CharacterComponent.bInterpolationValid = false;
        });

        Registry.clear<FNeedsPhysicsBodyUpdate>();
    }

    void FJoltPhysicsScene::StorePreviousTransforms()
    {
        LUMINA_PROFILE_SCOPE();

        entt::registry& Registry = World->GetEntityRegistry();

        auto CharacterView = Registry.view<SCharacterPhysicsComponent>();
        CharacterView.each([&](SCharacterPhysicsComponent& CharacterComponent)
        {
            if (CharacterComponent.Character == nullptr)
            {
                return;
            }

            CharacterComponent.PreviousLocation = JoltUtils::FromJPHRVec3(CharacterComponent.Character->GetPosition());
            CharacterComponent.PreviousRotation = JoltUtils::FromJPHQuat(CharacterComponent.Character->GetRotation());
            CharacterComponent.bInterpolationValid = true;
        });
    }

    void FJoltPhysicsScene::Update(double DeltaTime)
    {
        LUMINA_PROFILE_SCOPE();

        constexpr double MaxDeltaTime = 0.25;   // clamp to avoid spiral of death on giant hitches
        constexpr int    MaxSubSteps  = 5;

        DeltaTime = eastl::min(DeltaTime, MaxDeltaTime);

        const float PhysicsRateHz = eastl::max(10.0f, World->GetDefaultWorldSettings().FixedPhysicsTimestep);
        const float FixedDt       = 1.0f / PhysicsRateHz;

        #if JPH_DEBUG_RENDERER
        if (FConsoleRegistry::Get().GetAs<bool>("Jolt.Debug.Draw"))
        {
            FJoltDebugRenderer* DebugRenderer = FJoltPhysicsContext::GetDebugRenderer();
            DebugRenderer->DrawBodies(JoltSystem.get(), World);
        }
        #endif

        // Resolve deferred rigid-body creations (colliders that arrived before
        // a shape was attached).
        while (!PendingRigidBodyCreations.empty())
        {
            entt::entity Entity = PendingRigidBodyCreations.front();
            PendingRigidBodyCreations.pop();

            if (World->GetEntityRegistry().valid(Entity))
            {
                OnRigidBodyComponentConstructed(World->GetEntityRegistry(), Entity);
            }
        }

        // Push any per-frame transform edits from game/script code into Jolt
        // bodies before stepping. This runs once per frame, kinematic bodies
        // are authored with FixedDt so their velocity lands in one sub-step.
        ApplyDirtyTransforms(FixedDt);

        // Latch controller input into movement components once per frame. Every
        // substep below reads the same latched state, so a frame with multiple
        // substeps doesn't starve itself of input after the first iteration.
        LatchCharacterInput();

        // Fixed-step loop. We snapshot the pre-step pose, update characters
        // and the rigid-body world with exactly one fixed step, then do it
        // again until we've drained the accumulator.
        Accumulator += DeltaTime;

        int NumSteps = 0;
        while (Accumulator >= FixedDt && NumSteps < MaxSubSteps)
        {
            LUMINA_PROFILE_SECTION("Physics Sub-Step");

            StorePreviousTransforms();
            UpdateCharacters(FixedDt);
            JoltSystem->Update(FixedDt, 1, &Allocator, FJoltPhysicsContext::GetThreadPool());

            Accumulator -= FixedDt;
            ++NumSteps;
        }

        // If we blew through the sub-step cap, drop the remaining time on
        // the floor, better to briefly slow simulation than to spiral.
        if (NumSteps >= MaxSubSteps && Accumulator >= FixedDt)
        {
            Accumulator = 0.0;
        }

        InterpolationAlpha = static_cast<float>(Accumulator / FixedDt);
        InterpolateVisualTransforms(InterpolationAlpha);

        #if JPH_DEBUG_RENDERER
        FJoltPhysicsContext::GetDebugRenderer()->NextFrame();
        #endif
    }
    
    void FJoltPhysicsScene::Simulate()
    {
        entt::registry& Registry = World->GetEntityRegistry();
        
        auto View = Registry.view<SRigidBodyComponent>();
        
        View.each([&] (entt::entity EntityID, SRigidBodyComponent&)
        {
            OnRigidBodyComponentConstructed(Registry, EntityID);
        });

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

    void FJoltPhysicsScene::InterpolateVisualTransforms(float Alpha)
    {
        LUMINA_PROFILE_SCOPE();

        const float KillHeight = World->GetDefaultWorldSettings().WorldKillHeight;
        const float ClampedAlpha = glm::clamp(Alpha, 0.0f, 1.0f);

        const JPH::BodyLockInterfaceNoLock& LockInterface = JoltSystem->GetBodyLockInterfaceNoLock();
        entt::registry& Registry = World->GetEntityRegistry();
        
        TVector<entt::entity> PendingDestroy;

        auto View = Registry.view<SRigidBodyComponent, STransformComponent>();
        View.each([&](entt::entity EntityID, SRigidBodyComponent& BodyComponent, STransformComponent& TransformComponent)
        {
            const JPH::Body* Body = LockInterface.TryGetBody(JPH::BodyID(BodyComponent.BodyID));
            if (Body == nullptr || Body->IsStatic())
            {
                return;
            }

            // Sleeping dynamic bodies still need to sit at their final resting
            // pose but don't need interpolation, their previous snapshot is
            // equal to their current pose anyway.
            const JPH::RVec3 CurrentPos = Body->GetPosition();

            if (CurrentPos.GetY() < KillHeight)
            {
                PendingDestroy.push_back(EntityID);
                return;
            }

            const JPH::Quat CurrentRot = Body->GetRotation();
            glm::vec3 Location = JoltUtils::FromJPHRVec3(CurrentPos);
            glm::quat Rotation = JoltUtils::FromJPHQuat(CurrentRot);

            TransformComponent.SetLocation(Location);
            TransformComponent.SetRotation(Rotation);
        });

        auto CharacterView = Registry.view<SCharacterPhysicsComponent, STransformComponent>();
        CharacterView.each([&](entt::entity Entity, SCharacterPhysicsComponent& CharacterComponent, STransformComponent& TransformComponent)
        {
            if (CharacterComponent.Character == nullptr)
            {
                return;
            }

            const JPH::Vec3 CurrentPos = CharacterComponent.Character->GetPosition();

            if (CurrentPos.GetY() < KillHeight)
            {
                PendingDestroy.push_back(Entity);
                return;
            }

            const JPH::Quat CurrentRot = CharacterComponent.Character->GetRotation();
            glm::vec3 Location = JoltUtils::FromJPHVec3(CurrentPos);
            glm::quat Rotation = JoltUtils::FromJPHQuat(CurrentRot);

            if (CharacterComponent.bInterpolationValid)
            {
                Location = glm::mix(CharacterComponent.PreviousLocation, Location, ClampedAlpha);
                Rotation = glm::normalize(glm::slerp(CharacterComponent.PreviousRotation, Rotation, ClampedAlpha));
            }

            TransformComponent.SetLocation(Location);
            TransformComponent.SetRotation(Rotation);

            // The game code just placed this body. We already applied the
            // transform above, so clear the marker so ApplyDirtyTransforms
            // doesn't stomp the physics body next frame.
            Registry.remove<FNeedsPhysicsBodyUpdate>(Entity);
        });

        for (entt::entity Entity : PendingDestroy)
        {
            if (Registry.valid(Entity))
            {
                Registry.destroy(Entity);
            }
        }
    }

    void FJoltPhysicsScene::LatchCharacterInput()
    {
        LUMINA_PROFILE_SCOPE();

        entt::registry& Registry = World->GetEntityRegistry();
        auto View = Registry.view<SCharacterControllerComponent, SCharacterMovementComponent>();

        View.each([&](SCharacterControllerComponent& Controller, SCharacterMovementComponent& Movement)
        {
            if (glm::length2(Controller.MoveInput) > LE_SMALL_NUMBER)
            {
                glm::vec3 Forward = RenderUtils::GetForwardVector(Controller.LookInput.x, 0.0f);
                glm::vec3 Right   = RenderUtils::GetRightVector(Controller.LookInput.x);
                glm::vec3 Up      = glm::cross(Right, Forward);

                glm::vec3 Direction = Right * Controller.MoveInput.x + Up * Controller.MoveInput.y + Forward * Controller.MoveInput.z;
                if (glm::length2(Direction) > LE_SMALL_NUMBER)
                {
                    Movement.PendingMoveDirection = glm::normalize(Direction);
                    Movement.bHasPendingMoveInput = true;
                }
                else
                {
                    Movement.PendingMoveDirection = glm::vec3(0.0f);
                    Movement.bHasPendingMoveInput = false;
                }
            }
            else
            {
                Movement.PendingMoveDirection = glm::vec3(0.0f);
                Movement.bHasPendingMoveInput = false;
            }

            Movement.PendingLookYaw = Controller.LookInput.x;
            Controller.MoveInput    = {};

            // Jump is a one-shot edge, latch it as pending so the first
            // substep that sees it consumes the impulse, then both the
            // controller and pending flag are cleared.
            if (Controller.bJumpPressed)
            {
                Movement.bPendingJump    = true;
                Controller.bJumpPressed  = false;
            }
        });
    }

    void FJoltPhysicsScene::UpdateCharacters(float FixedDt)
    {
        LUMINA_PROFILE_SCOPE();

        entt::registry& Registry = World->GetEntityRegistry();
        JPH::PhysicsSystem* PhysicsSystem = JoltSystem.get();

        auto View = Registry.view<SCharacterPhysicsComponent, SCharacterMovementComponent>();

        View.each([&](SCharacterPhysicsComponent& Physics, SCharacterMovementComponent& Movement)
        {
            LUMINA_PROFILE_SECTION("Character Sub-Step");

            JPH::CharacterVirtual* Character = Physics.Character;
            if (Character == nullptr)
            {
                return;
            }

            // Input was latched once per frame in LatchCharacterInput(). Every
            // substep sees the same intent, which is what we want for multi-
            // substep frames.
            const bool bHasMovementInput = Movement.bHasPendingMoveInput;
            const glm::vec3 DesiredDirection = Movement.PendingMoveDirection;

            const float    TargetSpeed    = bHasMovementInput ? Movement.MoveSpeed : 0.0f;
            const glm::vec3 TargetVelocity = DesiredDirection * TargetSpeed;

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

            // Rotation: keep orientation work in the fixed step so slerp is
            // frame-rate independent.
            glm::quat TargetRotation = JoltUtils::FromJPHQuat(Character->GetRotation());
            if (Movement.bUseControllerRotation)
            {
                TargetRotation = glm::quat(glm::vec3(0.0f, glm::radians(Movement.PendingLookYaw), 0.0f));
            }
            else if (Movement.bOrientRotationToMovement && bHasMovementInput)
            {
                float TargetYaw    = glm::atan(DesiredDirection.x, DesiredDirection.z);
                glm::quat Rotation = glm::quat(glm::vec3(0.0f, TargetYaw, 0.0f));
                TargetRotation     = glm::slerp(TargetRotation, Rotation, glm::clamp(Movement.RotationRate * FixedDt, 0.0f, 1.0f));
            }

            // Horizontal velocity integration with accel / friction / air drag.
            glm::vec3 HorizontalVelocity(Movement.Velocity.x, 0.0f, Movement.Velocity.z);
            const float CurrentSpeed = glm::length(HorizontalVelocity);

            if (bHasMovementInput)
            {
                const float Blend = glm::clamp(Movement.Acceleration * FixedDt, 0.0f, 1.0f);
                HorizontalVelocity = glm::mix(HorizontalVelocity, TargetVelocity, Blend);
            }
            else if (Movement.bGrounded)
            {
                const float DecelerationAmount = Movement.Deceleration * FixedDt;
                const float NewSpeed           = glm::max(0.0f, CurrentSpeed - DecelerationAmount);

                if (CurrentSpeed > 0.001f)
                {
                    HorizontalVelocity = glm::normalize(HorizontalVelocity) * NewSpeed;
                }
                else
                {
                    HorizontalVelocity = glm::vec3(0.0f);
                }

                const float Friction = glm::max(0.0f, 1.0f - Movement.GroundFriction * FixedDt);
                HorizontalVelocity *= Friction;
            }
            else
            {
                const float AirFriction = glm::max(0.0f, 1.0f - (Movement.GroundFriction * 0.1f) * FixedDt);
                HorizontalVelocity *= AirFriction;
            }

            Movement.Velocity.x = HorizontalVelocity.x;
            Movement.Velocity.z = HorizontalVelocity.z;

            if (Movement.bGrounded)
            {
                // Inherit moving-platform velocity so the character isn't
                // left behind when standing on a kinematic lift etc.
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

                if (Movement.JumpCount != Movement.MaxJumpCount)
                {
                    Movement.Velocity.y = Movement.JumpSpeed;
                    Movement.JumpCount++;
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
                Allocator);

            Movement.Velocity = JoltUtils::FromJPHVec3(Character->GetLinearVelocity());
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
        
        SRayResult Result
        {
            .BodyID     = Hit.mBodyID.GetIndexAndSequenceNumber(),
            .Entity     = static_cast<uint32>(Body->GetUserData()),
            .Start      = Settings.Start,
            .End        = Settings.End,
            .Location   = JoltUtils::FromJPHRVec3(Ray.GetPointOnRay(Hit.mFraction)),
            .Normal     = glm::normalize(JoltUtils::FromJPHVec3(SurfaceNormal)),
            .Fraction   = Hit.mFraction
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
                R.Normal   = glm::normalize(JoltUtils::FromJPHVec3(Hit.mPenetrationAxis.Normalized()));
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

    void FJoltPhysicsScene::OnCharacterComponentConstructed(entt::registry& Registry, entt::entity Entity)
    {
        LUMINA_PROFILE_SCOPE();

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
        
        JPH::Ref Character = Memory::New<JPH::CharacterVirtual>(Settings,
            JoltUtils::ToJPHRVec3(TransformComponent.GetLocation()),
            JoltUtils::ToJPHQuat(TransformComponent.GetRotation()),
            0,
            JoltSystem.get());
        
        JPH::BodyInterface& BodyInterface = JoltSystem->GetBodyInterface();
        BodyInterface.SetUserData(Character->GetInnerBodyID(), entt::to_integral(Entity));
        
        CharacterComponent.Character = Move(Character);
    }

    void FJoltPhysicsScene::OnCharacterComponentDestroyed(entt::registry& Registry, entt::entity Entity)
    {
        
    }

    void FJoltPhysicsScene::OnRigidBodyComponentUpdated(entt::registry& Registry, entt::entity Entity)
    {
        
    }

    void FJoltPhysicsScene::OnRigidBodyComponentConstructed(entt::registry& Registry, entt::entity Entity)
    {
        LUMINA_PROFILE_SCOPE();

        JPH::ShapeRefC Shape;
        glm::vec3 ColliderTranslationOffset(0.0f);
        glm::vec3 ColliderRotationOffset(0.0f);

        STransformComponent& TransformComponent = Registry.get<STransformComponent>(Entity);
        
        if (SBoxColliderComponent* BC = Registry.try_get<SBoxColliderComponent>(Entity))
        {
            ColliderTranslationOffset       = BC->TranslationOffset;
            ColliderRotationOffset          = BC->RotationOffset;
            
            JPH::BoxShapeSettings Settings(JoltUtils::ToJPHVec3(BC->HalfExtent * TransformComponent.GetScale()));
            Settings.SetEmbedded();
            auto Result = Settings.Create();
            if (Result.HasError())
            {
                return LOG_ERROR("Failed to create BoxCollider Shape for Entity: {} - {}", entt::to_integral(Entity), Result.GetError());
            }
            
            Shape = Result.Get();
        }
        else if (SSphereColliderComponent* SC = Registry.try_get<SSphereColliderComponent>(Entity))
        {
            ColliderTranslationOffset           = SC->TranslationOffset;

            JPH::SphereShapeSettings Settings(SC->Radius * TransformComponent.MaxScale());
            Settings.SetEmbedded();
            auto Result = Settings.Create();
            if (Result.HasError())
            {
                return LOG_ERROR("Failed to create SphereCollider Shape for Entity: {} - {}", entt::to_integral(Entity), Result.GetError());
            }
            
            Shape = Result.Get();      
        }
        else
        {
            PendingRigidBodyCreations.push(Entity);
            return;
        }

        SRigidBodyComponent& RigidBodyComponent = Registry.get<SRigidBodyComponent>(Entity);
        if (RigidBodyComponent.BodyID != JPH::BodyID::cInvalidBodyID)
        {
            return;
        }
        
        JPH::ObjectLayer Layer      = JoltUtils::PackToObjectLayer(RigidBodyComponent.CollisionProfile);
        JPH::EMotionType MotionType = ToJoltMotionType(RigidBodyComponent.BodyType);

        glm::quat Rotation      = TransformComponent.GetRotation();
        glm::vec3 Position      = TransformComponent.GetLocation();
        
        glm::quat QuatRotation(ColliderRotationOffset);
        JPH::RotatedTranslatedShapeSettings RTS(JoltUtils::ToJPHVec3(ColliderTranslationOffset), JoltUtils::ToJPHQuat(QuatRotation), Shape);
        auto RTSResult = RTS.Create();
        if (RTSResult.HasError())
        {
            LOG_ERROR("Failed to create offset shape for Entity: {} - {}", entt::to_integral(Entity), RTSResult.GetError());
            return;
        }
        
        Shape = RTSResult.Get();

        JPH::BodyCreationSettings Settings(
            Shape,
            JoltUtils::ToJPHRVec3(Position),
            JoltUtils::ToJPHQuat(Rotation),
            MotionType,
            Layer);

        Settings.mNumPositionStepsOverride  = RigidBodyComponent.NumPositionStepsOverride;
        Settings.mNumVelocityStepsOverride  = RigidBodyComponent.NumVelocityStepsOverride;
        Settings.mIsSensor                  = RigidBodyComponent.bIsSensor;
        Settings.mUseManifoldReduction      = RigidBodyComponent.bUseManifoldReduction;
        Settings.mApplyGyroscopicForce      = RigidBodyComponent.bApplyGyroscopicForce;
        Settings.mMotionQuality             = RigidBodyComponent.MotionQualityLevel == 0 ? JPH::EMotionQuality::Discrete : JPH::EMotionQuality::LinearCast;
        Settings.mMaxLinearVelocity         = RigidBodyComponent.MaxLinearVelocity;
        Settings.mMaxAngularVelocity        = RigidBodyComponent.MaxAngularVelocity;
        Settings.mRestitution               = RigidBodyComponent.RestitutionOverride;
        Settings.mFriction                  = RigidBodyComponent.FrictionOverride;
        Settings.mAngularDamping            = RigidBodyComponent.AngularDamping;
        Settings.mLinearDamping             = RigidBodyComponent.LinearDamping; 

        JPH::BodyInterface& BodyInterface   = JoltSystem->GetBodyInterface();
        JPH::Body* Body                     = BodyInterface.CreateBody(Settings);
        
        if (Body == nullptr)
        {
            LOG_ERROR("Failed to create body for Entity: {}", entt::to_integral(Entity));
            return;
        }
        
        Body->SetUserData(static_cast<uint64>(Entity));
        RigidBodyComponent.BodyID           = Body->GetID().GetIndexAndSequenceNumber();
        
        BodyInterface.AddBody(Body->GetID(), JPH::EActivation::Activate);
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
        LUMINA_PROFILE_SCOPE();

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
        LUMINA_PROFILE_SCOPE();

        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID BodyID = JPH::BodyID(Force.BodyID);
        
        Interface.AddForce(BodyID, JoltUtils::ToJPHRVec3(Force.Force));
    }
    
    void FJoltPhysicsScene::OnTorqueEvent(const STorqueEvent& Torque)
    {
        LUMINA_PROFILE_SCOPE();

        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID BodyID = JPH::BodyID(Torque.BodyID);
        
        Interface.AddTorque(BodyID, JoltUtils::ToJPHRVec3(Torque.Torque));
    }
    
    void FJoltPhysicsScene::OnAngularImpulseEvent(const SAngularImpulseEvent& AngularImpulse)
    {
        LUMINA_PROFILE_SCOPE();

        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID BodyID = JPH::BodyID(AngularImpulse.BodyID);
        
        Interface.AddAngularImpulse(BodyID, JoltUtils::ToJPHRVec3(AngularImpulse.AngularImpulse));
    }
    
    void FJoltPhysicsScene::OnSetVelocityEvent(const SSetVelocityEvent& Velocity)
    {
        LUMINA_PROFILE_SCOPE();

        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID BodyID = JPH::BodyID(Velocity.BodyID);
        
        Interface.SetLinearVelocity(BodyID, JoltUtils::ToJPHRVec3(Velocity.Velocity));
    }
    
    void FJoltPhysicsScene::OnSetAngularVelocityEvent(const SSetAngularVelocityEvent& AngularVelocity)
    {
        LUMINA_PROFILE_SCOPE();

        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID BodyID = JPH::BodyID(AngularVelocity.BodyID);
        
        Interface.SetAngularVelocity(BodyID, JoltUtils::ToJPHRVec3(AngularVelocity.AngularVelocity));
    }
    
    void FJoltPhysicsScene::OnAddImpulseAtPositionEvent(const SAddImpulseAtPositionEvent& Event)
    {
        LUMINA_PROFILE_SCOPE();

        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID BodyID = JPH::BodyID(Event.BodyID);
        
        Interface.AddImpulse(BodyID, JoltUtils::ToJPHRVec3(Event.Impulse), JoltUtils::ToJPHRVec3(Event.Position));
    }
    
    void FJoltPhysicsScene::OnAddForceAtPositionEvent(const SAddForceAtPositionEvent& Event)
    {
        LUMINA_PROFILE_SCOPE();

        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID BodyID = JPH::BodyID(Event.BodyID);
        
        Interface.AddForce(BodyID, JoltUtils::ToJPHRVec3(Event.Force), JoltUtils::ToJPHRVec3(Event.Position));
    }
    
    void FJoltPhysicsScene::OnSetGravityFactorEvent(const SSetGravityFactorEvent& Event)
    {
        LUMINA_PROFILE_SCOPE();

        JPH::BodyInterface& Interface = JoltSystem->GetBodyInterface();
        JPH::BodyID BodyID = JPH::BodyID(Event.BodyID);
        
        Interface.SetGravityFactor(BodyID, Event.GravityFactor);
    }
}
