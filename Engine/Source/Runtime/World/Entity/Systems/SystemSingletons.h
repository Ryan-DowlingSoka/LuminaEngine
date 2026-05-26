#pragma once
#include "Containers/Array.h"
#include "Renderer/ViewVolume.h"
#include "World/Entity/Components/PostProcessSettings.h"

namespace Lumina
{
    class CMaterialInterface;

    // Registry-context singletons that hold per-world state systems produce or
    // consume. Plain (non-reflected) like the FUpdateStage_* tags: transient
    // runtime state, never serialized or shown in the editor. Emplaced once in
    // CWorld::InitializeWorld and read back in CWorld::Extract / the systems.

    // Resolved active view for the frame. SCameraSystem writes it at the end of
    // the update (FrameEnd in game/simulation, Paused in editor); CWorld::Extract
    // reads it and forwards to the render scene. Keeps camera/post-process
    // resolution in a system instead of inline in the extract phase.
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
