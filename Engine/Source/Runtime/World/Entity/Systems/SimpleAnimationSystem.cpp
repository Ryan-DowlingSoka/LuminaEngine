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

            Anim.Animation->SamplePose(Anim.CurrentTime, Skeleton, Mesh.BoneTransforms);
            Anim.bDirty = false;
        });

        // Serial: Lua refs are not thread-safe.
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
