#pragma once
#include "SceneRenderTypes.h"
#include "Platform/GenericPlatform.h"
#include "Renderer/PrimitiveDrawInterface.h"
#include "Renderer/RHIFwd.h"
#include "World/Scene/SceneInterface.h"


namespace Lumina
{
    class FViewVolume;

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
        virtual void RenderView(ICommandList& CmdList, const FViewVolume&) = 0;
        
        virtual entt::entity GetEntityAtPixel(uint32 X, uint32 Y) const = 0;
        
        virtual FRHIImage* GetRenderTarget() const = 0;

        virtual const FSceneRenderStats&  GetRenderStats() const = 0;
        virtual FSceneRenderSettings&     GetSceneRenderSettings() = 0;
    };
}
