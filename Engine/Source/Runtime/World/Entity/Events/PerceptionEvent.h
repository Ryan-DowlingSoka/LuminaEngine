#pragma once

#include "Core/Object/ObjectMacros.h"
#include "Core/Math/Math.h"
#include "PerceptionEvent.generated.h"

namespace Lumina
{
    // Payload for the OnTargetPerceived / OnTargetLost EntityScript callbacks. Self-oriented: Perceiver is
    // this entity, Target is the sensed/lost entity. NoCSharp: LuminaSharp hand-writes a blittable
    // `SPerceptionEvent` value struct mirroring this layout (see PerceptionEvent.cs), matching SCollisionEvent.
    REFLECT(Event, NoCSharp)
    struct SPerceptionEvent
    {
        GENERATED_BODY()

        /** This perceiving entity. */
        PROPERTY(Script)
        uint32 Perceiver = entt::null;

        /** The perceived (or lost) entity. */
        PROPERTY(Script)
        uint32 Target = entt::null;

        /** Last known / stimulus world location of the target. */
        PROPERTY(Script)
        FVector3 Location = FVector3(0.0f);

        /** The EAISenseChannel bit that triggered this event. */
        PROPERTY(Script)
        uint32 Sense = 0;

        /** Damage amount / noise loudness for hearing+damage; 0 for sight. */
        PROPERTY(Script)
        float Strength = 0.0f;
    };
}
