#pragma once
#include "EntitySystem.h"
#include "Core/Object/ObjectMacros.h"
#include "AnimationGraphSystem.generated.h"

namespace Lumina
{
    // Evaluates SAnimationGraphComponent each frame (FAnimationGraphVM bytecode into SSkeletalMeshComponent).
    // Runs in PrePhysics so the pose is ready before rendering, and during Paused for editor scrubbing.
    REFLECT(System)
    struct SAnimationGraphSystem
    {
        GENERATED_BODY()
        ENTITY_SYSTEM(RequiresUpdate(EUpdateStage::PrePhysics), RequiresUpdate(EUpdateStage::Paused))

    public:

        static void Update(const FSystemContext& SystemContext) noexcept;
    };
}
