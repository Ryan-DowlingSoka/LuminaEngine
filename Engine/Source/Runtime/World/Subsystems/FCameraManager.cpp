#include "pch.h"
#include "FCameraManager.h"
#include "World/World.h"

namespace Lumina
{
    FCameraManager::FCameraManager(CWorld* InWorld)
        : Registry(InWorld->GetEntityRegistry())
    {
    }

    SCameraComponent* FCameraManager::GetCameraComponent() const
    {
        return Registry.try_get<SCameraComponent>(ActiveCameraEntity);
    }

    void FCameraManager::SetActiveCamera(entt::entity InEntity, float BlendTime, ECameraBlendFunction Function)
    {
        // No-op switches keep any running blend intact.
        if (InEntity == ActiveCameraEntity)
        {
            return;
        }

        // Snapshot the currently displayed view as the blend source. With no prior
        // resolved view (first activation) or a zero duration we just snap.
        if (BlendTime > 0.0f && bHasResolvedView)
        {
            Blend.bActive   = true;
            Blend.Elapsed   = 0.0f;
            Blend.Duration  = BlendTime;
            Blend.Function  = Function;
            Blend.FromPosition    = LastViewPosition;
            Blend.FromRotation    = LastViewRotation;
            Blend.FromFOV         = LastViewFOV;
            Blend.FromPostProcess = LastPostProcess;
        }
        else
        {
            Blend.bActive = false;
        }

        ActiveCameraEntity = InEntity;
    }

    void FCameraManager::TickBlend(float DeltaTime)
    {
        if (!Blend.bActive)
        {
            return;
        }

        Blend.Elapsed += DeltaTime;
        if (Blend.Elapsed >= Blend.Duration)
        {
            Blend.bActive = false;
        }
    }

    void FCameraManager::StoreResolvedView(const glm::vec3& Position, const glm::quat& Rotation, float FOV, const SPostProcessSettings& PostProcess)
    {
        LastViewPosition = Position;
        LastViewRotation = Rotation;
        LastViewFOV      = FOV;
        LastPostProcess  = PostProcess;
        bHasResolvedView = true;
    }
}
