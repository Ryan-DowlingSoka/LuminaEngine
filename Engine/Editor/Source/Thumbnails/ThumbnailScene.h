#pragma once

#include "Containers/Function.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "Memory/SmartPtr.h"
#include "Platform/GenericPlatform.h"
#include "World/World.h"

namespace Lumina
{
    struct FPackageThumbnail;
}

namespace Lumina
{
    // Transient one-shot world used to render asset thumbnails.
    class FThumbnailScene
    {
    public:

        explicit FThumbnailScene(uint32 RenderTargetSize = 512);
        ~FThumbnailScene();

        LE_NO_COPYMOVE(FThumbnailScene);

        // Constructs the world, initializes its renderer, resizes the render
        // target to the requested square size, and creates a default camera
        // entity at SetCameraTransform's neutral pose. Call before populating.
        void Begin();

        // Tears down the world. Safe to call repeatedly; called by destructor.
        void End();

        CWorld* GetWorld() const { return World; }
        entt::entity GetCameraEntity() const { return CameraEntity; }

        // Place the thumbnail camera. Recomputes the view matrix immediately
        // so render results are deterministic without ticking the world.
        void SetCameraTransform(const glm::vec3& Position, const glm::vec3& Target, float FOVDegrees = 35.0f);

        // Renders one frame and reads back into Thumbnail (256x256 RGBA8).
        // Returns false if the world or render target is missing.
        bool Capture(FPackageThumbnail& Thumbnail);

    private:

        TObjectPtr<CWorld> World;
        entt::entity       CameraEntity = entt::null;
        uint32             RTSize       = 512;
        bool               bInitialized = false;
    };
}
