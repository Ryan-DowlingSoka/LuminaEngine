#pragma once
#include "EntitySystem.h"
#include "EventBusSystem.generated.h"

namespace Lumina
{
    // Flushes the per-world FLuaEventBus deferred queue (a ctx singleton) once per frame. Runs first at
    // FrameStart so events queued last frame fire before gameplay systems; serial, since dispatch runs Lua.
    REFLECT(System)
    struct SEventBusSystem
    {
        GENERATED_BODY()
        ENTITY_SYSTEM(RequiresUpdate(EUpdateStage::FrameStart, EUpdatePriority::Highest))

        static void Update(const FSystemContext& Context) noexcept;
    };
}
