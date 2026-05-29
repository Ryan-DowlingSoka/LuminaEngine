#pragma once

#include "Core/Object/Object.h"
#include "Core/Object/ObjectMacros.h"
#include "PhysicsMaterial.generated.h"

namespace Lumina
{
    /**
     * How two PhysicsMaterial values are combined at a contact. The pair's effective mode is the
     * max of the two bodies' modes (Max > Min > Multiply > Average), matching the convention used
     * by most engines so a "sticky" material always wins.
     */
    REFLECT()
    enum class RUNTIME_API EPhysicsMaterialCombineMode : uint8
    {
        Average,
        Min,
        Multiply,
        Max,
    };

    /**
     * Designer-authored physical surface properties (friction, restitution, density) plus the
     * combine modes used when two surfaces touch. Assigned per collider component; the rigid
     * body's *Override fields remain the fallback when no material is set.
     *
     * The combine modes drive the contact callback in FJoltContactListener::OverrideFrictionAndRestitution:
     * the pair's effective mode is the max of the two bodies' modes, then friction and restitution
     * are combined under that rule. Without a material the body keeps Jolt's default combining.
     */
    REFLECT()
    class RUNTIME_API CPhysicsMaterial : public CObject
    {
        GENERATED_BODY()

    public:

        bool IsAsset() const override { return true; }

        /** Surface friction coefficient. 0 = ice, 1 = rubber on rubber. */
        PROPERTY(Editable, ClampMin = 0.0f, ClampMax = 4.0f, Category = "Physics Material")
        float Friction = 0.4f;

        /** Bounciness: 0 = inelastic, 1 = perfectly elastic. */
        PROPERTY(Editable, ClampMin = 0.0f, ClampMax = 1.0f, Category = "Physics Material")
        float Restitution = 0.3f;

        /** Volumetric density in kg/m^3. Used when the rigid body computes mass from shape volume. */
        PROPERTY(Editable, ClampMin = 0.001f, Category = "Physics Material")
        float Density = 1000.0f;

        /** Rule for combining this surface's friction with another at contact. */
        PROPERTY(Editable, Category = "Physics Material|Combine")
        EPhysicsMaterialCombineMode FrictionCombine = EPhysicsMaterialCombineMode::Average;

        /** Rule for combining this surface's restitution with another at contact. */
        PROPERTY(Editable, Category = "Physics Material|Combine")
        EPhysicsMaterialCombineMode RestitutionCombine = EPhysicsMaterialCombineMode::Max;
    };
}
