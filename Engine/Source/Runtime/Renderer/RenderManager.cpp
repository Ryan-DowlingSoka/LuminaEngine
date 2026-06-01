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
    TMulticastDelegate<void, FVector2> FRenderManager::OnSwapchainResized;
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

        // Drop the shared LUT / icon refs while the device is still alive; member
        // teardown would otherwise run after GRenderContext is deleted below.
        SharedRenderResources.Reset();

        FGPUProfiler::Get().Shutdown();

        GRenderContext->Deinitialize();
        Memory::Delete(GRenderContext);
        GRenderContext = nullptr;
    }

    void FRenderManager::Initialize()
    {
        GRenderContext = Memory::New<FVulkanRenderContext>();

        #if defined(LUMINA_WITH_VALIDATION)
        constexpr bool bValidation = true;
        #else
        constexpr bool bValidation = false;
        #endif

        #if LUMINA_SHIPPING
        constexpr bool bDebugUtils = false;
        #else
        constexpr bool bDebugUtils = true;
        #endif

        GRenderContext->Initialize(FRenderContextDesc{ bValidation, bDebugUtils });

        GRenderThread = Memory::New<FRenderThread>();
        GRenderThread->Start();

        MaterialManager = MakeUnique<RHI::FMaterialManager>();
        TextureManager = MakeUnique<RHI::FTextureManager>();

        #if WITH_EDITOR
        ImGuiRenderer = Memory::New<FVulkanImGuiRender>();
        ImGuiRenderer->Initialize();
        #endif
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

        const uint8 ThisFrameIndex = CurrentFrameIndex;
        CurrentFrameIndex = (CurrentFrameIndex + 1) % FRAMES_IN_FLIGHT;

        [[maybe_unused]] FImDrawDataSnapshot* ImGuiSnapshot = nullptr;
        #if WITH_EDITOR
        ImGuiSnapshot = ImGuiRenderer->BuildFrame_GameThread(ThisFrameIndex);
        #endif

        ENQUEUE_RENDER_COMMAND(RenderFrame)([this, ThisFrameIndex, Snapshot = ImGuiSnapshot]() mutable
        {
            GRenderContext->WaitForGPU();
            GRenderContext->FlushPendingDeletes();

            FGPUProfiler::Get().BeginFrame();
            GRenderContext->FrameStart(ThisFrameIndex);
            
            FRHICommandListRef CmdList = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
            CmdList->Open();
            ICommandList& CL = *CmdList;

            {
                GPU_PROFILE_SCOPE_COLOR(&CL, "World Render", FColor(0.20f, 0.55f, 0.90f));
                GWorldManager->RenderWorlds(CL, ThisFrameIndex);
            }

            {
                GPU_PROFILE_SCOPE_COLOR(&CL, "RmlUi", FColor(0.95f, 0.55f, 0.20f));
                // Per-world UI rendered inside RenderWorlds (CWorld::Render); only editor contexts remain here.
                RmlUi::RenderEditorContexts(CL);
            }

            {
                GPU_PROFILE_SCOPE(&CL, "Frame Composite");
                #if USING(WITH_EDITOR)
                if (Snapshot)
                {
                    ImGuiRenderer->RecordFrame_RenderThread(CL, *Snapshot, ThisFrameIndex);
                    ImGuiRenderer->SignalSnapshotSlotConsumed(ThisFrameIndex);
                }
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
                                    CL.CopyImage(WorldRT, FTextureSlice(), ViewportRT, FTextureSlice());
                                }
                            }
                        }
                    }
                }
                #endif
            }

            GWorldManager->SignalFrameConsumed(ThisFrameIndex);

            GRenderContext->FrameEnd(CL);
            FGPUProfiler::Get().EndFrame();
        });
    }

    void FRenderManager::SwapchainResized(FVector2 NewSize)
    {
        OnSwapchainResized.Broadcast(NewSize);
    }
}
