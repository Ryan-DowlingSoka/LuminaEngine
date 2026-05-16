#pragma once
#include "SceneRenderTypes.h"
#include "Platform/GenericPlatform.h"
#include "Renderer/PrimitiveDrawInterface.h"
#include "Renderer/RHIFwd.h"
#include "World/Scene/SceneInterface.h"


namespace Lumina
{
    class FViewVolume;
    class CMaterialInterface;
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

        // Set the chain of post-process materials to apply this frame.
        // Resolved by the world from the active camera + any post-process
        // volumes the camera is inside. The renderer does not retain the
        // pointers across frames -- the world rebuilds the list each tick.
        virtual void SetActivePostProcessMaterials(const TVector<CMaterialInterface*>& Materials) {}

        // Game thread: read ECS and populate per-frame scene state. Caller must
        // gate this on the previous frame's RenderView_RenderThread completion
        // (e.g. via FlushRenderingCommands) or it races on scene storage.
        virtual void Extract(const FViewVolume&, const SPostProcessSettings* PostProcess) = 0;

        // Render thread: record this view's draws onto CmdList. Reads only the
        // scene state populated by the matching Extract call -- never the ECS.
        virtual void RenderView_RenderThread(ICommandList& CmdList) = 0;
        
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
