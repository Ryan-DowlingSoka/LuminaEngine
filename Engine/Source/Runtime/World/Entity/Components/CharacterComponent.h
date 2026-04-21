#pragma once

#include "Core/Object/ObjectMacros.h"
#include "Jolt/Physics/Character/CharacterVirtual.h"
#include "Physics/Physics.h"
#include "Physics/PhysicsTypes.h"
#include "CharacterComponent.generated.h"


namespace Lumina
{
    REFLECT(Component)
    struct RUNTIME_API SCharacterPhysicsComponent
    {
        GENERATED_BODY()

        JPH::Ref<JPH::CharacterVirtual> Character;
        
        // Snapshots for interpolation.
        glm::vec3 LastBodyPosition;
        glm::quat LastBodyRotation;

        /** Layer and mask controlling which bodies this character collides with. */
        PROPERTY(Script, Editable, Category = "Physics")
        FCollisionProfile CollisionProfile;

        /** Half-height of the character capsule in meters. */
        PROPERTY(Script, Editable, Category = "Collision")
        float HalfHeight = 1.8f;

        /** Radius of the character capsule in meters. */
        PROPERTY(Script, Editable, Category = "Collision")
        float Radius = 1.0f;

        /** Mass of the character body in kg, affects how much it is pushed by dynamic bodies. */
        PROPERTY(Script, Editable, Category = "Physics")
        float Mass = 70.0f;

        /** Small gap between the character shape and other surfaces to prevent tunneling. */
        PROPERTY(Script, Editable, Category = "Physics")
        float Padding = 0.02f;

        /** Cosine of the maximum angle between hit normals to be merged for reduction. */
        PROPERTY(Script, Editable, Category = "Physics")
        float HitReductionCosMaxAngle = 0.999f;

        /** Fraction of penetration resolved per step, higher values snap out of geometry faster. */
        PROPERTY(Script, Editable, Category = "Physics")
        float PenetrationRecoverySpeed = 1.0f;

        /** Distance ahead of the character to search for contacts and prevent clipping. */
        PROPERTY(Script, Editable, Category = "Physics")
        float PredictiveContactDistance = 0.1f;

        /** Maximum push force the character can exert against dynamic bodies. */
        PROPERTY(Script, Editable, Category = "Physics")
        float MaxStrength = 100.0f;

        /** Steepest surface angle (degrees) the character can walk up without sliding. */
        PROPERTY(Script, Editable, Category = "Physics")
        float MaxSlopeAngle = 45.0f;

        /** Maximum step height the character can automatically climb (meters). */
        PROPERTY(Script, Editable, Category = "Physics")
        float StepHeight = 0.4f;

        /** Maximum collision resolution iterations per step. Higher = more accurate but slower. */
        PROPERTY(Script, Editable, Category = "Physics")
        uint32 MaxCollisionIterations = 5;

        /** Maximum constraint solver iterations per step. */
        PROPERTY(Script, Editable, Category = "Physics")
        uint32 MaxConstraintIterations = 15;

        /** Minimum time remaining in a step before the character stops moving. */
        PROPERTY(Script, Editable, Category = "Physics")
        float MinTimeRemaining = 1.0e-4f;

        /** Tolerance for merging overlapping contact points (meters). */
        PROPERTY(Script, Editable, Category = "Physics")
        float CollisionTolerance = 1.0e-3f;

        /** Maximum number of contact hits processed per step before culling. */
        PROPERTY(Script, Editable, Category = "Physics")
        uint32 MaxNumHits = 256;
        
        
        
        FUNCTION(Script)
        uint32 GetBodyID() const
        {
            if (Character == nullptr)
            {
                return 0xFFFFFFFF;
            }
            
            return Character->GetInnerBodyID().GetIndexAndSequenceNumber();
        }
        
    };

    REFLECT(Component)
    struct RUNTIME_API SCharacterMovementComponent
    {
        GENERATED_BODY()
    
        /** Target horizontal movement speed (m/s). */
        PROPERTY(Script, Editable, ClampMin = 0.0f, Category = "Movement")
        float MoveSpeed = 5.0f;

        /** Rate at which the character accelerates to MoveSpeed (m/s²). */
        PROPERTY(Script, Editable, ClampMin = 0.0f, Category = "Movement")
        float Acceleration = 10.0f;

        /** Rate at which the character decelerates when no input is applied (m/s²). */
        PROPERTY(Script, Editable, ClampMin = 0.0f, Category = "Movement")
        float Deceleration = 8.0f;

        /** Fraction of ground acceleration available while airborne (0 = no air steering). */
        PROPERTY(Script, Editable, ClampMin = 0.0f, Category = "Movement")
        float AirControl = 0.3f;

        /** Friction coefficient applied against horizontal velocity while grounded. */
        PROPERTY(Script, Editable, ClampMin = 0.0f, Category = "Movement")
        float GroundFriction = 8.0f;

        /** Vertical impulse speed applied when the character jumps (m/s). */
        PROPERTY(Script, Editable, ClampMin = 0.0f, Category = "Movement")
        float JumpSpeed = 8.0f;

        /** Degrees per second the character rotates to face its movement direction. */
        PROPERTY(Script, Editable, ClampMin = 0.0f, ClampMax = 1000.0f, Category = "Movement")
        float RotationRate = 10.0f;

        /** Total number of jumps allowed before landing (1 = single jump, 2 = double jump, etc.). */
        PROPERTY(Script, Editable, ClampMin = 0, Category = "Movement")
        int MaxJumpCount = 1;

        /** Downward gravity acceleration (m/s²). */
        PROPERTY(Script, Editable, Category = "Gravity")
        float Gravity = Physics::GEarthGravity;

        /** Current velocity of the character in world space. */
        PROPERTY(Script, Visible, Category = "Movement")
        glm::vec3 Velocity;

        /** When true, the character's yaw matches the controller's look direction. */
        PROPERTY(Script, Editable, Category = "Rotation")
        bool bUseControllerRotation = false;

        /** When true, the character rotates to face its movement direction each frame. */
        PROPERTY(Script, Editable, Category = "Rotation")
        bool bOrientRotationToMovement = false;

        /** True when the character is standing on a surface. */
        PROPERTY(Script, ReadOnly, Category = "Movement")
        bool bGrounded = false;

        /** Number of jumps performed since last landing. */
        PROPERTY(Script, ReadOnly, Category = "Movement")
        int JumpCount = 0;

        // Internal staging: input is latched from the controller once per
        // frame in PrePhysics, then consumed at fixed-step rate in physics.
        glm::vec3 PendingMoveDirection = glm::vec3(0.0f);
        float     PendingLookYaw       = 0.0f;
        bool      bHasPendingMoveInput = false;
        bool      bPendingJump         = false;
    };

    
}
