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

        // Returns the scene's shadow atlas if the scene has one. Forward
        // rendering owns an atlas; deferred does not (yet) — debug tools
        // must check for null.
        virtual const FShadowAtlas* GetShadowAtlas() const { return nullptr; }
    };
}
