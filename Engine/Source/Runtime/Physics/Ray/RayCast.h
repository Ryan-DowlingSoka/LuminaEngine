#pragma once

#include "Platform/GenericPlatform.h"
#include "Core/Object/ObjectMacros.h"
#include "Physics/PhysicsTypes.h"
#include "RayCast.generated.h"

namespace Lumina
{
    REFLECT()
    struct SRayResult
    {
        GENERATED_BODY()

        PROPERTY(Script)
        int64 BodyID;

        PROPERTY(Script)
        uint32 Entity = entt::null;

        PROPERTY(Script)
        glm::vec3 Start;

        PROPERTY(Script)
        glm::vec3 End;

        PROPERTY(Script)
        glm::vec3 Location;

        PROPERTY(Script)
        glm::vec3 Normal;

        /** Normalized distance along ray (0 = start, 1 = end). */
        PROPERTY(Script)
        float Fraction;
        
        PROPERTY(Script)
        float Distance;
    };

    REFLECT()
    struct SRayCastSettings
    {
        GENERATED_BODY()

        PROPERTY(Script)
        glm::vec3 Start = glm::vec3(0.0f);

        PROPERTY(Script)
        glm::vec3 End = glm::vec3(0.0f);

        PROPERTY(Script)
        bool bDrawDebug = false;

        /** Seconds; 0 = one frame. */
        PROPERTY(Script)
        float DebugDuration = 0.0f;
        
        PROPERTY(Script)
        bool bIgnoreSelf = false;

        PROPERTY(Script)
        glm::vec3 DebugHitColor = glm::vec3(0.0, 1.0f, 0.0f);

        PROPERTY(Script)
        glm::vec3 DebugMissColor = glm::vec3(1.0f, 0.0f, 0.0f);

        PROPERTY(Script)
        ECollisionProfiles LayerMask;

        PROPERTY(Script)
        TVector<uint32> IgnoreBodies;
        
        FUNCTION(Script)
        void AddIgnoredBody(uint32 Body)
        {
            IgnoreBodies.push_back(Body);
        }
    };

    REFLECT()
    struct SSphereCastSettings
    {
        GENERATED_BODY()

        PROPERTY(Script)
        glm::vec3 Start = glm::vec3(0.0f);

        PROPERTY(Script)
        glm::vec3 End = glm::vec3(0.0f);

        /** Sphere radius (meters). */
        PROPERTY(Script)
        float Radius = 0.0f;

        PROPERTY(Script)
        bool bDrawDebug = false;

        /** Seconds; 0 = one frame. */
        PROPERTY(Script)
        float DebugDuration = 0.0f;

        PROPERTY(Script)
        glm::vec3 DebugHitColor = glm::vec3(0.0, 1.0f, 0.0f);

        PROPERTY(Script)
        glm::vec3 DebugMissColor = glm::vec3(1.0f, 0.0f, 0.0f);

        PROPERTY(Script)
        ECollisionProfiles LayerMask;

        PROPERTY(Script)
        TVector<uint32> IgnoreBodies;
        
        FUNCTION(Script)
        void AddIgnoredBody(uint32 Body)
        {
            IgnoreBodies.push_back(Body);
        }
    };
}
