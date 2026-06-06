#pragma once

#include "EntitySystem.h"
#include "Core/Object/ObjectMacros.h"
#include "BuoyancySystem.generated.h"

namespace Lumina
{
    // Floats rigid bodies with an SBuoyancyComponent on any water body whose SWaterComponent has bBuoyancy.
    // Samples the water surface (matching the rendered Gerstner waves) at four points around each body and
    // applies lift + drag. PrePhysics so the forces land the same physics step.
    REFLECT(System)
    struct RUNTIME_API SBuoyancySystem
    {
        GENERATED_BODY()
        ENTITY_SYSTEM(RequiresUpdate(EUpdateStage::PrePhysics))

        static void Update(const FSystemContext& Context) noexcept;
    };
}
