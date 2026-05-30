#pragma once
#include "EntitySystem.h"
#include "CameraRigSystem.generated.h"

namespace Lumina
{
    // Positions cameras from rig components (follow + spring-arm boom) at FrameEnd before SCameraSystem, on
    // settled transforms. Game/simulation only. With both rig components the spring arm wins (processed last).
    REFLECT(System)
    struct SCameraRigSystem
    {
        GENERATED_BODY()
        ENTITY_SYSTEM(RequiresUpdate(EUpdateStage::FrameEnd, EUpdatePriority::Medium))

        static void Update(const FSystemContext& Context) noexcept;
    };
}
