#include "pch.h"
#include "RenderManager.h"

#include "API/Vulkan/VulkanRenderContext.h"
#include "Tools/UI/ImGui/Vulkan/VulkanImGuiRender.h"

#include "RHIGlobals.h"
#include "Core/Profiler/Profile.h"
#include "Tools/UI/ImGui/ImGuiRenderer.h"

namespace Lumina
{
    TMulticastDelegate<void, glm::vec2> FRenderManager::OnSwapchainResized;
    RUNTIME_API FRenderManager* GRenderManager = nullptr;

    FRenderManager::FRenderManager()
    {
    }

    FRenderManager::~FRenderManager()
    {
        
        #if WITH_EDITOR
        ImGuiRenderer->Deinitialize();
        Memory::Delete(ImGuiRenderer);
        ImGuiRenderer = nullptr;
        #endif

        MaterialManager = nullptr;
        TextureManager = nullptr;
        GRenderContext->Deinitialize();
        Memory::Delete(GRenderContext);
        GRenderContext = nullptr;
    }

    void FRenderManager::Initialize()
    {
        GRenderContext = Memory::New<FVulkanRenderContext>();
        
        GRenderContext->Initialize(FRenderContextDesc{true, true});
        
        #if WITH_EDITOR
        ImGuiRenderer = Memory::New<FVulkanImGuiRender>();
        ImGuiRenderer->Initialize();
        #endif
        
        MaterialManager = MakeUnique<RHI::FMaterialManager>();
        TextureManager = MakeUnique<RHI::FTextureManager>();
    }

    void FRenderManager::FrameStart(const FUpdateContext& UpdateContext)
    {
        LUMINA_PROFILE_SCOPE();
        
        GRenderContext->FrameStart(UpdateContext, CurrentFrameIndex);

        #if WITH_EDITOR
        ImGuiRenderer->StartFrame(UpdateContext);
        #endif
    }

    void FRenderManager::FrameEnd(const FUpdateContext& UpdateContext, FRenderGraph& RenderGraph)
    {
        LUMINA_PROFILE_SCOPE();
        
        #if WITH_EDITOR
        ImGuiRenderer->EndFrame(UpdateContext, RenderGraph);
        #endif

        // Internally executes the render graph.
        GRenderContext->FrameEnd(UpdateContext, RenderGraph);
        
        GRenderContext->FlushPendingDeletes();
        
        CurrentFrameIndex = (CurrentFrameIndex + 1) % FRAMES_IN_FLIGHT;
    }

    void FRenderManager::SwapchainResized(glm::vec2 NewSize)
    {
        OnSwapchainResized.Broadcast(NewSize);
    }
}
