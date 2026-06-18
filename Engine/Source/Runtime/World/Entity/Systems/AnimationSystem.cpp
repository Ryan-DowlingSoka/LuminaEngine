#include "pch.h"
#include "AnimationSystem.h"

#include "Animation/AnimationGraphVM.h"
#include "Animation/Pose.h"
#include "Animation/RootMotion.h"
#include "Assets/AssetTypes/Animation/AnimationGraph/AnimationGraph.h"
#include "Assets/AssetTypes/Mesh/Animation/Animation.h"
#include "Assets/AssetTypes/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Assets/AssetTypes/Mesh/Skeleton/Skeleton.h"
#include "Renderer/MeshData.h"
#include "TaskSystem/TaskGraph.h"
#include "world/entity/components/entitytags.h"
#include "World/Entity/Components/AnimationGraphComponent.h"
#include "World/Entity/Components/BlackboardComponent.h"
#include "World/Entity/Components/SimpleAnimationComponent.h"
#include "World/Entity/Components/SkeletalMeshComponent.h"
#include "World/Entity/Components/TransformComponent.h"
#include "World/Entity/Systems/SystemResources.h"

namespace Lumina
{
    FSystemAccess SAnimationSystem::Access = FSystemAccess{}
        .Write<SSkeletalMeshComponent, STransformComponent>()
        .Read<SSimpleAnimationComponent, SAnimationGraphComponent, SBlackboardComponent>();

    // Skeletons not rendered within this window are treated as off-screen (a few frames of slack so brief
    // occlusion / culling flicker doesn't stutter the pose).
    static constexpr double kAnimVisibilityGrace = 0.25;

    namespace
    {
        // One entity's single-clip pose evaluation (parallel-safe: touches only this entity's components).
        // Root-motion deltas are stashed on the component and applied serially after the dispatch joins.
        void EvaluateSimple(SSimpleAnimationComponent& Anim, SSkeletalMeshComponent& Mesh, float DeltaTime, double Now)
        {
            if (Mesh.VisibilityBasedAnimTick == EAnimUpdateMode::TickWhenRendered &&
                (Now - Mesh.LastRenderedTime) > kAnimVisibilityGrace)
            {
                Anim.bAdvancedThisFrame = false;
                return;
            }

            if (!Anim.Animation.IsValid() || !Mesh.SkeletalMesh.IsValid())
            {
                Anim.bAdvancedThisFrame = false;
                return;
            }

            CSkeletalMesh* SkelMesh = Mesh.SkeletalMesh;
            if (!SkelMesh->Skeleton.IsValid())
            {
                Anim.bAdvancedThisFrame = false;
                return;
            }

            FSkeletonResource* Skeleton = SkelMesh->Skeleton->GetSkeletonResource();
            if (Skeleton == nullptr || Skeleton->GetNumBones() == 0)
            {
                Anim.bAdvancedThisFrame = false;
                return;
            }

            if (!Anim.bPlaying && !Anim.bDirty)
            {
                Anim.bAdvancedThisFrame = false;
                return;
            }

            const float Duration = Anim.Animation->GetDuration();

            if (Anim.bPlaying)
            {
                // Record the pre-advance time so the notify pass can find crossings.
                Anim.PreviousTime = Anim.CurrentTime;
                Anim.CurrentTime += DeltaTime * Anim.PlaybackSpeed;

                if (Duration > 0.0f && Anim.CurrentTime >= Duration)
                {
                    if (Anim.bLooping)
                    {
                        Anim.CurrentTime = fmodf(Anim.CurrentTime, Duration);
                    }
                    else
                    {
                        Anim.CurrentTime = Duration;
                        Anim.bPlaying    = false;
                        Anim.bFinished   = true;
                    }
                }

                Anim.bAdvancedThisFrame = true;
            }
            else
            {
                Anim.PreviousTime       = Anim.CurrentTime;
                Anim.bAdvancedThisFrame = false;
            }

            CAnimation* Asset = Anim.Animation.Get();

            // Per-thread scratch pose; SampleLocalPose gives us the local-space TRS so root motion can
            // be locked / extracted before FK folds the hierarchy into skinning matrices.
            thread_local FPose LocalPose;
            Asset->SampleLocalPose(Anim.CurrentTime, Skeleton, LocalPose);

            const int32 RootIdx = RootMotion::ResolveRootBoneIndex(Skeleton, Asset->RootBoneName);

            const bool bLock = (Anim.RootMotionLock == ERootMotionLockMode::ForceLock) ||
                               (Anim.RootMotionLock == ERootMotionLockMode::FromAsset && Asset->bLockRootMotion);
            const bool bExtract = !bLock && Asset->bEnableRootMotion;

            Anim.PendingRootMotion.bHasMotion = false;

            if (RootIdx != INDEX_NONE)
            {
                if (bLock)
                {
                    RootMotion::PinRootToBindPose(LocalPose, Skeleton, RootIdx);
                }
                else if (bExtract && Anim.bAdvancedThisFrame)
                {
                    // Skip on seek/stop frames (handled by bAdvancedThisFrame) so scrubbing doesn't teleport
                    // the entity. The root is stripped from the in-place pose so the mesh stays centered.
                    Anim.PendingRootMotion = RootMotion::ExtractRootDelta(
                        Asset, Skeleton, RootIdx, Anim.PreviousTime, Anim.CurrentTime, Anim.bLooping, Duration);
                    RootMotion::PinRootToBindPose(LocalPose, Skeleton, RootIdx);
                }
            }

            AnimPose::ToSkinningMatrices(LocalPose, Skeleton, Mesh.BoneTransforms);
            Anim.bDirty = false;
        }

        // One entity's animation-graph evaluation (parallel-safe: touches only this entity's components).
        void EvaluateGraph(const FSystemContext& SystemContext, entt::entity Entity,
                           SAnimationGraphComponent& AnimGraph, SSkeletalMeshComponent& Mesh, float DeltaTime, double Now)
        {
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

            // Graph root motion: lock only (ForceLock). FromAsset / ForceUnlock leave the root free --
            // a graph has no single source asset, and extraction through a blend tree isn't implemented yet.
            const bool bLockRoot = (AnimGraph.RootMotionLock == ERootMotionLockMode::ForceLock);
            const int32 RootIdx  = bLockRoot ? RootMotion::ResolveRootBoneIndex(Skeleton, FName()) : INDEX_NONE;

            FAnimationGraphVM::Execute(Graph, Skeleton, DeltaTime, AnimGraph.VMState, Mesh.BoneTransforms, bLockRoot, RootIdx);
        }
    }

    void SAnimationSystem::Update(const FSystemContext& SystemContext) noexcept
    {
        LUMINA_PROFILE_SCOPE();

        auto SimpleView = SystemContext.CreateView<SSimpleAnimationComponent, SSkeletalMeshComponent>(entt::exclude<SDisabledTag>);
        auto GraphView  = SystemContext.CreateView<SAnimationGraphComponent, SSkeletalMeshComponent>(entt::exclude<SDisabledTag>);

        auto SimpleHandle = SimpleView.handle();
        auto GraphHandle  = GraphView.handle();

        const bool bHasSimple = SimpleHandle != nullptr && !SimpleHandle->empty();
        const bool bHasGraph  = GraphHandle != nullptr && !GraphHandle->empty();

        if (!bHasSimple && !bHasGraph)
        {
            return;
        }

        const float  DeltaTime = (float)SystemContext.GetDeltaTime();
        const double Now       = SystemContext.GetTime();

        // Both passes touch disjoint entity sets; dispatch them as independent task-graph roots so their
        // chunks drain the worker pool together instead of running in two serialized batches.
        FTaskGraph Graph;

        if (bHasSimple)
        {
            Graph.AddParallelFor((uint32)SimpleHandle->size(), 16, [&](const Task::FParallelRange& Range)
            {
                for (uint32 i = Range.Start; i < Range.End; ++i)
                {
                    entt::entity Entity = (*SimpleHandle)[i];
                    if (!SimpleView.contains(Entity))
                    {
                        continue;
                    }
                    EvaluateSimple(SimpleView.get<SSimpleAnimationComponent>(Entity),
                                   SimpleView.get<SSkeletalMeshComponent>(Entity), DeltaTime, Now);
                }
            });
        }

        if (bHasGraph)
        {
            Graph.AddParallelFor((uint32)GraphHandle->size(), 16, [&](const Task::FParallelRange& Range)
            {
                for (uint32 i = Range.Start; i < Range.End; ++i)
                {
                    entt::entity Entity = (*GraphHandle)[i];
                    if (!GraphView.contains(Entity))
                    {
                        continue;
                    }
                    EvaluateGraph(SystemContext, Entity,
                                  GraphView.get<SAnimationGraphComponent>(Entity),
                                  GraphView.get<SSkeletalMeshComponent>(Entity), DeltaTime, Now);
                }
            });
        }

        Graph.Dispatch();
        Graph.Wait();

        if (!bHasSimple)
        {
            return;
        }

        // Serial: applying root motion marks the transform dirty (a structural registry change, not
        // ParallelFor-safe).
        auto&& TransformStorage = SystemContext.GetStorage<STransformComponent>();
        for (size_t i = 0; i < SimpleHandle->size(); ++i)
        {
            entt::entity Entity = (*SimpleHandle)[i];
            if (!SimpleView.contains(Entity))
            {
                continue;
            }

            SSimpleAnimationComponent& Anim = SimpleView.get<SSimpleAnimationComponent>(Entity);

            if (Anim.PendingRootMotion.bHasMotion)
            {
                STransformComponent& Transform = TransformStorage.get(Entity);
                FTransform Delta;
                Delta.SetLocation(Anim.PendingRootMotion.Translation);
                Delta.SetRotation(Anim.PendingRootMotion.Rotation);
                Delta.SetScale(FVector3(1.0f));
                Transform.SetLocalTransform(Transform.LocalTransform * Delta);
                Anim.PendingRootMotion.bHasMotion = false;
            }
        }
    }
}
