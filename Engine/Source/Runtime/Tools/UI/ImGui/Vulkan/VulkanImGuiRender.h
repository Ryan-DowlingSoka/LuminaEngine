#pragma once

#include "Memory/SmartPtr.h"
#include "Containers/Array.h"
#include "Core/Threading/Thread.h"
#include "Renderer/RenderResource.h"
#include "Renderer/RenderTypes.h"
#include "Tools/UI/ImGui/ImGuiRenderer.h"

struct ImDrawData;
struct ImTextureData;
struct ImGuiViewport;

namespace Lumina
{
    class FVulkanRenderContext;
    class FVulkanSwapchain;
    class FUpdateContext;

    // A real RHI ImGui backend. Textures are sampled straight out of the engine
    // bindless table (ImTextureID == FRHIImage::GetResourceID), so there are no
    // per-texture descriptor sets, no descriptor pool, and no per-frame texture
    // churn. The stock imgui_impl_vulkan renderer is gone; ImGui_ImplGlfw is
    // kept for platform/input. Draws are recorded directly onto our command
    // lists (main viewport on the render thread, secondary viewports on the
    // game thread), one DrawIndexed per ImDrawCmd with the bindless table bound
    // once and the texture id passed by push constant.
    class FVulkanImGuiRender : public IImGuiRenderer
    {
    public:

        void Initialize() override;
        void Deinitialize() override;

        void OnStartFrame(const FUpdateContext& UpdateContext) override;
        void OnEndFrame(ICommandList& CmdList, FImDrawDataSnapshot& Snapshot) override;
        void FillReferencedImagesSnapshot(TVector<FRHIImageRef>& Out) override;
        void ProcessTextureUpdates_GameThread() override;

        void CaptureViewports_GameThread(uint8 Slot) override;
        void RenderViewports_RenderThread(uint8 Slot) override;

        RUNTIME_API ImTextureID GetOrCreateImTexture(FStringView Path) override;
        RUNTIME_API ImTextureID GetOrCreateImTexture(FRHIImage* Image, const FTextureSubresourceSet& Subresources = AllSubresources) override;
        void DestroyImTexture(uint64 Hash) override;

    private:

        // Lazily create + cache a pipeline for a given color-attachment format.
        // The main viewport RT and the secondary-window swapchains have different
        // formats, so we key the pipeline by format.
        FRHIGraphicsPipeline* GetOrCreatePipeline(EFormat ColorFormat);

        // Record one ImDrawData into Target (shared by main + secondary viewports).
        // Caller has already transitioned Target to RenderTarget and any sampled
        // user images to ShaderResource, with automatic barriers committed.
        void RecordDrawData(ICommandList& CmdList, ImDrawData* DrawData, FRHIImage* Target);

        // Honor ImGui 1.92 dynamic-texture requests for the font atlas (the only
        // ImGui-owned texture). Create + SetTexID happen on the game thread (so
        // TexIDs are valid for the snapshot); the pixel upload is queued and
        // flushed on the render-thread frame list right before the draw, so the
        // upload and the sample are in one submit (robust, like FRmlUiRenderer).
        void CreateFontTexture(ImTextureData* Tex);
        void DestroyFontTexture(ImTextureData* Tex);
        void FlushFontUploads_RenderThread(ICommandList& CmdList);

        // Multi-viewport window-lifecycle hooks (installed into ImGuiPlatformIO),
        // all on the game thread. CreateWindow makes the swapchain (GLFW surface
        // creation is main-thread only); DestroyWindow defers swapchain teardown
        // to the render thread (which owns acquire/present). No Render/SwapBuffers
        // hooks: rendering + present happen in RenderViewports_RenderThread.
        static void OnRendererCreateWindow(ImGuiViewport* Viewport);
        static void OnRendererDestroyWindow(ImGuiViewport* Viewport);
        static void OnRendererSetWindowSize(ImGuiViewport* Viewport, ImVec2 Size);

        FVulkanRenderContext*       VulkanRenderContext = nullptr;

        FRHIInputLayoutRef          InputLayout;
        // Not owned: the engine bindless texture-table layout.
        FRHIBindingLayout*          BindlessLayout = nullptr;
        THashMap<uint32, FRHIGraphicsPipelineRef> PipelinesByFormat;

        // 1x1 white fallback for failed loads / invalid texture ids.
        FRHIImageRef                WhiteImage;
        int32                       WhiteTextureID = -1;

        // Path-loaded textures: keep the imported image alive and reuse its
        // bindless id across frames; swept when unused.
        struct FPathEntry
        {
            FRHIImageRef Image;
            int32        ResourceID = -1;
            uint64       LastUseFrame = 0;
        };
        THashMap<FName, FPathEntry> PathTextures;

        // Custom subresource SRVs (texture editor mip viewer). The default
        // ResourceID is the all-mips view, so a single mip needs its own slot.
        struct FSubresEntry
        {
            int32  ResourceID = -1;
            uint64 LastUseFrame = 0;
        };
        THashMap<uint64, FSubresEntry> SubresTextures;

        // Font / ImGui-owned dynamic textures, keyed by ImTextureData::UniqueID.
        struct FFontEntry
        {
            FRHIImageRef Image;
            int32        ResourceID = -1;
        };
        THashMap<int32, FFontEntry> FontTextures;

        // Pixel uploads captured on the game thread (we own the copy, decoupled
        // from ImGui's Pixels lifetime), drained on the render-thread frame list.
        struct FPendingFontUpload
        {
            FRHIImageRef   Image;
            uint32         Width = 0;
            uint32         Height = 0;
            TVector<uint8> Pixels;   // tightly packed RGBA8, Width*Height*4
        };
        TVector<FPendingFontUpload> PendingFontUploads;

        // --- Multi-viewport: capture on game thread, render+present on render thread ---

        // One ImDrawCmd, flattened with global vertex/index offsets and the clip
        // rect pre-projected to framebuffer pixels (so the render thread does no
        // ImGui math).
        struct FCapturedCmd
        {
            float  ClipMinX = 0, ClipMinY = 0, ClipMaxX = 0, ClipMaxY = 0;
            uint32 TextureID = 0;
            uint32 ElemCount = 0;
            uint32 IdxOffset = 0;
            int32  VtxOffset = 0;
        };

        // A secondary viewport's whole frame, deep-copied off ImGui's live data.
        struct FCapturedViewport
        {
            FVulkanSwapchain*       Swapchain = nullptr;   // not owned (lives in viewport RendererUserData)
            float                   ScaleX = 0, ScaleY = 0, TranslateX = 0, TranslateY = 0;
            float                   FbWidth = 0, FbHeight = 0;
            TVector<ImDrawVert>     Vertices;
            TVector<ImDrawIdx>      Indices;
            TVector<FCapturedCmd>   Cmds;
        };
        TVector<FCapturedViewport>  SecondaryCaptures[FRAMES_IN_FLIGHT];

        // Render+present one captured viewport (render thread).
        void RenderCapturedViewport(FCapturedViewport& Capture);

        mutable FRecursiveMutex     Mutex;

        // Images referenced by ImGui::Image() this frame; copied into the snapshot
        // so the render thread keeps them alive and barriers them to ShaderResource.
        TFixedVector<FRHIImageRef, 16> ReferencedImages;

        uint64                      LastCleanupFrame = 0;
    };
}
