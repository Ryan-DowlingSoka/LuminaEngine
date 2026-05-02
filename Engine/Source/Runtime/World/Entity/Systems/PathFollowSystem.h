#pragma once

#include "EntitySystem.h"
#include "Core/Object/ObjectMacros.h"
#include "PathFollowSystem.generated.h"

namespace Lumina
{
    /**
     * Ticks every SPathFollowComponent: keeps the cached path fresh by
     * calling Nav::FindPath when stale, advances the corner cursor when
     * within AcceptanceRadius, and writes a movement direction into the
     * paired SCharacterControllerComponent (when present) so existing
     * physics-driven motion handles the actual locomotion.
     *
     * Lives in PrePhysics so movement input is consumed by the same
     * frame's physics step, matching CharacterMovementSystem.
     */
    REFLECT(System)
    struct RUNTIME_API SPathFollowSystem
    {
        GENERATED_BODY()
        ENTITY_SYSTEM(RequiresUpdate(EUpdateStage::PrePhysics))

    public:

        static void Update(const FSystemContext& Context) noexcept;
    };
}
