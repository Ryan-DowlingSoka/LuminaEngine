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
        .Write<SSkeletalMeshComponent, STransformComponent, SystemResource::LuaVM>()
        .Read<SSimpleAnimationComponent, SAnimationGraphComponent, SBlackboardComponent>();

    // Skeletons not rendered within this window are treated as off-screen (a few frames of slack so brief
    // occlusion / culling flicker doesn't stutter the pose).
    static constexpr double kAnimVisibilityGrace = 0.25;

    namespace
    {
        bool Contains(const TVector<int32>& Indices, int32 Value)
        {
            return eastl::any_of(Indices.begin(), Indices.end(), [Value] (int32 Index)
            {
                return Index == Value;
            });
        }

        // Game thread only: Lua refs are not thread-safe.
        void FireNotifies(entt::entity Entity, SSimpleAnimationComponent& Anim)
        {
            FAnimationResource* Res = Anim.Animation->GetAnimationResource();
            if (Res == nullptr)
            {
                return;
            }

            const float Prev      = Anim.PreviousTime;
            const float Cur       = Anim.CurrentTime;
            const bool  bAdvanced = Anim.bAdvancedThisFrame;
            const bool  bWrapped  = Cur < Prev; // looped past the end this frame

            // Point notifies: fire once as the playhead crosses, playback only.
            if (bAdvanced && !Anim.NotifyHandlers.empty())
            {
                for (const FAnimationNotify& Notify : Res->Notifies)
                {
                    auto It = Anim.NotifyHandlers.find(Notify.NotifyName);
                    if (It == Anim.NotifyHandlers.end())
                    {
                        continue;
                    }

                    const bool bCrossed = bWrapped
                        ? (Notify.Time > Prev || Notify.Time <= Cur)
                        : (Notify.Time > Prev && Notify.Time <= Cur);

                    if (bCrossed)
                    {
                        for (Lua::FRef& Callback : It->second)
                        {
                            if (Callback.IsInvokable())
                            {
                                std::ignore = Callback.Invoke(Entity, Notify.NotifyName, Notify.Time);
                            }
                        }
                    }
                }
            }

            // Notify states: diff the active set to drive Begin / Tick / End.
            if (!Anim.NotifyStateHandlers.empty())
            {
                TVector<int32> NowActive;
                for (int32 i = 0; i < (int32)Res->NotifyStates.size(); ++i)
                {
                    const FAnimationNotifyState& State = Res->NotifyStates[i];
                    if (Anim.NotifyStateHandlers.find(State.NotifyName) == Anim.NotifyStateHandlers.end())
                    {
                        continue;
                    }
                    if (Cur >= State.StartTime && Cur < State.EndTime)
                    {
                        NowActive.push_back(i);
                    }
                }

                for (int32 Idx : Anim.ActiveNotifyStates)
                {
                    if (Idx < 0 || Idx >= (int32)Res->NotifyStates.size() || Contains(NowActive, Idx))
                    {
                        continue;
                    }
                    const FAnimationNotifyState& State = Res->NotifyStates[Idx];
                    auto It = Anim.NotifyStateHandlers.find(State.NotifyName);
                    if (It == Anim.NotifyStateHandlers.end())
                    {
                        continue;
                    }
                    for (FNotifyStateBinding& Binding : It->second)
                    {
                        if (Binding.OnEnd.IsInvokable())
                        {
                            std::ignore = Binding.OnEnd.Invoke(Entity, State.NotifyName, State.EndTime);
                        }
                    }
                }

                // Begin (newly entered, playback only) + Tick (while inside, playback only).
                for (int32 Idx : NowActive)
                {
                    const FAnimationNotifyState& State = Res->NotifyStates[Idx];
                    auto It = Anim.NotifyStateHandlers.find(State.NotifyName);
                    if (It == Anim.NotifyStateHandlers.end())
                    {
                        continue;
                    }

                    const bool  bWasActive = Contains(Anim.ActiveNotifyStates, Idx);
                    const float Range      = State.EndTime - State.StartTime;
                    float Alpha            = Range > 0.0f ? (Cur - State.StartTime) / Range : 0.0f;
                    Alpha = Alpha < 0.0f ? 0.0f : (Alpha > 1.0f ? 1.0f : Alpha);

                    for (FNotifyStateBinding& Binding : It->second)
                    {
                        if (!bWasActive && bAdvanced && Binding.OnBegin.IsInvokable())
                        {
                            std::ignore = Binding.OnBegin.Invoke(Entity, State.NotifyName, State.StartTime);
                        }
                        if (bAdvanced && Binding.OnTick.IsInvokable())
                        {
                            std::ignore = Binding.OnTick.Invoke(Entity, State.NotifyName, Alpha);
                        }
                    }
                }

                Anim.ActiveNotifyStates = eastl::move(NowActive);
            }
        }

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
        // ParallelFor-safe), and Lua notify refs are not thread-safe.
        
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
                Delta.Location = Anim.PendingRootMotion.Translation;
                Delta.Rotation = Anim.PendingRootMotion.Rotation;
                Delta.Scale    = FVector3(1.0f);
                Transform.SetLocalTransform(Transform.LocalTransform * Delta);
                Anim.PendingRootMotion.bHasMotion = false;
            }

            if (Anim.HasNotifyBindings() && Anim.Animation.IsValid())
            {
                FireNotifies(Entity, Anim);
            }
        }
    }
}
