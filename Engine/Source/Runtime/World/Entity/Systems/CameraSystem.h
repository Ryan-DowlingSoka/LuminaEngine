#pragma once
#include "EntitySystem.h"
#include "World/Entity/Components/CameraComponent.h"
#include "CameraSystem.generated.h"

namespace Lumina
{
    // Resolves the active view: bakes the camera matrix + blends post-process volumes into the
    // FResolvedSceneView singleton that CWorld::Extract forwards. Runs last (Low) so it sees every change.
    // Also owns active-camera selection + the cinematic blend, stored in the FCameraGlobalState singleton.
    REFLECT(System)
    struct SCameraSystem
    {
        GENERATED_BODY()
        ENTITY_SYSTEM(RequiresUpdate(EUpdateStage::FrameEnd, EUpdatePriority::Low),
                      RequiresUpdate(EUpdateStage::Paused, EUpdatePriority::Low))

        // Writes the resolved-view + camera-state singletons + the active camera component; reads transforms +
        // post-process volumes. Disjoint from animation/navmesh → overlaps them. Defined in the .cpp.
        static FSystemAccess Access;

        static void Startup(const FSystemContext& Context) noexcept;
        static void Update(const FSystemContext& Context) noexcept;
        static void Teardown(const FSystemContext& Context) noexcept;

        //~ Active-camera management on the FCameraGlobalState singleton.

        /** Switch the active camera. BlendTime > 0 eases from the current view; 0 snaps. */
        static void SetActiveCamera(entt::registry& Registry, entt::entity Entity, float BlendTime = 0.0f, ECameraBlendFunction Function = ECameraBlendFunction::EaseInOut);
        static entt::entity GetActiveCameraEntity(entt::registry& Registry);
        static SCameraComponent* GetActiveCamera(entt::registry& Registry);
    };
}
