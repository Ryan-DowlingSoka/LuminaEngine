#include "pch.h"
#include "NetMovementInterpSystem.h"

#include "SystemContext.h"
#include "TaskSystem/TaskSystem.h"
#include "Config/NetworkSettings.h"
#include "Core/Object/ObjectCore.h"
#include "World/Net/NetWorldState.h"
#include "World/Entity/EntityUtils.h"
#include "World/Entity/Components/RepTransformComponent.h"
#include "World/Entity/Components/NetworkComponent.h"
#include "World/Entity/Components/TransformComponent.h"

namespace Lumina
{
    // Disjoint write set so the scheduler can overlap this with other PostPhysics systems that don't touch
    // transforms. Dirty-tagging (FNeedsTransformUpdate) happens in the serial tail, covered by the transform
    // write domain (same convention as SAnimationSystem's root-motion pass).
    FSystemAccess SNetMovementInterpSystem::Access = FSystemAccess{}
        .Write<STransformComponent>()
        .Read<FRepTransform, SNetworkComponent>();

    void SNetMovementInterpSystem::Update(const FSystemContext& Context) noexcept
    {
        LUMINA_PROFILE_SCOPE();

        entt::registry& Registry = Context.GetRegistry();

        // No net state => not a networked world; nothing to interpolate.
        FNetWorldState* State = Registry.ctx().find<FNetWorldState>();
        if (State == nullptr)
        {
            return;
        }

        auto View   = Registry.view<FRepTransform>();
        auto Handle = View.handle();
        const uint32 Count = static_cast<uint32>(Handle->size());
        if (Count == 0)
        {
            return;
        }

        //~ Serial pre-step. RenderTime and the clock offset are global render-clock state; computing them in
        //  the parallel body would race. Track the server/client offset with a gentle EMA so RenderTime stays
        //  ~InterpDelay behind the newest received server time and advances with the local frame clock.
        const CNetworkSettings* Settings = GetDefault<CNetworkSettings>();
        const double InterpDelay  = Settings ? static_cast<double>(Settings->InterpDelay) : 0.1;
        const bool   bExtrapolate = Settings ? Settings->bEnableExtrapolation : true;
        const double MaxExtrap    = Settings ? static_cast<double>(Settings->MaxExtrapolation) : 0.25;

        const double ClientNow = Context.GetTime();
        if (!State->bClockInitialized)
        {
            State->ClockOffset       = State->LatestServerTime - ClientNow;
            State->bClockInitialized = true;
        }
        else
        {
            const double Measured = State->LatestServerTime - ClientNow;
            State->ClockOffset += (Measured - State->ClockOffset) * 0.02;
        }
        const double RenderTime = ClientNow + State->ClockOffset - InterpDelay;

        auto&& RepStorage       = Registry.storage<FRepTransform>();
        auto&& NetStorage       = Registry.storage<SNetworkComponent>();
        auto&& TransformStorage = Registry.storage<STransformComponent>();

        // True for any entity whose pose we should drive from its sample ring. AutonomousProxy is locally
        // controlled (its ring is never filled, but skip explicitly); empty rings have no data yet.
        auto ShouldInterp = [&](entt::entity Entity) -> bool
        {
            if (!RepStorage.contains(Entity) || !NetStorage.contains(Entity) || !TransformStorage.contains(Entity))
            {
                return false;
            }
            if (RepStorage.get(Entity).Ring.Count == 0)
            {
                return false;
            }
            return NetStorage.get(Entity).LocalRole != ENetRole::AutonomousProxy;
        };

        //~ Parallel phase. Per-entity writes to disjoint STransformComponent slots are safe; SetFromNetwork
        //  deliberately does NOT mark dirty (MarkDirty mutates the transform pool under a mutex, which is not
        //  ParallelFor-safe). No structural changes, no GuidTable lookups here.
        Task::ParallelFor(Count, [&](const Task::FParallelRange& Range)
        {
            for (uint32 i = Range.Start; i < Range.End; ++i)
            {
                const entt::entity Entity = (*Handle)[i];
                if (!ShouldInterp(Entity))
                {
                    continue;
                }

                FRepTransform&       Rep = RepStorage.get(Entity);
                STransformComponent& T   = TransformStorage.get(Entity);

                FVector3 Pos;
                FQuat    Rot;
                Rep.Ring.Evaluate(RenderTime, Pos, Rot, bExtrapolate, MaxExtrap);

                // Scale isn't interpolated; apply the latest replicated value, else keep the local scale.
                const FVector3 Scale = Rep.bHasScale ? Rep.CurrentScaleQ.ToVector(NetQuantize::ScaleQuantum)
                                                     : T.GetLocalScale();
                T.SetFromNetwork(Pos, Rot, Scale);
            }
        }, 16);

        //~ Serial tail (Jolt ApplyInterpolatedTransforms pattern). Structural dirty-tagging + one bulk resolve.
        //  Tagging a different pool (FNeedsTransformUpdate) doesn't invalidate the FRepTransform handle.
        for (uint32 i = 0; i < Count; ++i)
        {
            const entt::entity Entity = (*Handle)[i];
            if (ShouldInterp(Entity))
            {
                Registry.emplace_or_replace<FNeedsTransformUpdate>(Entity);
            }
        }
        ECS::Utils::ResolveAllDirtyTransforms(Registry);
    }
}
