#pragma once
#include <entt/entt.hpp>
#include "Containers/Array.h"
#include "Renderer/ViewVolume.h"
#include "World/Entity/Components/PostProcessSettings.h"

namespace Lumina
{
    class CMaterialInterface;
    enum class ECameraBlendFunction : uint8;

    // Registry-context singletons holding per-world state systems produce/consume. Plain (non-reflected),
    // transient, never serialized. Emplaced in CWorld::InitializeWorld, read in Extract / the systems.

    // Resolved active view. SCameraSystem writes it at end of update (FrameEnd, or Paused in editor);
    // CWorld::Extract reads + forwards it, keeping camera/post-process resolution in a system.
    struct FResolvedSceneView
    {
        FViewVolume                     ViewVolume;
        SPostProcessSettings            PostProcess;
        TVector<CMaterialInterface*>    PostProcessMaterials;
        bool                            bHasView = false;
        bool                            bHasPostProcess = false;
    };

    // Active-camera selection + cinematic blend, owned and ticked entirely by SCameraSystem. A non-zero
    // blend snapshots the displayed view and eases to the new camera over Duration; the resolved view is
    // stored back here each frame as the source for the next blend.
    struct FCameraGlobalState
    {
        struct FBlendState
        {
            bool                    bActive = false;
            float                   Elapsed = 0.0f;
            float                   Duration = 0.0f;
            ECameraBlendFunction    Function = (ECameraBlendFunction)3; // EaseInOut

            // Snapshot of the displayed view when the blend began.
            FVector3                FromPosition = FVector3(0.0f);
            FQuat                   FromRotation = FQuat(1.0f, 0.0f, 0.0f, 0.0f);
            float                   FromFOV = 90.0f;
            SPostProcessSettings    FromPostProcess;
        };

        entt::entity            ActiveCameraEntity = entt::null;
        FBlendState             Blend;

        // Last fully resolved (post-blend) view; the source for the next blend's snapshot.
        FVector3                LastViewPosition = FVector3(0.0f);
        FQuat                   LastViewRotation = FQuat(1.0f, 0.0f, 0.0f, 0.0f);
        float                   LastViewFOV = 90.0f;
        SPostProcessSettings    LastPostProcess;
        bool                    bHasResolvedView = false;
    };

    // Game-thread accumulator driving OnFixedUpdate at the physics fixed rate.
    // Owned by SScriptSystem; independent of the physics scene's own accumulator.
    struct FScriptFixedUpdateState
    {
        float Accumulator = 0.0f;
    };
}
