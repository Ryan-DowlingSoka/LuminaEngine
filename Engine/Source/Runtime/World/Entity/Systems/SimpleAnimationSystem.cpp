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
    namespace
    {
        bool Contains(const TVector<int32>& Indices, int32 Value)
        {
            for (int32 V : Indices)
            {
                if (V == Value)
                {
                    return true;
                }
            }
            return false;
        }

        // Runs on the game thread (after the parallel pose pass) because Lua refs are
        // single-threaded. Cheap: only entities that bound at least one handler reach here.
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

            // ---- Point notifies: fire once as the playhead crosses, playback only. ----
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
                                Callback.Invoke(Entity, Notify.NotifyName, Notify.Time);
                            }
                        }
                    }
                }
            }

            // ---- Notify states: diff the active set to drive Begin / Tick / End. ----
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

                // End: states that were active last frame but no longer are. Fires on a
                // natural exit, a loop wrap, or Stop()/seek -- so trails etc. always clean up.
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
                            Binding.OnEnd.Invoke(Entity, State.NotifyName, State.EndTime);
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
                            Binding.OnBegin.Invoke(Entity, State.NotifyName, State.StartTime);
                        }
                        if (bAdvanced && Binding.OnTick.IsInvokable())
                        {
                            Binding.OnTick.Invoke(Entity, State.NotifyName, Alpha);
                        }
                    }
                }

                Anim.ActiveNotifyStates = eastl::move(NowActive);
            }
        }
    }

    void SSimpleAnimationSystem::Update(const FSystemContext& SystemContext) noexcept
    {
        LUMINA_PROFILE_SCOPE();
        auto View = SystemContext.CreateView<SSimpleAnimationComponent, SSkeletalMeshComponent>();

        auto Handle = View.handle();
        if (Handle->empty())
        {
            return;
        }

        const float  DeltaTime = (float)SystemContext.GetDeltaTime();
        const double Now       = SystemContext.GetTime();
        // Skeletons not rendered within this window are treated as off-screen.
        constexpr double kAnimVisibilityGrace = 0.25;

        Task::ParallelFor(Handle->size(), [&](uint32 Index)
        {
            entt::entity Entity = (*Handle)[Index];

            if (!View.contains(Entity))
            {
                return;
            }

            SSimpleAnimationComponent& Anim = View.get<SSimpleAnimationComponent>(Entity);
            SSkeletalMeshComponent&    Mesh = View.get<SSkeletalMeshComponent>(Entity);

            // Off-screen: freeze the pose (skip the SamplePose hot path and time advance)
            // until the mesh is rendered again. Notifies don't fire while frozen.
            if (Mesh.VisibilityBasedAnimTick == EAnimUpdateMode::TickWhenRendered &&
                (Now - Mesh.LastRenderedTime) > kAnimVisibilityGrace)
            {
                Anim.bAdvancedThisFrame = false;
                return;
            }

            // No animation or no mesh asset -> nothing to do. Don't clear the
            // pose buffer; the renderer treats an empty BoneTransforms as "not
            // a skinned draw".
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

            // Steady-state fast path: a paused-and-already-sampled component
            // contributes nothing this frame. The pose buffer still holds the
            // last sampled pose so the mesh keeps rendering at the frozen
            // frame, and the render scene re-uploads it from the same vector.
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

                Anim.bAdvancedThisFrame = true;
            }
            else
            {
                // Dirty resample with no playback (scrub / Stop / freshly played but
                // not yet ticked): no segment was traversed, so suppress point notifies.
                Anim.PreviousTime       = Anim.CurrentTime;
                Anim.bAdvancedThisFrame = false;
            }

            Anim.Animation->SamplePose(Anim.CurrentTime, Skeleton, Mesh.BoneTransforms);
            Anim.bDirty = false;
        });

        // Serial pass: fire notifies for the (typically few) entities that bound
        // handlers. Kept out of the ParallelFor because Lua refs are not thread-safe.
        for (size_t i = 0; i < Handle->size(); ++i)
        {
            entt::entity Entity = (*Handle)[i];
            if (!View.contains(Entity))
            {
                continue;
            }

            SSimpleAnimationComponent& Anim = View.get<SSimpleAnimationComponent>(Entity);
            if (!Anim.HasNotifyBindings() || !Anim.Animation.IsValid())
            {
                continue;
            }

            FireNotifies(Entity, Anim);
        }
    }
}
