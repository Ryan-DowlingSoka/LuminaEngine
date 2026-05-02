#include "pch.h"
#include "SimpleAnimationSystem.h"

#include "Assets/AssetTypes/Mesh/Animation/Animation.h"
#include "Assets/AssetTypes/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Assets/AssetTypes/Mesh/Skeleton/Skeleton.h"
#include "Renderer/MeshData.h"
#include "World/Entity/Components/SimpleAnimationComponent.h"
#include "World/Entity/Components/SkeletalMeshComponent.h"

namespace Lumina
{
    void SSimpleAnimationSystem::Update(const FSystemContext& SystemContext) noexcept
    {
        LUMINA_PROFILE_SCOPE();
        auto View = SystemContext.CreateView<SSimpleAnimationComponent, SSkeletalMeshComponent>();

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

            SSimpleAnimationComponent& Anim = View.get<SSimpleAnimationComponent>(Entity);
            SSkeletalMeshComponent&    Mesh = View.get<SSkeletalMeshComponent>(Entity);

            // No animation or no mesh asset -> nothing to do. Don't clear the
            // pose buffer; the renderer treats an empty BoneTransforms as "not
            // a skinned draw".
            if (!Anim.Animation.IsValid() || !Mesh.SkeletalMesh.IsValid())
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

            // Steady-state fast path: a paused-and-already-sampled component
            // contributes nothing this frame. The pose buffer still holds the
            // last sampled pose so the mesh keeps rendering at the frozen
            // frame, and the render scene re-uploads it from the same vector.
            if (!Anim.bPlaying && !Anim.bDirty)
            {
                return;
            }

            if (Anim.bPlaying)
            {
                Anim.CurrentTime += DeltaTime * Anim.PlaybackSpeed;
            }

            const float Duration = Anim.Animation->GetDuration();

            if (Duration > 0.0f && Anim.CurrentTime >= Duration)
            {
                if (Anim.bLooping)
                {
                    Anim.CurrentTime = fmodf(Anim.CurrentTime, Duration);
                }
                else
                {
                    // One-shot finished: clamp to the final frame so the pose
                    // still resolves on this tick, then latch the finished
                    // state. The dirty flag is cleared below so the next tick
                    // skips the resample entirely until the user calls
                    // PlayAnimation/Resume again.
                    Anim.CurrentTime = Duration;
                    Anim.bPlaying    = false;
                    Anim.bFinished   = true;
                }
            }

            Anim.Animation->SamplePose(Anim.CurrentTime, Skeleton, Mesh.BoneTransforms);
            Anim.bDirty = false;
        });
    }
}
