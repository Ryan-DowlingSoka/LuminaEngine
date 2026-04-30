#pragma once
#include "SceneRenderTypes.h"
#include "Platform/GenericPlatform.h"
#include "Renderer/PrimitiveDrawInterface.h"
#include "Renderer/RHIFwd.h"
#include "World/Scene/SceneInterface.h"


namespace Lumina
{
    class FViewVolume;
    struct SPostProcessSettings;

    class IRenderScene : public ISceneInterface, public IPrimitiveDrawInterface
    {
    public:
        virtual ~IRenderScene() = default;

        virtual void Init() = 0;
        virtual void Shutdown() = 0;

        // Frame boundary, scene prepares/clears per-frame state
        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;

        // Record this view's draws onto the provided command list.
        // PostProcess is the active camera's grading; pass nullptr to skip
        // grading and use the default tonemap behaviour.
        virtual void RenderView(ICommandList& CmdList, const FViewVolume&, const SPostProcessSettings* PostProcess = nullptr) = 0;
        
        virtual entt::entity GetEntityAtPixel(uint32 X, uint32 Y) const = 0;
        
        virtual FRHIImage* GetRenderTarget() const = 0;

        // Re-create the scene's render target at a new size. Used by transient
        // render paths (e.g. thumbnail capture) that need a fixed RT independent
        // of the swapchain.
        virtual void Resize(const glm::uvec2& NewSize) = 0;

        virtual const FSceneRenderStats&  GetRenderStats() const = 0;
        virtual FSceneRenderSettings&     GetSceneRenderSettings() = 0;

        // Returns the scene's shadow atlas, or null if the scene has none
        // (forward has one, deferred does not yet).
        virtual const FShadowAtlas* GetShadowAtlas() const { return nullptr; }
    };
}
