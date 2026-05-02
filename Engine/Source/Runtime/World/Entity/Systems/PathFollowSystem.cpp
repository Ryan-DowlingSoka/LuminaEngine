#include "pch.h"
#include "PathFollowSystem.h"

#include "TaskSystem/TaskSystem.h"
#include "World/Entity/EntityUtils.h"
#include "World/Entity/Components/CharacterControllerComponent.h"
#include "World/Entity/Components/PathFollowComponent.h"
#include "World/Entity/Components/TransformComponent.h"
#include "World/Entity/Systems/NavMeshSystem.h"

namespace Lumina
{
    namespace
    {
        // Copy a query path into the component's fixed-size corner array,
        // truncating if the result exceeds MaxCorners. Truncation is rare
        // for typical agent ranges; bumping MaxCorners is a free knob.
        void StorePath(SPathFollowComponent& Comp, const FNavPath& Path)
        {
            const int32 N = (int32)std::min<size_t>(Path.Corners.size(), (size_t)SPathFollowComponent::MaxCorners);
            for (int32 i = 0; i < N; ++i)
            {
                Comp.PathCorners[i] = Path.Corners[i];
            }
            Comp.CornerCount = N;
            Comp.CurrentCorner = 0;
        }

        // Resolve the active goal: prefer a tracked entity's current
        // location over the latched static world position. Returns false if
        // the tracked entity has been destroyed since SetTargetEntity.
        //
        // Reads STransformComponent::WorldTransform directly. The caller
        // must have flushed dirty transforms via ResolveAllDirtyTransforms
        // before invoking this from the parallel body.
        bool ResolveGoal(const FSystemContext& Context, SPathFollowComponent& Comp, glm::vec3& OutGoal)
        {
            if (Comp.TargetEntity != entt::null)
            {
                if (!Context.IsValidEntity(Comp.TargetEntity))
                {
                    Comp.bHasTarget = false;
                    Comp.TargetEntity = entt::null;
                    return false;
                }
                if (auto* T = Context.TryGet<STransformComponent>(Comp.TargetEntity))
                {
                    Comp.TargetLocation = T->WorldTransform.Location;
                }
            }
            OutGoal = Comp.TargetLocation;
            return true;
        }
    }

    void SPathFollowSystem::Update(const FSystemContext& Context) noexcept
    {
        LUMINA_PROFILE_SCOPE();

        const float DeltaTime = (float)Context.GetDeltaTime();
        auto View = Context.CreateView<SPathFollowComponent, STransformComponent>();
        auto Handle = View.handle();
        if (Handle->empty()) return;

        // Bulk-resolve every dirty transform once on the calling thread so
        // the parallel body below can read STransformComponent::WorldTransform
        // directly and skip the global ResolveTransformChain mutex on every
        // access. Cost is O(dirty), so an empty-dirty frame is essentially free.
        //
        // Contract for the ParallelFor body: it MUST NOT mutate any
        // transform. Today the only ECS write inside the loop is
        // SCharacterControllerComponent::AddMovementInput, which only
        // touches MoveInput - if that ever changes, either revert these
        // reads to GetWorldTransform()/GetWorldLocation() or call
        // ResolveAllDirtyTransforms again before the affected reads.
        ECS::Utils::ResolveAllDirtyTransforms(Context.GetRegistry());

        // Path-follow is per-entity-independent: each agent's path request,
        // corner advance, and steering write-out is isolated. Parallel-fan
        // matches the SimpleAnimationSystem pattern.
        Task::ParallelFor(Handle->size(), [&](uint32 Index)
        {
            entt::entity Entity = (*Handle)[Index];
            if (!View.contains(Entity)) return;

            SPathFollowComponent& Comp = View.get<SPathFollowComponent>(Entity);
            STransformComponent&  Xform = View.get<STransformComponent>(Entity);

            Comp.TimeSinceLastPath += DeltaTime;

            if (!Comp.bHasTarget)
            {
                Comp.CornerCount = 0;
                Comp.Status = EPathFollowStatus::None;
                return;
            }

            glm::vec3 Goal;
            if (!ResolveGoal(Context, Comp, Goal))
            {
                Comp.CornerCount = 0;
                Comp.Status = EPathFollowStatus::None;
                return;
            }

            const glm::vec3 AgentPos = Xform.WorldTransform.Location;

            // Decide whether to (re)plan. Reasons: path was explicitly
            // marked dirty (target swap), no cached corners, target moved
            // beyond RepathDistance from the source point, or the
            // backstop interval elapsed.
            const bool bMovedTarget = glm::length(Goal - Comp.PathSourceTarget) > Comp.RepathDistance;
            const bool bIntervalElapsed = Comp.TimeSinceLastPath > Comp.RepathInterval;
            const bool bNeedRepath = Comp.bPathDirty || Comp.CornerCount == 0 || bMovedTarget || bIntervalElapsed;

            if (bNeedRepath)
            {
                FNavPath Path;
                FNavQueryFilter Filter;
                if (Nav::FindPath(Context, AgentPos, Goal, Filter, Path) && Path.bValid)
                {
                    StorePath(Comp, Path);
                    Comp.PathSourceTarget = Goal;
                    Comp.bPathDirty = false;
                    Comp.TimeSinceLastPath = 0.0f;
                    Comp.Status = EPathFollowStatus::Following;
                    Comp.ConsecutiveFailures = 0;
                }
                else
                {
                    // Path failed - latch the failure so script can react,
                    // but keep any prior corners in case the next tick
                    // succeeds; clearing here would stutter the agent
                    // during a transient nav-edge case. Reset the timer so
                    // the next attempt waits one full RepathInterval
                    // instead of hammering FindPath every tick.
                    Comp.Status = EPathFollowStatus::Failed;
                    ++Comp.ConsecutiveFailures;
                    Comp.TimeSinceLastPath = 0.0f;
                    Comp.bPathDirty = false;
                    if (Comp.CornerCount == 0)
                    {
                        return;
                    }
                }
            }

            // Advance the corner cursor while the agent is within the
            // acceptance radius of the current corner. Multiple corners
            // can collapse in a single tick at high speed / dense paths.
            while (Comp.CurrentCorner < Comp.CornerCount)
            {
                const glm::vec3 ToCorner = Comp.PathCorners[Comp.CurrentCorner] - AgentPos;
                const glm::vec3 Flat(ToCorner.x, 0.0f, ToCorner.z);
                if (glm::length(Flat) <= Comp.AcceptanceRadius)
                {
                    ++Comp.CurrentCorner;
                    continue;
                }
                break;
            }

            if (Comp.CurrentCorner >= Comp.CornerCount)
            {
                if (Comp.Status == EPathFollowStatus::Following)
                {
                    Comp.Status = EPathFollowStatus::Reached;
                }
                return;
            }

            // Steer toward the next corner. Project to horizontal so the
            // controller doesn't try to walk vertically; vertical motion
            // is the controller / physics' problem.
            const glm::vec3 Dir = Comp.PathCorners[Comp.CurrentCorner] - AgentPos;
            const glm::vec3 Flat(Dir.x, 0.0f, Dir.z);
            const float Len = glm::length(Flat);
            if (Len < 1e-4f)
            {
                return;
            }
            const glm::vec3 Move = (Flat / Len) * Comp.Speed;

            if (Comp.bDriveCharacterController)
            {
                if (auto* CC = Context.TryGet<SCharacterControllerComponent>(Entity))
                {
                    CC->AddMovementInput(Move);
                }
            }
        });

        // Debug draw is a sequential pass: the line batcher's vector
        // pushes aren't thread-safe, so we can't emit from inside the
        // ParallelFor. Cheap relative to the steering work above.
        const glm::vec3 Lift(0.0f, 0.1f, 0.0f);
        const glm::vec4 PathColor(0.4f, 0.8f, 1.0f, 1.0f);
        const glm::vec4 ActiveSegmentColor(1.0f, 0.95f, 0.2f, 1.0f);
        const glm::vec4 GoalColor(1.0f, 0.4f, 0.4f, 1.0f);
        for (entt::entity Entity : View)
        {
            SPathFollowComponent& Comp = View.get<SPathFollowComponent>(Entity);
            if (!Comp.bDrawDebugPath || Comp.CornerCount == 0)
            {
                continue;
            }
            STransformComponent& Xform = View.get<STransformComponent>(Entity);
            // Same fresh-as-of-top-of-Update guarantee from the bulk resolve.
            const glm::vec3 AgentPos = Xform.WorldTransform.Location + Lift;

            // Active segment from the agent to the next pending corner.
            const int32 Cur = std::min(Comp.CurrentCorner, Comp.CornerCount - 1);
            Context.DrawDebugLine(AgentPos, Comp.PathCorners[Cur] + Lift, ActiveSegmentColor, 2.0f, -1.0f);

            // Remaining corner-to-corner segments.
            for (int32 i = Cur; i + 1 < Comp.CornerCount; ++i)
            {
                Context.DrawDebugLine(Comp.PathCorners[i] + Lift, Comp.PathCorners[i + 1] + Lift, PathColor, 1.5f, -1.0f);
            }

            // Goal marker (small horizontal cross at the final corner).
            const glm::vec3 Goal = Comp.PathCorners[Comp.CornerCount - 1] + Lift;
            const float CrossSize = std::max(Comp.AcceptanceRadius, 0.25f);
            Context.DrawDebugLine(Goal - glm::vec3(CrossSize, 0, 0), Goal + glm::vec3(CrossSize, 0, 0), GoalColor, 1.5f, -1.0f);
            Context.DrawDebugLine(Goal - glm::vec3(0, 0, CrossSize), Goal + glm::vec3(0, 0, CrossSize), GoalColor, 1.5f, -1.0f);
        }
    }
}
