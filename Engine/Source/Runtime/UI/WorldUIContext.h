#pragma once

#include <glm/glm.hpp>
#include "Containers/Array.h"
#include "Containers/String.h"

namespace Rml
{
    class Context;
    class ElementDocument;
}

namespace Lumina
{
    class FRHIImage;

    // One world-space widget to rasterize this frame. Built on the game thread in
    // TickWorldWidgets (iterating SWidgetComponent) and consumed on the render thread in
    // RenderWorldWidgets, so the render thread never touches the live entity registry.
    // Non-owning: the Rml context + RT live on the SWidgetComponent that outlives the frame.
    struct FWidgetRenderJob
    {
        Rml::Context* Context = nullptr;
        FRHIImage*    Target = nullptr;
        glm::uvec2    Size{0, 0};
    };

    // Per-world UI state, owned by CWorld (mirrors RenderScene/PhysicsScene ownership).
    // The RmlUi bridge holds only process-global backend state; this holds the world's
    // own Rml context and its loaded documents. Created in InitializeWorld, destroyed
    // in TeardownWorld, so its lifetime tracks the world with no external hooks.
    struct FWorldUIContext
    {
        Rml::Context*                            Context = nullptr;
        THashMap<FString, Rml::ElementDocument*> Documents;

        // Editor override: lay UI out at this size instead of the RT image size.
        // Zero means use the RT size (standalone-runtime default).
        glm::uvec2                               DisplaySize{0, 0};

        // Per-frame world-space widget render jobs (cleared + refilled each TickWorldWidgets).
        TVector<FWidgetRenderJob>                WidgetJobs;
    };
}
