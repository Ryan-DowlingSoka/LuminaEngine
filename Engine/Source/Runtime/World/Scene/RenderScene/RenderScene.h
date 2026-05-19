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

        // Game thread: read ECS and populate the FFrameData snapshot for the
        // current FrameIndex slot. Scene implementations N-buffer per-frame
        // state so Extract and RenderView_RenderThread for different frames
        // can run concurrently. Extract may still block on a per-slot fence
        // when the game thread laps the render thread by FRAMES_IN_FLIGHT.
        virtual void Extract(const FViewVolume&, const SPostProcessSettings* PostProcess) = 0;

        // Render thread: record this view's draws onto CmdList from the
        // FFrameData slot identified by FrameIndex. Reads only the snapshot
        // populated by the matching Extract; never touches the live ECS.
        virtual void RenderView_RenderThread(ICommandList& CmdList, uint8 FrameIndex) = 0;

        // Render thread: called from the render lambda after the LAST CPU
        // read of FrameRing[Slot] for this frame (i.e. after ImGui composite,
        // RmlUi, present, etc.). Releases the slot back to the game thread.
        virtual void SignalFrameConsumed(uint8 FrameIndex) {}
        
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
