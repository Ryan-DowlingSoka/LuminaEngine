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

        // Writes only the skeletal pose; reads anim-graph + blackboard params. Safe to run alongside
        // systems that touch disjoint components (e.g. SPathFollowSystem). Defined in the .cpp.
        static FSystemAccess Access;

        static void Update(const FSystemContext& SystemContext) noexcept;
    };
}
