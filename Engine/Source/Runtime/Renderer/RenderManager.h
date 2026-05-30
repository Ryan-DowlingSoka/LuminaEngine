#pragma once
#include "MaterialManager.h"
#include "RenderResource.h"
#include "RenderTypes.h"
#include "TextureManager.h"
#include "Core/Delegates/Delegate.h"


namespace Lumina
{
    class ICommandList;
    class IImGuiRenderer;
    class IRenderContext;
    class FRHICommandList;
    class FUpdateContext;
}

namespace Lumina
{
    // Immutable GPU resources shared by every render scene (BRDF LUT, SMAA LUTs, editor icons).
    // Built once on the first scene, aliased by ref after; released in ~FRenderManager pre-teardown.
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

        static TMulticastDelegate<void, FVector2> OnSwapchainResized;

        FRenderManager();
        ~FRenderManager();

        void Initialize();

        // Game thread: ImGui::NewFrame (and any other backend per-frame init).
        void FrameStart(const FUpdateContext& UpdateContext);

        // Game thread: snapshot ImGui DrawData and enqueue the render-thread pipeline (world + RmlUi
        // + ImGui composite). Per-world UI must already have ticked (CWorld::Extract).
        void FrameEnd();

        void SwapchainResized(FVector2 NewSize);


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
