#pragma once
#include "Core/Object/ObjectHandleTyped.h"
#include "World/Entity/Components/CameraComponent.h"
#include "World/Entity/Components/PostProcessSettings.h"
#include "World/Entity/Registry/EntityRegistry.h"


namespace Lumina
{
    class CWorld;

    // Owns active-camera selection + cinematic blend. A non-zero blend snapshots the current view and eases
    // to the new camera over BlendTime; SCameraSystem ticks it each frame and stores the result back here.
    class FCameraManager
    {
    public:

        struct FBlendState
        {
            bool                    bActive = false;
            float                   Elapsed = 0.0f;
            float                   Duration = 0.0f;
            ECameraBlendFunction    Function = ECameraBlendFunction::EaseInOut;

            // Snapshot of the displayed view when the blend began.
            FVector3               FromPosition = FVector3(0.0f);
            FQuat               FromRotation = FQuat(1.0f, 0.0f, 0.0f, 0.0f);
            float                   FromFOV = 90.0f;
            SPostProcessSettings    FromPostProcess;
        };

        FCameraManager(CWorld* InWorld);

        /** Switch the active camera. BlendTime > 0 eases from the current view; 0 snaps. */
        void SetActiveCamera(entt::entity InEntity, float BlendTime = 0.0f, ECameraBlendFunction Function = ECameraBlendFunction::EaseInOut);

        FORCEINLINE entt::entity GetActiveCameraEntity() const { return ActiveCameraEntity; }
        SCameraComponent* GetCameraComponent() const;

        //~ Blend state, driven by SCameraSystem.

        /** Advance the blend timer; clears the blend once Elapsed reaches Duration. */
        void TickBlend(float DeltaTime);
        FORCEINLINE bool IsBlending() const { return Blend.bActive; }
        FORCEINLINE const FBlendState& GetBlend() const { return Blend; }

        /** Record the view actually displayed this frame so a later switch can blend from it. */
        void StoreResolvedView(const FVector3& Position, const FQuat& Rotation, float FOV, const SPostProcessSettings& PostProcess);
        FORCEINLINE bool HasResolvedView() const { return bHasResolvedView; }

    private:

        FEntityRegistry&    Registry;
        entt::entity        ActiveCameraEntity = entt::null;

        FBlendState         Blend;

        // Last fully resolved (post-blend) view; the source for the next blend's snapshot.
        FVector3               LastViewPosition = FVector3(0.0f);
        FQuat               LastViewRotation = FQuat(1.0f, 0.0f, 0.0f, 0.0f);
        float                   LastViewFOV = 90.0f;
        SPostProcessSettings    LastPostProcess;
        bool                    bHasResolvedView = false;
    };
}
