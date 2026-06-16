#pragma once
#include "EntitySystem.h"
#include "CSharpScriptSystem.generated.h"

namespace Lumina
{
    // Ticks C# EntityScripts. For each SCSharpScriptComponent it binds a managed instance
    // (generation-keyed, so the same path handles first attach and hot-reload rebind) and
    // dispatches OnUpdate. Reflected -> auto-registered via RegisterECSSystem, like SScriptSystem.
    REFLECT(System)
    struct SCSharpScriptSystem
    {
        GENERATED_BODY()
        ENTITY_SYSTEM(RequiresUpdate(EUpdateStage::PrePhysics))

        static void Update(const FSystemContext& Context) noexcept;
    };
}
