#pragma once
#include "MaterialManager.h"
#include "TextureManager.h"
#include "Core/Delegates/Delegate.h"
#include "Subsystems/Subsystem.h"


namespace Lumina
{
    class ICommandList;
    class IImGuiRenderer;
    class IRenderContext;
}

namespace Lumina
{
    class FRenderManager
    {
    public:

        static TMulticastDelegate<void, glm::vec2> OnSwapchainResized;

        FRenderManager();
        ~FRenderManager();

        void Initialize();

        void FrameStart(const FUpdateContext& UpdateContext);
        void FrameEnd(const FUpdateContext& UpdateContext, ICommandList& CmdList);

        void SwapchainResized(glm::vec2 NewSize);


        #if WITH_EDITOR
        IImGuiRenderer* GetImGuiRenderer() const { return ImGuiRenderer; }
        #endif

        uint32 GetCurrentFrameIndex() const { return CurrentFrameIndex; }

        NODISCARD RHI::FTextureManager& GetTextureManager() const { return *TextureManager.get(); }
        NODISCARD RHI::FTextureManager* TryGetTextureManager() const { return TextureManager.get(); }
        NODISCARD RHI::FMaterialManager& GetMaterialManager() const { return *MaterialManager.get(); }

    private:

        #if WITH_EDITOR
        IImGuiRenderer*                     ImGuiRenderer = nullptr;
        #endif

        TUniquePtr<RHI::FTextureManager>    TextureManager;
        TUniquePtr<RHI::FMaterialManager>   MaterialManager;

        uint8                               CurrentFrameIndex = 0;
    };


    RUNTIME_API extern FRenderManager* GRenderManager;
}
