#pragma once

#include "EntitySystem.h"
#include "AudioSystem.generated.h"

namespace Lumina
{
    REFLECT(System)
    struct SAudioSystem
    {
        GENERATED_BODY()
        ENTITY_SYSTEM(RequiresUpdate(EUpdateStage::PostPhysics))

        static void Startup(const FSystemContext& Context) noexcept;
        static void Update(const FSystemContext& Context) noexcept;
        static void Teardown(const FSystemContext& Context) noexcept;
    };
}
