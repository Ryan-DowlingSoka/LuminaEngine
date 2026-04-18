#pragma once
#include "EntitySystem.h"
#include "CameraSystem.generated.h"

namespace Lumina
{
    REFLECT(System)
    struct SCameraSystem
    {
        GENERATED_BODY()
        ENTITY_SYSTEM(RequiresUpdate(EUpdateStage::DuringPhysics), RequiresUpdate(EUpdateStage::Paused))

        static void Startup(const FSystemContext& Context) noexcept;
        static void Update(const FSystemContext& Context) noexcept;
        static void Teardown(const FSystemContext& Context) noexcept;
    };
}
