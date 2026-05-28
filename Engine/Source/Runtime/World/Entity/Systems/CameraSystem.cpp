#include "pch.h"
#include "CameraSystem.h"
#include "SystemSingletons.h"
#include "World/World.h"
#include "World/Entity/Components/CameraComponent.h"
#include "World/Entity/Components/PostProcessComponent.h"
#include "World/Entity/Components/EntityTags.h"
#include "World/Subsystems/FCameraManager.h"

namespace Lumina
{
    float EvaluateCameraBlend(ECameraBlendFunction Function, float Alpha)
    {
        Alpha = Math::Clamp(Alpha, 0.0f, 1.0f);
        switch (Function)
        {
        case ECameraBlendFunction::EaseIn:    return Alpha * Alpha;
        case ECameraBlendFunction::EaseOut:   return Alpha * (2.0f - Alpha);
        case ECameraBlendFunction::EaseInOut: return Alpha * Alpha * (3.0f - 2.0f * Alpha); // smoothstep
        case ECameraBlendFunction::Linear:
        default:                              return Alpha;
        }
    }

    namespace Detail
    {
        static void NewCameraConstructed(entt::registry& Registry, entt::entity Entity)
        {
            // Auto-activate is a play-mode spawn convenience. In the editor it would hijack the
            // viewport with no easy way back.
            const CWorld* World = Registry.ctx().get<CWorld*>();
            if (World == nullptr || World->GetWorldType() == EWorldType::Editor)
            {
                return;
            }

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

        SCameraComponent& Camera = Registry.get<SCameraComponent>(CameraEntity);

        // Live pose of the active camera; the blend (if any) eases toward this.
        const FVector3 TargetPosition = CameraTransform.GetWorldLocation();
        const FQuat TargetRotation = CameraTransform.GetWorldRotation();
        const float     TargetFOV      = Camera.FOV;

        const FVector3 CameraWorldPos = TargetPosition;
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
                const FMatrix4 InvWorld = Math::Inverse(VolXform.GetWorldMatrix());
                const FVector3 LocalCam = FVector3(InvWorld * FVector4(CameraWorldPos, 1.0f));
                const FVector3 D = Math::Abs(LocalCam) - Volume.BoxExtent;
                const float Outside = Math::Max(D.x, Math::Max(D.y, D.z));

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
                const FMatrix4 InvWorld = Math::Inverse(VolXform.GetWorldMatrix());
                const FVector3 LocalCam = FVector3(InvWorld * FVector4(CameraWorldPos, 1.0f));
                const FVector3 D = Math::Abs(LocalCam) - Volume.BoxExtent;
                const float Outside = Math::Max(D.x, Math::Max(D.y, D.z));
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

        // Drive the camera-to-camera blend: ease from the snapshot toward the live target.
        CameraManager->TickBlend((float)Context.GetDeltaTime());

        FVector3            FinalPosition    = TargetPosition;
        FQuat            FinalRotation    = TargetRotation;
        float                FinalFOV         = TargetFOV;
        SPostProcessSettings FinalPostProcess = ResolvedPostProcess;

        if (CameraManager->IsBlending())
        {
            const FCameraManager::FBlendState& B = CameraManager->GetBlend();
            const float Alpha = EvaluateCameraBlend(B.Function, B.Duration > 0.0f ? B.Elapsed / B.Duration : 1.0f);

            FQuat To = TargetRotation;
            if (Math::Dot(B.FromRotation, To) < 0.0f)
            {
                To = -To; // Shortest-arc slerp.
            }

            FinalPosition = Math::Mix(B.FromPosition, TargetPosition, Alpha);
            FinalRotation = Math::Slerp(B.FromRotation, To, Alpha);
            FinalFOV      = Math::Mix(B.FromFOV, TargetFOV, Alpha);

            FinalPostProcess = B.FromPostProcess;
            BlendPostProcessSettings(FinalPostProcess, ResolvedPostProcess, Alpha);
        }

        // Bake the resolved view into the camera component so consumers reading its
        // matrices directly (editor gizmo, CPU picking) match the rendered view.
        // SetResolvedView leaves the authored FOV property intact for the blend target.
        Camera.SetResolvedView(
            FinalPosition,
            FinalRotation * FVector3(0.0f, 0.0f, 1.0f),
            FinalRotation * FVector3(0.0f, 1.0f, 0.0f),
            FinalFOV);

        Resolved.ViewVolume      = Camera.GetViewVolume();
        Resolved.PostProcess     = FinalPostProcess;
        Resolved.bHasView        = true;
        Resolved.bHasPostProcess = true;

        CameraManager->StoreResolvedView(FinalPosition, FinalRotation, FinalFOV, FinalPostProcess);
    }
}
