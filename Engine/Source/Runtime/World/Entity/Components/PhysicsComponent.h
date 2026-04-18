#pragma once

#include "Core/Object/ObjectMacros.h"
#include "Physics/PhysicsTypes.h"
#include "PhysicsComponent.generated.h"

namespace Lumina
{
    REFLECT(Component)
    struct RUNTIME_API SRigidBodyComponent
    {
        GENERATED_BODY()

        PROPERTY(Script, ReadOnly, Category = "Physics")
        uint32 BodyID = 0xFFFFFFFF;
        
        PROPERTY(Script, Editable, Category = "Physics")
        float Mass = 1.0f;
        
        PROPERTY(Script, Editable, Category = "Physics")
        FCollisionProfile CollisionProfile;
        
        
        PROPERTY(Script, Editable, Category = "Physics")
        uint32 NumVelocityStepsOverride = 0;
        
        PROPERTY(Script, Editable, Category = "Physics")
        uint32 NumPositionStepsOverride = 0;
        
        PROPERTY(Script, Editable, ClampMin = 0.001f, Category = "Physics")
        float MaxLinearVelocity = 500.0f;
        
        PROPERTY(Script, Editable, ClampMin = 0.001f, Category = "Physics")
        float MaxAngularVelocity = 0.25f * LE_PI_F * 60.0f;
        
        PROPERTY(Script, Editable, ClampMin = 0.001f, ClampMax = 1.0f, Category = "Physics")
        float RestitutionOverride = 0.5f;
        
        PROPERTY(Script, Editable, ClampMin = 0.001f, ClampMax = 1.0f, Category = "Physics")
        float FrictionOverride = 0.3f;

        PROPERTY(Script, Editable, ClampMin = 0.001f, ClampMax = 1.0f, Category = "Physics")
        float LinearDamping = 0.0f;

        PROPERTY(Script, Editable, ClampMin = 0.001f, ClampMax = 1.0f, Category = "Physics")
        float AngularDamping = 0.05f;
        
        
        PROPERTY(Script, Editable, Category = "Physics")
        uint8 MotionQualityLevel = 0;
        
        PROPERTY(Script, Editable, Category = "Physics")
        EBodyType BodyType = EBodyType::Dynamic;
        
        PROPERTY(Script, Editable, Category = "Physics")
        bool bIsSensor = false;
        
        PROPERTY(Script, Editable, Category = "Physics")
        bool bUseManifoldReduction = false;
        
        PROPERTY(Script, Editable, Category = "Physics")
        bool bApplyGyroscopicForce = false;
        
        PROPERTY(Script, Editable, Category = "Physics")
        bool bAllowSleeping = true;
        
        PROPERTY(Script, Editable, Category = "Physics")
        bool bUseGravity = true;
    };

    REFLECT(Component)
    struct RUNTIME_API SBoxColliderComponent
    {
        GENERATED_BODY()

        PROPERTY(Editable)
        glm::vec3 HalfExtent = glm::vec3(0.5f);

        PROPERTY(Editable)
        glm::vec3 TranslationOffset;
        
        PROPERTY(Editable)
        glm::vec3 RotationOffset;
    };

    REFLECT(Component)
    struct RUNTIME_API SSphereColliderComponent
    {
        GENERATED_BODY()

        PROPERTY(Editable)
        float Radius = 0.5f;

        PROPERTY(Editable)
        glm::vec3 TranslationOffset;
    };
    
}