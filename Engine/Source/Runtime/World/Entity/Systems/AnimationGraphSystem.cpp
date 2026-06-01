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
    FSystemAccess SAnimationGraphSystem::Access = FSystemAccess{}
        .Write<SSkeletalMeshComponent>()
        .Read<SAnimationGraphComponent, SBlackboardComponent>();

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

            if (Mesh.VisibilityBasedAnimTick == EAnimUpdateMode::TickWhenRendered &&
                (Now - Mesh.LastRenderedTime) > kAnimVisibilityGrace)
            {
                return;
            }

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

            // Init VM state first so Execute won't re-init and wipe the written values.
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

            FAnimationGraphVM::Execute(Graph, Skeleton, DeltaTime, AnimGraph.VMState, Mesh.BoneTransforms);
        });
    }
}
