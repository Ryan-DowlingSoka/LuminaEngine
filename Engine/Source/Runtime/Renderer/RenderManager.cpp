#include "pch.h"
#include "RenderManager.h"

#include "API/Vulkan/VulkanRenderContext.h"
#include "Tools/UI/ImGui/Vulkan/VulkanImGuiRender.h"

#include "RHIGlobals.h"
#include "CommandList.h"
#include "Core/Application/Application.h"
#include "Core/Console/ConsoleVariable.h"
#include "Core/Engine/Engine.h"
#include "Core/Profiler/Profile.h"
#include "GPUProfiler/GPUProfiler.h"
#include "Tools/UI/ImGui/ImGuiRenderer.h"
#include "World/World.h"
#include "World/WorldManager.h"
#include "World/Scene/RenderScene/RenderScene.h"

namespace Lumina
{
    TMulticastDelegate<void, glm::vec2> FRenderManager::OnSwapchainResized;
    RUNTIME_API FRenderManager* GRenderManager = nullptr;
    
    static TConsoleVar CVarMaxFrameRate("Core.VSync", true, "Toggles v-sync", [](const CVarValueType& Value)
    {
        if (GRenderContext)
        {
            GRenderContext->SetVSyncEnabled(eastl::get<bool>(Value));
        }
    });


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

        // Drop GPU profiler resources before the render context goes away.
        FGPUProfiler::Get().Shutdown();

        GRenderContext->Deinitialize();
        Memory::Delete(GRenderContext);
        GRenderContext = nullptr;
    }

    void FRenderManager::Initialize()
    {
        GRenderContext = Memory::New<FVulkanRenderContext>();
        
        #if LUMINA_SHIPPING
        GRenderContext->Initialize(FRenderContextDesc{false, false});
        #else
        GRenderContext->Initialize(FRenderContextDesc{true, true});
        #endif
        
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

        FGPUProfiler::Get().BeginFrame();

        GRenderContext->FrameStart(UpdateContext, CurrentFrameIndex);

        #if WITH_EDITOR
        ImGuiRenderer->StartFrame(UpdateContext);
        #endif
    }

    void FRenderManager::FrameEnd(const FUpdateContext& UpdateContext, ICommandList& CmdList)
    {
        LUMINA_PROFILE_SCOPE();

        #if WITH_EDITOR
        ImGuiRenderer->EndFrame(UpdateContext, CmdList);
        #else
        
        if (FWorldContext* Ctx = GWorldManager->GetPrimaryGameContext())
        {
            if (CWorld* World = Ctx->World.Get())
            {
                if (IRenderScene* Scene = World->GetRenderer())
                {
                    if (FRHIImage* WorldRT = Scene->GetRenderTarget())
                    {
                        if (FRHIImage* ViewportRT = FEngine::GetEngineViewport()->GetRenderTarget())
                        {
                            CmdList.CopyImage(WorldRT, FTextureSlice(), ViewportRT, FTextureSlice());
                        }
                    }
                }
            }
        }
        #endif
        
        // Records the swapchain copy, closes, executes, and presents.
        GRenderContext->FrameEnd(UpdateContext, CmdList);

        FGPUProfiler::Get().EndFrame();

        GRenderContext->FlushPendingDeletes();

        CurrentFrameIndex = (CurrentFrameIndex + 1) % FRAMES_IN_FLIGHT;
    }

    void FRenderManager::SwapchainResized(glm::vec2 NewSize)
    {
        OnSwapchainResized.Broadcast(NewSize);
    }
}
