#include "pch.h"
#include "CameraSystem.h"
#include "SystemSingletons.h"
#include "World/Entity/Components/CameraComponent.h"
#include "World/Entity/Components/PostProcessComponent.h"
#include "World/Entity/Components/EntityTags.h"
#include "World/Subsystems/FCameraManager.h"

namespace Lumina
{
    namespace Detail
    {
        static void NewCameraConstructed(entt::registry& Registry, entt::entity Entity)
        {
            SCameraComponent& Camera = Registry.get<SCameraComponent>(Entity);
            if (Camera.bAutoActivate)
            {
                Registry.ctx().get<entt::dispatcher&>().trigger<FSwitchActiveCameraEvent>(FSwitchActiveCameraEvent{Entity});
            }
        }
    }

    void SCameraSystem::Startup(const FSystemContext& Context) noexcept
    {
        Context.GetRegistry().on_construct<SCameraComponent>().connect<&Detail::NewCameraConstructed>();
    }

    void SCameraSystem::Teardown(const FSystemContext& Context) noexcept
    {
    }

    void SCameraSystem::Update(const FSystemContext& Context) noexcept
    {
        LUMINA_PROFILE_SCOPE();

        entt::registry& Registry = Context.GetRegistry();
        FResolvedSceneView& Resolved = Registry.ctx().get<FResolvedSceneView>();

        Resolved.bHasView = false;
        Resolved.bHasPostProcess = false;
        Resolved.PostProcessMaterials.clear();

        FCameraManager* CameraManager = Registry.ctx().get<FCameraManager*>();
        const entt::entity CameraEntity = CameraManager->GetActiveCameraEntity();
        if (!Registry.valid(CameraEntity) || !Registry.all_of<SCameraComponent, STransformComponent>(CameraEntity))
        {
            return;
        }

        const STransformComponent& CameraTransform = Registry.get<STransformComponent>(CameraEntity);
        (void)CameraTransform.GetWorldMatrix();

        const glm::quat CameraRotation = CameraTransform.GetWorldRotation();
        SCameraComponent& Camera = Registry.get<SCameraComponent>(CameraEntity);
        Camera.SetView(
            CameraTransform.GetWorldLocation(),
            CameraRotation * glm::vec3(0.0f, 0.0f, 1.0f),
            CameraRotation * glm::vec3(0.0f, 1.0f, 0.0f));

        const glm::vec3 CameraWorldPos = CameraTransform.GetWorldLocation();
        SPostProcessSettings ResolvedPostProcess = Camera.PostProcess;

        struct FVolumeContribution { float Weight; const SPostProcessSettings* Settings; int32 Priority; };
        TVector<FVolumeContribution> Contributions;

        auto VolumeView = Registry.view<const SPostProcessComponent, const STransformComponent>(entt::exclude<SDisabledTag>);
        for (entt::entity VolEntity : VolumeView)
        {
            const SPostProcessComponent& Volume = VolumeView.get<const SPostProcessComponent>(VolEntity);
            if (!Volume.bEnabled || Volume.BlendWeight <= 0.0f)
            {
                continue;
            }

            float Weight = Volume.BlendWeight;

            if (!Volume.bInfiniteExtent)
            {
                const STransformComponent& VolXform = VolumeView.get<const STransformComponent>(VolEntity);
                const glm::mat4 InvWorld = glm::inverse(VolXform.GetWorldMatrix());
                const glm::vec3 LocalCam = glm::vec3(InvWorld * glm::vec4(CameraWorldPos, 1.0f));
                const glm::vec3 D = glm::abs(LocalCam) - Volume.BoxExtent;
                const float Outside = glm::max(D.x, glm::max(D.y, D.z));

                if (Outside > Volume.BlendDistance)
                {
                    continue;
                }
                if (Outside > 0.0f && Volume.BlendDistance > 0.0001f)
                {
                    Weight *= 1.0f - (Outside / Volume.BlendDistance);
                }
            }

            if (Weight > 0.0f)
            {
                Contributions.push_back({Weight, &Volume.Settings, Volume.Priority});
            }
        }

        eastl::sort(Contributions.begin(), Contributions.end(), [](const FVolumeContribution& A, const FVolumeContribution& B)
        {
            return A.Priority < B.Priority;
        });

        for (const FVolumeContribution& Contribution : Contributions)
        {
            BlendPostProcessSettings(ResolvedPostProcess, *Contribution.Settings, Contribution.Weight);
        }

        TVector<CMaterialInterface*>& PostProcessMaterials = Resolved.PostProcessMaterials;
        for (const TObjectPtr<CMaterialInterface>& M : Camera.PostProcessMaterials)
        {
            if (M.IsValid())
            {
                PostProcessMaterials.push_back(M.Get());
            }
        }

        struct FMaterialVolumeRef { entt::entity Entity; int32 Priority; };
        TVector<FMaterialVolumeRef> MaterialVolumes;
        for (entt::entity VolEntity : VolumeView)
        {
            const SPostProcessComponent& Volume = VolumeView.get<const SPostProcessComponent>(VolEntity);
            if (!Volume.bEnabled || Volume.PostProcessMaterials.empty())
            {
                continue;
            }
            if (!Volume.bInfiniteExtent)
            {
                const STransformComponent& VolXform = VolumeView.get<const STransformComponent>(VolEntity);
                const glm::mat4 InvWorld = glm::inverse(VolXform.GetWorldMatrix());
                const glm::vec3 LocalCam = glm::vec3(InvWorld * glm::vec4(CameraWorldPos, 1.0f));
                const glm::vec3 D = glm::abs(LocalCam) - Volume.BoxExtent;
                const float Outside = glm::max(D.x, glm::max(D.y, D.z));
                if (Outside > Volume.BlendDistance)
                {
                    continue;
                }
            }
            MaterialVolumes.push_back({VolEntity, Volume.Priority});
        }
        
        eastl::sort(MaterialVolumes.begin(), MaterialVolumes.end(), [](const FMaterialVolumeRef& A, const FMaterialVolumeRef& B)
        {
            return A.Priority < B.Priority;
        });
        
        for (const FMaterialVolumeRef& Ref : MaterialVolumes)
        {
            const SPostProcessComponent& Volume = VolumeView.get<const SPostProcessComponent>(Ref.Entity);
            for (const TObjectPtr<CMaterialInterface>& M : Volume.PostProcessMaterials)
            {
                if (M.IsValid())
                {
                    PostProcessMaterials.push_back(M.Get());
                }
            }
        }

        Resolved.ViewVolume = Camera.GetViewVolume();
        Resolved.PostProcess = ResolvedPostProcess;
        Resolved.bHasView = true;
        Resolved.bHasPostProcess = true;
    }
}
