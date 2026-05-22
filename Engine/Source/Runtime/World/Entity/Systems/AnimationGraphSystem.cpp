#include "pch.h"
#include "AnimationGraphSystem.h"

#include "Animation/AnimationGraphVM.h"
#include "Assets/AssetTypes/Animation/AnimationGraph/AnimationGraph.h"
#include "Assets/AssetTypes/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Assets/AssetTypes/Mesh/Skeleton/Skeleton.h"
#include "Renderer/MeshData.h"
#include "World/Entity/Components/AnimationGraphComponent.h"
#include "World/Entity/Components/BlackboardComponent.h"
#include "World/Entity/Components/SkeletalMeshComponent.h"

namespace Lumina
{
    void SAnimationGraphSystem::Update(const FSystemContext& SystemContext) noexcept
    {
        LUMINA_PROFILE_SCOPE();
        auto View = SystemContext.CreateView<SAnimationGraphComponent, SSkeletalMeshComponent>();

        auto Handle = View.handle();
        if (Handle->empty())
        {
            return;
        }

        const float  DeltaTime = (float)SystemContext.GetDeltaTime();
        const double Now       = SystemContext.GetTime();
        // Skeletons not rendered within this window are treated as off-screen (a few frames of
        // slack so brief occlusion / culling flicker doesn't stutter the pose).
        constexpr double kAnimVisibilityGrace = 0.25;

        Task::ParallelFor(Handle->size(), [&](uint32 Index)
        {
            entt::entity Entity = (*Handle)[Index];

            if (!View.contains(Entity))
            {
                return;
            }

            SAnimationGraphComponent& AnimGraph = View.get<SAnimationGraphComponent>(Entity);
            SSkeletalMeshComponent&   Mesh      = View.get<SSkeletalMeshComponent>(Entity);

            // Off-screen pose evaluation skip: the graph VM is the per-skeleton hot path, so
            // for crowds where most skeletons aren't on screen this is the big win. The pose
            // simply freezes (and the clocks with it) until the mesh is rendered again.
            if (Mesh.VisibilityBasedAnimTick == EAnimUpdateMode::TickWhenRendered &&
                (Now - Mesh.LastRenderedTime) > kAnimVisibilityGrace)
            {
                return;
            }

            // No graph or no mesh asset -> nothing to do. Leave BoneTransforms
            // untouched; the renderer treats an empty buffer as "not skinned".
            if (!AnimGraph.Graph.IsValid() || !Mesh.SkeletalMesh.IsValid())
            {
                return;
            }

            CAnimationGraph* Graph = AnimGraph.Graph.Get();
            if (!Graph->IsCompiled())
            {
                return;
            }

            CSkeletalMesh* SkelMesh = Mesh.SkeletalMesh;
            if (!SkelMesh->Skeleton.IsValid())
            {
                return;
            }

            FSkeletonResource* Skeleton = SkelMesh->Skeleton->GetSkeletonResource();
            if (Skeleton == nullptr || Skeleton->GetNumBones() == 0)
            {
                return;
            }

            // Resolve the graph's referenced parameters from the entity's
            // blackboard. Init the VM state first so it is sized and marked
            // current -- Execute then won't re-init and wipe the values we write.
            // No blackboard component -> values keep their compiled defaults.
            if (SBlackboardComponent* Blackboard = SystemContext.TryGet<SBlackboardComponent>(Entity))
            {
                Blackboard->EnsureInitialized();
                AnimGraph.EnsureStateInitialized();

                const int32 NumParams = (int32)Graph->Parameters.size();
                for (int32 i = 0; i < NumParams && i < (int32)AnimGraph.VMState.Parameters.size(); ++i)
                {
                    const FAnimGraphParameter& Param = Graph->Parameters[i];
                    AnimGraph.VMState.Parameters[i] = Blackboard->GetFloat(Param.Name, Param.DefaultValue);
                }
            }

            // FAnimationGraphVM::Execute lazily (re)initializes the VM state when
            // it is stale, so a freshly added component or a swapped graph asset
            // is handled here without an explicit init pass.
            FAnimationGraphVM::Execute(Graph, Skeleton, DeltaTime, AnimGraph.VMState, Mesh.BoneTransforms);
        });
    }
}
