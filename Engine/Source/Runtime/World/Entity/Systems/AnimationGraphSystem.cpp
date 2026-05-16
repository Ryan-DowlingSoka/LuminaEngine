#include "pch.h"
#include "AnimationGraphSystem.h"

#include "Animation/AnimationGraphVM.h"
#include "Assets/AssetTypes/Animation/AnimationGraph/AnimationGraph.h"
#include "Assets/AssetTypes/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Assets/AssetTypes/Mesh/Skeleton/Skeleton.h"
#include "Renderer/MeshData.h"
#include "World/Entity/Components/AnimationGraphComponent.h"
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

        const float DeltaTime = (float)SystemContext.GetDeltaTime();

        Task::ParallelFor(Handle->size(), [&](uint32 Index)
        {
            entt::entity Entity = (*Handle)[Index];

            if (!View.contains(Entity))
            {
                return;
            }

            SAnimationGraphComponent& AnimGraph = View.get<SAnimationGraphComponent>(Entity);
            SSkeletalMeshComponent&   Mesh      = View.get<SSkeletalMeshComponent>(Entity);

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

            // FAnimationGraphVM::Execute lazily (re)initializes the VM state when
            // it is stale, so a freshly added component or a swapped graph asset
            // is handled here without an explicit init pass.
            FAnimationGraphVM::Execute(Graph, Skeleton, DeltaTime, AnimGraph.VMState, Mesh.BoneTransforms);
        });
    }
}
