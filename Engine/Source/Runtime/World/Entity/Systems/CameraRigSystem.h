#pragma once
#include "EntitySystem.h"
#include "CameraRigSystem.generated.h"

namespace Lumina
{
    // Positions camera entities from their rig components each frame: follow
    // (SCameraFollowComponent) and the third-person boom (SSpringArmComponent).
    // Runs at FrameEnd just before SCameraSystem (Medium sorts ahead of Low) so it
    // sees fully settled gameplay/physics transforms and the resolved view picks
    // up the camera's new pose the same frame. Game/simulation only -- the editor
    // viewport drives its own camera, so this does not run at Paused.
    //
    // When an entity has both rig components the spring arm wins (processed last).
    REFLECT(System)
    struct SCameraRigSystem
    {
        GENERATED_BODY()
        ENTITY_SYSTEM(RequiresUpdate(EUpdateStage::FrameEnd, EUpdatePriority::Medium))

        static void Update(const FSystemContext& Context) noexcept;
    };
}
