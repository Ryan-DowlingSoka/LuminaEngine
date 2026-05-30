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

        // Post-process material chain for this frame, resolved by the world from the active camera +
        // volumes. Not retained across frames -- the world rebuilds the list each tick.
        virtual void SetActivePostProcessMaterials(const TVector<CMaterialInterface*>& Materials) {}

        // Game thread: populate the frame slot's snapshot. N-buffered so Extract and RenderView run
        // concurrently; Extract back-pressures on the slot's consumed fence.
        virtual void Extract(const FViewVolume&, const SPostProcessSettings* PostProcess) = 0;

        // Render thread: record this view's draws from the slot's snapshot.
        virtual void RenderView_RenderThread(ICommandList& CmdList, uint8 FrameIndex) = 0;

        // Render thread: release the slot after the last CPU read for this frame.
        virtual void SignalFrameConsumed(uint8 FrameIndex) {}
        
        virtual entt::entity GetEntityAtPixel(uint32 X, uint32 Y) const = 0;

        // Editor only: pick-cursor position (picker-RT texels) so readback copies just a region around it.
        // bOverViewport=false skips the readback that frame.
        virtual void SetPickerCursor(uint32 X, uint32 Y, bool bOverViewport) {}

        virtual FRHIImage* GetRenderTarget() const = 0;

        // Scene-capture views: render the world from an extra camera into its own RT (gather once, shade each).
        // Register returns an opaque handle (-1 on fail); SetCaptureView/GetCaptureRenderTarget drive + display it.
        virtual int32 RegisterCaptureView(const FUIntVector2& Size) { return -1; }
        virtual void  SetCaptureView(int32 Handle, const FViewVolume& View, bool bEnabled) {}
        virtual FRHIImage* GetCaptureRenderTarget(int32 Handle) const { return nullptr; }

        // Re-create the scene's render target at a new size. Used by transient render paths
        // (e.g. thumbnail capture) needing a fixed RT independent of the swapchain.
        virtual void Resize(const FUIntVector2& NewSize) = 0;

        virtual const FSceneRenderStats&  GetRenderStats() const = 0;
        virtual FSceneRenderSettings&     GetSceneRenderSettings() = 0;

        // Returns the scene's shadow atlas, or null if the scene has none
        // (forward has one, deferred does not yet).
        virtual const FShadowAtlas* GetShadowAtlas() const { return nullptr; }
    };
}
