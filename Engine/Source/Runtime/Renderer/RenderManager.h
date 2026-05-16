#pragma once
#include "MaterialManager.h"
#include "RenderTypes.h"
#include "TextureManager.h"
#include "Core/Delegates/Delegate.h"
#include "Subsystems/Subsystem.h"


namespace Lumina
{
    class ICommandList;
    class IImGuiRenderer;
    class IRenderContext;
    class FRHICommandList;
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

        // Game thread: ImGui::NewFrame (and any other backend per-frame init).
        void FrameStart(const FUpdateContext& UpdateContext);

        // Game thread: snapshot ImGui DrawData and enqueue the whole render
        // pipeline (cmd list create, all recording, submit, present, wait) onto
        // the render thread. Returns immediately.
        void FrameEnd();

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
