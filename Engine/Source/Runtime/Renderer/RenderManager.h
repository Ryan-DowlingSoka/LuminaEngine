#pragma once
#include "MaterialManager.h"
#include "RenderResource.h"
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
    // Immutable GPU resources shared by every render scene: the BRDF LUT, the SMAA
    // lookup tables, and (editor) the billboard icon textures. These are identical
    // across scenes and were previously re-baked / re-loaded from disk on every
    // FForwardRenderScene::Init(). Built once on the first scene and aliased by ref
    // thereafter. Released in ~FRenderManager before the device is torn down.
    struct FSharedRenderResources
    {
        FRHIImageRef    BRDFLut;
        FRHIImageRef    SMAAArea;
        FRHIImageRef    SMAASearch;

        #if WITH_EDITOR
        // PointLight, DirectionalLight, SkyLight, SpotLight, Camera, Character, ParticleSystem.
        FRHIImageRef    EditorIcons[7];
        #endif

        bool            bInitialized = false;

        void Reset() { *this = FSharedRenderResources{}; }
    };

    class FRenderManager
    {
    public:

        static TMulticastDelegate<void, glm::vec2> OnSwapchainResized;

        FRenderManager();
        ~FRenderManager();

        void Initialize();

        // Game thread: ImGui::NewFrame (and any other backend per-frame init).
        void FrameStart(const FUpdateContext& UpdateContext);

        // Game thread: snapshot ImGui DrawData and enqueue the render-thread
        // pipeline (one cmdlist with world + RmlUi + ImGui composite).
        // RmlUi::TickAll() must have already run on the game thread; the bridge's
        // FState mutex guards DOM + world list against next-frame TickAll.
        void FrameEnd();

        void SwapchainResized(glm::vec2 NewSize);


        #if WITH_EDITOR
        IImGuiRenderer* GetImGuiRenderer() const { return ImGuiRenderer; }
        #endif

        uint32 GetCurrentFrameIndex() const { return CurrentFrameIndex; }

        NODISCARD RHI::FTextureManager& GetTextureManager() const { return *TextureManager.get(); }
        NODISCARD RHI::FTextureManager* TryGetTextureManager() const { return TextureManager.get(); }
        NODISCARD RHI::FMaterialManager& GetMaterialManager() const { return *MaterialManager.get(); }

        // Lazily populated by the first render scene; aliased by all later scenes.
        NODISCARD FSharedRenderResources& GetSharedRenderResources() { return SharedRenderResources; }

    private:

        #if WITH_EDITOR
        IImGuiRenderer*                     ImGuiRenderer = nullptr;
        #endif

        TUniquePtr<RHI::FTextureManager>    TextureManager;
        TUniquePtr<RHI::FMaterialManager>   MaterialManager;

        FSharedRenderResources              SharedRenderResources;

        uint8                               CurrentFrameIndex = 0;
    };


    RUNTIME_API extern FRenderManager* GRenderManager;
}
