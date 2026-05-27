#pragma once
#include "Core/Object/ObjectHandleTyped.h"
#include "World/Entity/Components/CameraComponent.h"
#include "World/Entity/Components/PostProcessSettings.h"
#include "World/Entity/Registry/EntityRegistry.h"


namespace Lumina
{
    class CWorld;

    /**
     * Owns the active-camera selection and the cinematic blend between cameras.
     *
     * Switching with a non-zero blend time snapshots the currently displayed view
     * (the "from" pose) and eases toward the new active camera over BlendTime.
     * SCameraSystem drives the blend each frame: it ticks the timer, interpolates
     * pose/FOV/post-process from the snapshot toward the live target, and stores
     * the resolved result back here so the next switch can blend from it.
     */
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
            glm::vec3               FromPosition = glm::vec3(0.0f);
            glm::quat               FromRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
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
        void StoreResolvedView(const glm::vec3& Position, const glm::quat& Rotation, float FOV, const SPostProcessSettings& PostProcess);
        FORCEINLINE bool HasResolvedView() const { return bHasResolvedView; }

    private:

        FEntityRegistry&    Registry;
        entt::entity        ActiveCameraEntity = entt::null;

        FBlendState         Blend;

        // Last fully resolved (post-blend) view; the source for the next blend's snapshot.
        glm::vec3               LastViewPosition = glm::vec3(0.0f);
        glm::quat               LastViewRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        float                   LastViewFOV = 90.0f;
        SPostProcessSettings    LastPostProcess;
        bool                    bHasResolvedView = false;
    };
}
