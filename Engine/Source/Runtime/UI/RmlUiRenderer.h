#pragma once

// Rml::RenderInterface on the RHI. Frame: BeginFrame -> Context::Render (defers draws) -> EndFrame (uploads + replay).
// Draws are deferred so texture uploads can run outside the render pass.

#include "Containers/Array.h"
#include "Containers/String.h"
#include "Core/Delegates/Delegate.h"
#include "Renderer/RHIFwd.h"
#include "Renderer/RenderResource.h"

#include <atomic>

#include <RmlUi/Core/RenderInterface.h>

namespace Lumina
{
    class FRmlUiRenderer final : public Rml::RenderInterface
    {
    public:
        FRmlUiRenderer();
        ~FRmlUiRenderer() override;
        FRmlUiRenderer(const FRmlUiRenderer&) = delete;
        FRmlUiRenderer& operator = (const FRmlUiRenderer&) = delete;

        bool Initialize();
        void Shutdown();

        // LogicalSize=0 mirrors ViewportSize; nonzero decouples projection (layout pixels) from RT pixels.
        void BeginFrame(ICommandList& CmdList, FRHIImage* Target, const glm::uvec2& ViewportSize, const glm::uvec2& LogicalSize = glm::uvec2(0));
        void EndFrame();

        Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> Vertices, Rml::Span<const int> Indices) override;
        void                        RenderGeometry(Rml::CompiledGeometryHandle Geometry, Rml::Vector2f Translation, Rml::TextureHandle Texture) override;
        void                        ReleaseGeometry(Rml::CompiledGeometryHandle Geometry) override;

        Rml::TextureHandle          LoadTexture(Rml::Vector2i& OutDimensions, const Rml::String& Source) override;
        Rml::TextureHandle          GenerateTexture(Rml::Span<const Rml::byte> Bytes, Rml::Vector2i Dimensions) override;
        void                        ReleaseTexture(Rml::TextureHandle Texture) override;

        void                        EnableScissorRegion(bool bEnable) override;
        void                        SetScissorRegion(Rml::Rectanglei Region) override;

        void                        SetTransform(const Rml::Matrix4f* Transform) override;

    private:
        // RmlUi geometry is upload-grade: small, GPU-touched once per frame, then
        // potentially re-uploaded on relayout. We keep CPU-side bytes here and
        // suballocate from the cmdlist's transient ring at draw time -- no
        // vmaCreateBuffer / vmaDestroyBuffer per Compile/Release pair.
        struct FGeometry
        {
            TVector<uint8> VertexData;
            TVector<uint8> IndexData;
            uint32         IndexCount = 0;
        };

        struct FTexture
        {
            FRHIImageRef               Image;
            int32                      ResourceID = -1;        // bindless slot (FRHIImage::GetResourceID)
            class CTexture*            AssetKeepalive = nullptr;   // rooted while held; released on ReleaseTexture
            class CMaterialInterface*  BrushMaterial  = nullptr;   // UI-material brush; rooted while held, rendered each frame
            glm::uvec2                 BrushSize = {0, 0};
            FString                    BrushSourcePath;            // resolved asset path; re-validated so a rename/delete breaks the brush
            bool                       bBrushStale = false;        // source path no longer resolves -> cleared + not rendered (material stays rooted so a rename-back can resume)
        };

        struct FPendingTexture
        {
            Rml::TextureHandle Handle = 0;
            int                Width = 0;
            int                Height = 0;
            TVector<uint8>     Bytes;
        };

        struct FDrawCall
        {
            Rml::CompiledGeometryHandle Geometry = 0;
            Rml::TextureHandle          Texture = 0;
            glm::vec2                   Translation = {0.0f, 0.0f};
            glm::mat4                   MVP = glm::mat4(1.0f);
            bool                        bScissorEnabled = false;
            Rml::Rectanglei             Scissor;
        };

        // GPU vertex for the batched path: RmlUi's pos/color/uv plus the index
        // of this vertex's draw in the per-draw buffer. Matches RmlUiVert.slang's
        // input layout (stride 24).
        struct FUiVertex
        {
            float  Position[2];
            uint32 Colour;        // premultiplied RGBA8
            float  UV[2];
            uint32 DrawIndex;
        };

        // Per-draw data read in-shader via device address (std430). Matches
        // RmlUiCommon.slang::FUiDraw.
        struct FUiDraw
        {
            glm::mat4 MVP;
            glm::vec4 ClipRect;     // minX, minY, maxX, maxY (framebuffer pixels)
            uint32    TextureID;
            uint32    SamplerIndex;
            uint32    Pad0;
            uint32    Pad1;
        };

        bool                        CreatePipeline();
        Rml::TextureHandle          RegisterTexturePending(TVector<uint8>&& Bytes, int Width, int Height);
        Rml::TextureHandle          LoadMaterialBrush(Rml::Vector2i& OutDimensions, class CMaterialInterface* Material, const FStringView& SourcePath, uint32 Width, uint32 Height);
        Rml::TextureHandle          LoadTextureAsset(Rml::Vector2i& OutDimensions, class CTexture* Texture);
        void                        UploadPendingTextures();
        void                        RenderMaterialBrushes();
        void                        RevalidateBrushes(ICommandList& CmdList);
        bool                        EnsureMaterialBufferSet();

        FRHIInputLayoutRef          InputLayout;
        FRHIGraphicsPipelineRef     Pipeline;
        FRHIImageRef                DefaultWhiteImage;
        int32                       DefaultWhiteResourceID = -1;

        // UI-material brush rendering (UI material -> offscreen RT, sampled bindless).
        FRHIBindingLayoutRef        MaterialBufferLayout;   // set 0, binding 5 (material uniforms SRV)
        FRHIBindingSetRef           MaterialBufferSet;
        FRHIBuffer*                 MaterialBufferCached = nullptr;


        std::atomic<bool>           bBrushRevalidatePending{false};
        FDelegateHandle             AssetRegistryUpdateHandle;

        THashMap<Rml::CompiledGeometryHandle, FGeometry>    Geometries;
        THashMap<Rml::TextureHandle, FTexture>              Textures;
        Rml::CompiledGeometryHandle                         NextGeometryHandle = 1;
        Rml::TextureHandle                                  NextTextureHandle = 1;

        TVector<FPendingTexture>    PendingTextureUploads;
        TVector<FDrawCall>          DrawCalls;


        TVector<FUiVertex>          BatchVertices;
        TVector<uint32>             BatchIndices;
        TVector<FUiDraw>            BatchDrawData;

        ICommandList*               CurrentCmdList = nullptr;
        FRHIImage*                  CurrentTarget = nullptr;
        glm::uvec2                  CurrentSize = {0, 0};
        FRenderPassDesc             CurrentPassDesc;
        bool                        bRenderPassOpen = false;
        bool                        bInitialized = false;

        glm::mat4                   ProjectionMatrix = glm::mat4(1.0f);
        glm::mat4                   UserTransform = glm::mat4(1.0f);
        glm::mat4                   CachedMVP = glm::mat4(1.0f);
        bool                        bScissorEnabled = false;
        Rml::Rectanglei             CurrentScissor;
    };
}
