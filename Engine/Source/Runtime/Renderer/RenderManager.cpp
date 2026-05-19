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

    FRenderManager::FRenderManager()
    {
    }

    FRenderManager::~FRenderManager()
    {
        // Stop the worker first: queued commands hold refs to GPU resources destroyed below.
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
        GRenderContext->Initialize(FRenderContextDesc{false, true});
        #endif

        GRenderThread = Memory::New<FRenderThread>();
        GRenderThread->Start();

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

    void FRenderManager::FrameEnd(FRHICommandListRef RmlUiCmdList)
    {
        LUMINA_PROFILE_SCOPE();

        const uint8 ThisFrameIndex = CurrentFrameIndex;
        CurrentFrameIndex = (CurrentFrameIndex + 1) % FRAMES_IN_FLIGHT;

        FImDrawDataSnapshot* ImGuiSnapshot = nullptr;
        #if WITH_EDITOR
        ImGuiSnapshot = ImGuiRenderer->BuildFrame_GameThread(ThisFrameIndex);
        #endif

        ENQUEUE_RENDER_COMMAND(RenderFrame)([this, ThisFrameIndex, Snapshot = ImGuiSnapshot, RmlUi = Move(RmlUiCmdList)]() mutable
        {
            FGPUProfiler::Get().BeginFrame();
            GRenderContext->FrameStart(ThisFrameIndex);

            // World render goes into its own cmdlist so the pre-recorded RmlUi
            // cmdlist (built on the game thread) can execute between world and
            // ImGui without breaking GPU ordering. Single graphics queue =
            // submission order is execution order.
            FRHICommandListRef WorldCmdList = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
            WorldCmdList->Open();
            {
                GPU_PROFILE_SCOPE_COLOR(WorldCmdList.GetReference(), "World Render", FColor(0.20f, 0.55f, 0.90f));
                GWorldManager->RenderWorlds(*WorldCmdList, ThisFrameIndex);
            }
            WorldCmdList->Close();
            GRenderContext->ExecuteCommandList(WorldCmdList);

            // RmlUi (recorded on the game thread, references the same world RTs).
            // Submitted between world render and final composite so editor panels
            // sample world+RmlUi.
            if (RmlUi)
            {
                GRenderContext->ExecuteCommandList(RmlUi);
            }

            // Final cmdlist: ImGui composite (or game-build swap copy) + the
            // FrameEnd path that does AcquireNextImage + engine-viewport-to-
            // swapchain copy + submit + present.
            FRHICommandListRef FinalCmdList = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
            FinalCmdList->Open();
            ICommandList& CL = *FinalCmdList;

            {
                GPU_PROFILE_SCOPE(&CL, "Frame Composite");

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

            // Last per-slot reader (ImGui composite via Snapshot) has finished.
            // Release the scene's frame slot back to the game thread so it can
            // overwrite FrameRing[Slot] for frame N+FRAMES_IN_FLIGHT.
            GWorldManager->SignalFrameConsumed(ThisFrameIndex);
        });
    }

    void FRenderManager::SwapchainResized(glm::vec2 NewSize)
    {
        OnSwapchainResized.Broadcast(NewSize);
    }
}
