#pragma once
#include "EntitySystem.h"
#include "CameraSystem.generated.h"

namespace Lumina
{
    // Resolves the active view: bakes the camera matrix + blends post-process volumes into the
    // FResolvedSceneView singleton that CWorld::Extract forwards. Runs last (Low) so it sees every change.
    REFLECT(System)
    struct SCameraSystem
    {
        GENERATED_BODY()
        ENTITY_SYSTEM(RequiresUpdate(EUpdateStage::FrameEnd, EUpdatePriority::Low),
                      RequiresUpdate(EUpdateStage::Paused, EUpdatePriority::Low))

        static void Startup(const FSystemContext& Context) noexcept;
        static void Update(const FSystemContext& Context) noexcept;
        static void Teardown(const FSystemContext& Context) noexcept;
    };
}
