#include "pch.h"
#include "RenderManager.h"

#include "Tools/UI/ImGui/Vulkan/VulkanImGuiRender.h"

#include "RenderThread.h"
#include "ShaderCompiler.h"
#include "ShaderLibrary.h"
#include "RHI.h"
#include "RHICore.h"
#include "Core/Application/Application.h"
#include "Core/Console/ConsoleVariable.h"
#include "Core/Engine/Engine.h"
#include "Core/Windows/Window.h"
#include "Core/Profiler/Profile.h"
#include "Tools/UI/ImGui/ImGuiRenderer.h"
#include "UI/RmlUiBridge.h"
#include "World/World.h"
#include "World/WorldManager.h"
#include "World/Scene/RenderScene/RenderScene.h"

namespace Lumina
{
    TMulticastDelegate<void, FVector2> FRenderManager::OnSwapchainResized;
    RUNTIME_API FRenderManager* GRenderManager = nullptr;

    static TConsoleVar CVarVSync("Core.VSync", true, "Toggles v-sync", [](const CVarValueType& Value)
    {
        // Render thread recreates the swapchain with the new present mode.
        const bool bEnabled = eastl::get<bool>(Value);
        if (GRenderManager != nullptr)
        {
            ENQUEUE_RENDER_COMMAND(SetVSync)([bEnabled]
            {
                RHI::SetVSync(bEnabled);
                GRenderManager->RecreatePrimarySwapchain();
            });
        }
        else
        {
            RHI::SetVSync(bEnabled);
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

        // Release the shared LUT / icon heap slots while the device is still alive; member
        // teardown would otherwise run after the device is freed below.
        if (SharedRenderResources.bInitialized)
        {
            if (SharedRenderResources.BRDFLutUAV != RHI::kInvalidHeapSlot)
            {
                RHI::HeapFreeRWTexture(RHI::Core::GetGlobalHeap(), SharedRenderResources.BRDFLutUAV);
            }
            RHI::Textures::Release(SharedRenderResources.BRDFLut);
            RHI::Textures::Release(SharedRenderResources.SMAAArea);
            RHI::Textures::Release(SharedRenderResources.SMAASearch);
            #if WITH_EDITOR
            for (RHI::FManagedTexture& Icon : SharedRenderResources.EditorIcons)
            {
                RHI::Textures::Release(Icon);
            }
            #endif
        }
        SharedRenderResources.Reset();

        GShaderCompiler = nullptr;
        if (ShaderCompiler != nullptr)
        {
            ShaderCompiler->Shutdown();
            Memory::Delete(ShaderCompiler);
            ShaderCompiler = nullptr;
        }
        GShaderLibrary = nullptr;
        if (ShaderLibrary != nullptr)
        {
            Memory::Delete(ShaderLibrary);
            ShaderLibrary = nullptr;
        }

        RHI::FreeH(Swapchain);
        RHI::Core::Shutdown();
        RHI::FreeDevice();
    }

    void FRenderManager::Initialize()
    {
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

        RHI::CreateDevice(RHI::FDeviceDesc{ bValidation, bDebugUtils });
        RHI::Core::Initialize();

        ShaderLibrary = Memory::New<FShaderLibrary>();
        GShaderLibrary = ShaderLibrary;
        ShaderCompiler = Memory::New<FSpirVShaderCompiler>();
        GShaderCompiler = ShaderCompiler;
        ShaderCompiler->Initialize();

        FWindow* Window = Windowing::GetPrimaryWindowHandle();
        Swapchain = RHI::CreateSwapchain(Window->GetWindow(), Window->GetExtent());

        GRenderThread = Memory::New<FRenderThread>();
        GRenderThread->Start();

        MaterialManager = MakeUnique<RHI::FMaterialManager>();

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
        CurrentFrameIndex = (CurrentFrameIndex + 1) % RHI::kFramesInFlight;

        [[maybe_unused]] FImDrawDataSnapshot* ImGuiSnapshot = nullptr;
        #if WITH_EDITOR
        ImGuiSnapshot = ImGuiRenderer->BuildFrame_GameThread(ThisFrameIndex);
        #endif

        ENQUEUE_RENDER_COMMAND(RenderFrame)([this, ThisFrameIndex, Snapshot = ImGuiSnapshot]() mutable
        {
            // Everything up to each scene's slot release (inside RenderWorlds) is what the game
            // thread's WaitForSlotConsumed actually waits on; keep these stages attributed.
            {
                LUMINA_PROFILE_SECTION_COLORED("RT Frame Fence (GPU)", tracy::Color::Crimson);
                RHI::Core::BeginFrame(ThisFrameIndex);
            }

            // RenderWorlds signals each scene's slot exactly once (after its recording); a second
            // blanket signal here would race a freshly started Extract and corrupt frame data.
            GWorldManager->RenderWorlds_NewRHI(ThisFrameIndex);

            RHI::FTextureH SwapImage;
            {
                LUMINA_PROFILE_SECTION_COLORED("RT Acquire Swapchain", tracy::Color::Orange3);
                SwapImage = RHI::AcquireNextImage(Swapchain);
            }
            if (!RHI::IsValid(SwapImage))
            {
                RHI::RecreateSwapchain(Swapchain, Windowing::GetPrimaryWindowHandle()->GetExtent());
                #if WITH_EDITOR
                if (Snapshot)
                {
                    ImGuiRenderer->SignalSnapshotSlotConsumed(ThisFrameIndex);
                }
                #endif
                return;
            }

            const FUIntVector2 Extent = RHI::GetSwapchainExtent(Swapchain);

            RHI::FCmdListH CL = RHI::OpenCommandList();
            RHI::CmdSetTextureHeap(CL, RHI::Core::GetGlobalHeap());
            RHI::CmdSwapchainBarrierToRender(CL, Swapchain);

            #if WITH_EDITOR
            // Editor RmlUi previews rasterize before ImGui samples their RTs below.
            {
                LUMINA_PROFILE_SECTION_COLORED("RT Editor UI", tracy::Color::SlateBlue1);
                RmlUi::RenderEditorContexts(CL);
            }
            #endif

            #if WITH_EDITOR
            if (Snapshot)
            {
                LUMINA_PROFILE_SECTION_COLORED("RT ImGui Record", tracy::Color::SlateBlue3);
                ImGuiRenderer->OnEndFrame_NewRHI(CL, SwapImage, Extent, *Snapshot);
                ImGuiRenderer->SignalSnapshotSlotConsumed(ThisFrameIndex);
            }
            #endif

            {
                LUMINA_PROFILE_SECTION_COLORED("RT Present", tracy::Color::Orange4);
                RHI::Core::Present(Swapchain, CL);
            }
        });
    }

    void FRenderManager::SwapchainResized(FVector2 NewSize)
    {
        OnSwapchainResized.Broadcast(NewSize);
    }

    void FRenderManager::RecreatePrimarySwapchain()
    {
        RHI::RecreateSwapchain(Swapchain, Windowing::GetPrimaryWindowHandle()->GetExtent());
    }
}
