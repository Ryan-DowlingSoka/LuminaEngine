#pragma once
#include "EntitySystem.h"
#include "Core/Object/ObjectMacros.h"
#include "AnimationGraphSystem.generated.h"

namespace Lumina
{
    // Evaluates SAnimationGraphComponent each frame: runs the graph's compiled
    // bytecode through FAnimationGraphVM and writes the resolved skinning
    // matrices into the entity's SSkeletalMeshComponent. Runs in PrePhysics so
    // the pose is ready before rendering, and during Paused so editor-time
    // scrubbing still resolves.
    REFLECT(System)
    struct SAnimationGraphSystem
    {
        GENERATED_BODY()
        ENTITY_SYSTEM(RequiresUpdate(EUpdateStage::PrePhysics), RequiresUpdate(EUpdateStage::Paused))

    public:

        static void Update(const FSystemContext& SystemContext) noexcept;
    };
}
