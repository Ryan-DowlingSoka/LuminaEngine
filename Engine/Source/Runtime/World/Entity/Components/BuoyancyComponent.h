#pragma once

#include "Core/Math/Math.h"
#include "BuoyancyComponent.generated.h"

namespace Lumina
{
    // Makes a rigid body float on water. Requires an SRigidBodyComponent on the same entity and a water body
    // (SWaterComponent with bBuoyancy) the entity is over. The buoyancy system samples the water surface
    // (matching the rendered Gerstner waves) at four points around the body and applies lift + drag, so the
    // body bobs and self-rights. Needs gravity enabled on the rigid body.
    REFLECT(Component, Category = "Physics")
    struct RUNTIME_API SBuoyancyComponent
    {
        GENERATED_BODY()

        /** Buoyant strength as a multiple of gravity. >1 floats; 2 settles at ~50% submerged, 4 at ~25%. */
        PROPERTY(Editable, Category = "Buoyancy", ClampMin = 0.0f)
        float Buoyancy = 2.0f;

        /** Depth over which buoyancy ramps from none (at the surface) to full. Roughly the body's height. */
        PROPERTY(Editable, Category = "Buoyancy", ClampMin = 0.01f, Units = "m")
        float SubmergeDepth = 1.0f;

        /** Half-spacing of the four sample points (drives self-righting). 0 = a single center point, no righting. */
        PROPERTY(Editable, Category = "Buoyancy", ClampMin = 0.0f, Units = "m")
        float FloatRadius = 0.5f;

        /** Water drag while submerged. Damps bobbing and, with FloatRadius, rotation. */
        PROPERTY(Editable, Category = "Buoyancy", ClampMin = 0.0f)
        float LinearDrag = 1.0f;
    };
}
