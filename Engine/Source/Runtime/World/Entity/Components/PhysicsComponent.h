#pragma once

#include "Core/Object/ObjectMacros.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Physics/PhysicsTypes.h"
#include "PhysicsComponent.generated.h"

namespace Lumina
{
    class CStaticMesh;

    REFLECT(Component, Category = "Physics")
    struct RUNTIME_API alignas(Threading::GCacheLineSize) SRigidBodyComponent
    {
        GENERATED_BODY()
        
        // Snapshots for interpolation.
        glm::vec3 LastBodyPosition;
        glm::quat LastBodyRotation;
        
        /** Internal Jolt body ID, read-only, assigned by the physics system. */
        PROPERTY(Script, ReadOnly, Category = "Physics")
        uint32 BodyID = 0xFFFFFFFF;

        /** Mass of the rigid body in kg. */
        PROPERTY(Script, Editable, Category = "Physics")
        float Mass = 1.0f;

        /** When true, Mass overrides the value Jolt would compute from shape density. */
        PROPERTY(Script, Editable, Category = "Physics")
        bool bOverrideMass = false;

        /** Local-space offset applied to the body's center of mass. Lower the Y for car-like weight bias. */
        PROPERTY(Script, Editable, Category = "Physics")
        glm::vec3 CenterOfMassOffset = glm::vec3(0.0f);

        /** Layer and mask controlling which bodies this one collides with. */
        PROPERTY(Script, Editable, Category = "Physics")
        FCollisionProfile CollisionProfile;

        /** Per-body override for global velocity solver iterations (0 = use world setting). */
        PROPERTY(Script, Editable, Category = "Physics")
        uint32 NumVelocityStepsOverride = 0;

        /** Per-body override for global position solver iterations (0 = use world setting). */
        PROPERTY(Script, Editable, Category = "Physics")
        uint32 NumPositionStepsOverride = 0;

        /** Maximum linear speed (m/s) this body can reach. */
        PROPERTY(Script, Editable, ClampMin = 0.001f, Category = "Physics")
        float MaxLinearVelocity = 500.0f;

        /** Maximum angular speed (rad/s) this body can reach. */
        PROPERTY(Script, Editable, ClampMin = 0.001f, Category = "Physics")
        float MaxAngularVelocity = 0.25f * LE_PI_F * 60.0f;

        /** Bounciness override for this body (0 = no bounce, 1 = perfectly elastic). */
        PROPERTY(Script, Editable, ClampMin = 0.001f, ClampMax = 1.0f, Category = "Physics")
        float RestitutionOverride = 0.5f;

        /** Surface friction coefficient override for this body. */
        PROPERTY(Script, Editable, ClampMin = 0.001f, ClampMax = 1.0f, Category = "Physics")
        float FrictionOverride = 0.3f;

        /** Damping applied to linear velocity each step. */
        PROPERTY(Script, Editable, ClampMin = 0.001f, ClampMax = 1.0f, Category = "Physics")
        float LinearDamping = 0.0f;

        /** Damping applied to angular velocity each step. */
        PROPERTY(Script, Editable, ClampMin = 0.001f, ClampMax = 1.0f, Category = "Physics")
        float AngularDamping = 0.05f;

        /** Motion quality level: 0 = Discrete, 1 = LinearCast. Higher is more expensive but prevents tunneling. */
        PROPERTY(Script, Editable, Category = "Physics")
        uint8 MotionQualityLevel = 0;

        /** Whether the body is Static, Kinematic, or Dynamic. */
        PROPERTY(Script, Editable, Category = "Physics")
        EBodyType BodyType = EBodyType::Dynamic;

        /** When true, the body detects overlaps but does not produce contact responses. */
        PROPERTY(Script, Editable, Category = "Physics")
        bool bIsSensor = false;

        /** Merge similar contact manifolds to reduce solver work for this body. */
        PROPERTY(Script, Editable, Category = "Physics")
        bool bUseManifoldReduction = false;

        /** Apply gyroscopic torque to spinning bodies for more realistic angular motion. */
        PROPERTY(Script, Editable, Category = "Physics")
        bool bApplyGyroscopicForce = false;

        /** Allow this body to enter sleep state when it comes to rest. */
        PROPERTY(Script, Editable, Category = "Physics")
        bool bAllowSleeping = true;

        /** Apply global gravity to this body. Disable for projectiles or floating objects. */
        PROPERTY(Script, Editable, Category = "Physics")
        bool bUseGravity = true;
        
    };

    REFLECT(Component, Category = "Physics")
    struct RUNTIME_API SBoxColliderComponent
    {
        GENERATED_BODY()

        /** Half-size of the box along each axis in local space (meters). */
        PROPERTY(Editable)
        glm::vec3 HalfExtent = glm::vec3(0.5f);

        /** Local-space offset applied to the collider position relative to the entity. */
        PROPERTY(Editable)
        glm::vec3 TranslationOffset;

        /** Local-space euler rotation offset applied to the collider. */
        PROPERTY(Editable)
        glm::vec3 RotationOffset;

        /** When true, the body produces overlap events but no contact response (trigger volume). */
        PROPERTY(Editable)
        bool bIsTrigger = false;

        /** When true, this collider contributes its shape to NavMesh bakes. */
        PROPERTY(Editable, Category = "Navigation")
        bool bAffectsNavigation = true;
    };

    REFLECT(Component, Category = "Physics")
    struct RUNTIME_API SSphereColliderComponent
    {
        GENERATED_BODY()

        /** Radius of the sphere collider in meters. */
        PROPERTY(Editable)
        float Radius = 0.5f;

        /** Local-space offset applied to the collider position relative to the entity. */
        PROPERTY(Editable)
        glm::vec3 TranslationOffset;

        /** When true, the body produces overlap events but no contact response (trigger volume). */
        PROPERTY(Editable)
        bool bIsTrigger = false;

        /** When true, this collider contributes its shape to NavMesh bakes. */
        PROPERTY(Editable, Category = "Navigation")
        bool bAffectsNavigation = true;
    };

    /** Static collider built from an STerrainComponent's heightmap (Jolt HeightFieldShape). Requires both components on the same entity. */
    REFLECT(Component, Category = "Physics")
    struct RUNTIME_API STerrainColliderComponent
    {
        GENERATED_BODY()

        /** When true, the body produces overlap events but no contact response (trigger volume). */
        PROPERTY(Editable)
        bool bIsTrigger = false;

        /** When true, this collider contributes its shape to NavMesh bakes. */
        PROPERTY(Editable, Category = "Navigation")
        bool bAffectsNavigation = true;
    };

    REFLECT(Component, Category = "Physics")
    struct RUNTIME_API SMeshColliderComponent
    {
        GENERATED_BODY()

        /** Mesh asset used as collision geometry. If null, the system falls back to the entity's StaticMeshComponent. */
        PROPERTY(Editable)
        TObjectPtr<CStaticMesh> Mesh;

        /** Build a convex hull from the mesh (allows dynamic bodies). When false, builds a concave triangle mesh (static / kinematic only). */
        PROPERTY(Editable)
        bool bConvex = false;

        /** Local-space offset applied to the collider position relative to the entity. */
        PROPERTY(Editable)
        glm::vec3 TranslationOffset;

        /** Local-space euler rotation offset applied to the collider. */
        PROPERTY(Editable)
        glm::vec3 RotationOffset;

        /** When true, the body produces overlap events but no contact response (trigger volume). */
        PROPERTY(Editable)
        bool bIsTrigger = false;

        /** When true, this collider contributes its shape to NavMesh bakes. */
        PROPERTY(Editable, Category = "Navigation")
        bool bAffectsNavigation = true;
    };

}