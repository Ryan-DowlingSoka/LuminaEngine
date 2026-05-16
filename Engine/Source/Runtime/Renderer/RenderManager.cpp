#include "pch.h"
#include "RenderManager.h"

#include "API/Vulkan/VulkanRenderContext.h"
#include "Tools/UI/ImGui/Vulkan/VulkanImGuiRender.h"

#include "RHIGlobals.h"
#include "CommandList.h"
#include "RenderThread.h"
#include "Core/Application/Application.h"
#include "Core/Console/ConsoleVariable.h"
#include "Core/Engine/Engine.h"
#include "Core/Profiler/Profile.h"
#include "GPUProfiler/GPUProfiler.h"
#include "Tools/UI/ImGui/ImGuiRenderer.h"
#include "UI/RmlUiBridge.h"
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

    // Boot-time only. When false the worker stays down and ENQUEUE_RENDER_COMMAND
    // runs inline on the caller -- effectively single-threaded rendering.
    static TConsoleVar CVarRenderThreadEnabled("Core.RenderThread.Enabled", true,
        "Run a dedicated render thread. Boot-time only; restart to apply changes.");


    FRenderManager::FRenderManager()
    {
    }

    FRenderManager::~FRenderManager()
    {
        // Stop the render thread first: any pending command on the queue holds
        // refs to GPU resources we're about to destroy below.
        if (GRenderThread)
        {
            GRenderThread->Stop();
            Memory::Delete(GRenderThread);
            GRenderThread = nullptr;
        }

        #if WITH_EDITOR
        ImGuiRenderer->Deinitialize();
        Memory::Delete(ImGuiRenderer);
        ImGuiRenderer = nullptr;
        #endif

        MaterialManager = nullptr;
        TextureManager = nullptr;

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

        GRenderThread = Memory::New<FRenderThread>();
        if (CVarRenderThreadEnabled.GetValue())
        {
            GRenderThread->Start();
        }

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

        #if WITH_EDITOR
        ImGuiRenderer->StartFrame(UpdateContext);
        #endif
    }

    void FRenderManager::FrameEnd()
    {
        LUMINA_PROFILE_SCOPE();

        TUniquePtr<FImDrawDataSnapshot> ImGuiSnapshot;
        #if WITH_EDITOR
        ImGuiSnapshot = ImGuiRenderer->BuildFrame_GameThread();
        #endif

        const uint8 ThisFrameIndex = CurrentFrameIndex;
        CurrentFrameIndex = (CurrentFrameIndex + 1) % FRAMES_IN_FLIGHT;

        ENQUEUE_RENDER_COMMAND(RenderFrame)([this, ThisFrameIndex, Snapshot = Move(ImGuiSnapshot)]() mutable
        {
            FGPUProfiler::Get().BeginFrame();
            GRenderContext->FrameStart(ThisFrameIndex);

            FRHICommandListRef CmdList = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
            CmdList->Open();
            ICommandList& CL = *CmdList;

            {
                GPU_PROFILE_SCOPE(&CL, "Frame");

                {
                    GPU_PROFILE_SCOPE_COLOR(&CL, "World Render", FColor(0.20f, 0.55f, 0.90f));
                    GWorldManager->RenderWorlds(CL);
                }

                RmlUi::RenderAll(CL);

                #if WITH_EDITOR
                if (Snapshot)
                {
                    ImGuiRenderer->RecordFrame_RenderThread(CL, *Snapshot);
                }
                #else
                // Game build: copy the primary world's RT directly to the
                // engine viewport (which the swap copies from in FrameEnd).
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
                                    CL.CopyImage(WorldRT, FTextureSlice(), ViewportRT, FTextureSlice());
                                }
                            }
                        }
                    }
                }
                #endif
            }

            GRenderContext->FrameEnd(CL);
            GRenderContext->WaitForGPU();
            FGPUProfiler::Get().EndFrame();
            GRenderContext->FlushPendingDeletes();
        });
    }

    void FRenderManager::SwapchainResized(glm::vec2 NewSize)
    {
        OnSwapchainResized.Broadcast(NewSize);
    }
}
