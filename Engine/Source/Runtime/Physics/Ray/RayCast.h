#pragma once

#include "Platform/GenericPlatform.h"
#include "Core/Object/ObjectMacros.h"
#include "RayCast.generated.h"

namespace Lumina
{
    REFLECT()
    struct SRayResult
    {
        GENERATED_BODY()

        /** Jolt body ID of the hit object. */
        PROPERTY(Script)
        int64 BodyID;

        /** ECS entity handle of the hit entity. */
        PROPERTY(Script)
        uint32 Entity = entt::null;

        /** World-space origin of the ray. */
        PROPERTY(Script)
        glm::vec3 Start;

        /** World-space end point of the ray. */
        PROPERTY(Script)
        glm::vec3 End;

        /** World-space position of the hit. */
        PROPERTY(Script)
        glm::vec3 Location;

        /** Surface normal at the hit point. */
        PROPERTY(Script)
        glm::vec3 Normal;

        /** Normalized distance along the ray where the hit occurred (0 = start, 1 = end). */
        PROPERTY(Script)
        float Fraction;
    };

    REFLECT()
    struct SRayCastSettings
    {
        GENERATED_BODY()

        /** World-space origin of the ray. */
        PROPERTY(Script)
        glm::vec3 Start = glm::vec3(0.0f);

        /** World-space end point of the ray. */
        PROPERTY(Script)
        glm::vec3 End = glm::vec3(0.0f);

        /** When true, the ray is drawn in the world for the debug duration. */
        PROPERTY(Script)
        bool bDrawDebug = false;

        /** How long (seconds) the debug line is visible (0 = one frame). */
        PROPERTY(Script)
        float DebugDuration = 0.0f;

        /** Debug line color when the ray hits something. */
        PROPERTY(Script)
        glm::vec3 DebugHitColor = glm::vec3(0.0, 1.0f, 0.0f);

        /** Debug line color when the ray misses. */
        PROPERTY(Script)
        glm::vec3 DebugMissColor = glm::vec3(1.0f, 0.0f, 0.0f);

        /** Bitmask of collision layers this ray tests against. */
        PROPERTY(Script)
        ECollisionProfiles LayerMask;

        /** List of body IDs to skip during the cast. */
        PROPERTY(Script)
        TVector<int64> IgnoreBodies;
    };

    REFLECT()
    struct SSphereCastSettings
    {
        GENERATED_BODY()

        /** World-space origin of the sphere sweep. */
        PROPERTY(Script)
        glm::vec3 Start = glm::vec3(0.0f);

        /** World-space end point of the sphere sweep. */
        PROPERTY(Script)
        glm::vec3 End = glm::vec3(0.0f);

        /** Radius of the sweeping sphere (meters). */
        PROPERTY(Script)
        float Radius = 0.0f;

        /** When true, the sweep is drawn in the world for the debug duration. */
        PROPERTY(Script)
        bool bDrawDebug = false;

        /** How long (seconds) the debug shape is visible (0 = one frame). */
        PROPERTY(Script)
        float DebugDuration = 0.0f;

        /** Debug line color when the sweep hits something. */
        PROPERTY(Script)
        glm::vec3 DebugHitColor = glm::vec3(0.0, 1.0f, 0.0f);

        /** Debug line color when the sweep misses. */
        PROPERTY(Script)
        glm::vec3 DebugMissColor = glm::vec3(1.0f, 0.0f, 0.0f);

        /** Bitmask of collision layers this sweep tests against. */
        PROPERTY(Script)
        ECollisionProfiles LayerMask;

        /** List of body IDs to skip during the cast. */
        PROPERTY(Script)
        TVector<int64> IgnoreBodies;
    };
}
