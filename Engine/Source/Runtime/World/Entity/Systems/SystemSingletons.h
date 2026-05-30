#pragma once
#include "Containers/Array.h"
#include "Renderer/ViewVolume.h"
#include "World/Entity/Components/PostProcessSettings.h"

namespace Lumina
{
    class CMaterialInterface;

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

    // Game-thread accumulator driving OnFixedUpdate at the physics fixed rate.
    // Owned by SScriptSystem; independent of the physics scene's own accumulator.
    struct FScriptFixedUpdateState
    {
        float Accumulator = 0.0f;
    };
}
