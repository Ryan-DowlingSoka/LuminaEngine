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
    };
}
