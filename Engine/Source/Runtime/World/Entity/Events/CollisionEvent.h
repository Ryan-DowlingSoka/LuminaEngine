#pragma once

#include "Core/Object/ObjectMacros.h"
#include "CollisionEvent.generated.h"

namespace Lumina
{
    // Payload handed to a script's OnContactBegin/End and OnOverlapBegin/End. Pushed
    // to Lua as a tagged userdata (one allocation, lazy field reads via the metatable)
    // instead of a hand-built table, so dispatching many contacts a frame stays cheap.
    // Fields are oriented per receiving side: Entity/Velocity are self, Other* is the
    // other body, and Normal points away from self toward the other.
    REFLECT(Event)
    struct SCollisionEvent
    {
        GENERATED_BODY()

        /** This script's entity. */
        PROPERTY(Script)
        uint32 Entity = entt::null;

        /** The other body's entity. */
        PROPERTY(Script)
        uint32 Other = entt::null;

        /** This body's Jolt body id. */
        PROPERTY(Script)
        uint32 BodyID = 0;

        /** The other body's Jolt body id. */
        PROPERTY(Script)
        uint32 OtherBodyID = 0;

        /** World-space contact point. */
        PROPERTY(Script)
        glm::vec3 Point = glm::vec3(0.0f);

        /** Contact normal pointing away from self toward the other body. */
        PROPERTY(Script)
        glm::vec3 Normal = glm::vec3(0.0f, 1.0f, 0.0f);

        /** This body's linear velocity at contact time (m/s). */
        PROPERTY(Script)
        glm::vec3 Velocity = glm::vec3(0.0f);

        /** The other body's linear velocity at contact time (m/s). */
        PROPERTY(Script)
        glm::vec3 OtherVelocity = glm::vec3(0.0f);

        /** Other - Self linear velocity (m/s). */
        PROPERTY(Script)
        glm::vec3 RelativeVelocity = glm::vec3(0.0f);

        /** |relative velocity along the normal| (m/s). */
        PROPERTY(Script)
        float ImpactSpeed = 0.0f;

        /** True if the OTHER side was a trigger/sensor. */
        PROPERTY(Script)
        bool bIsTrigger = false;
    };
}
