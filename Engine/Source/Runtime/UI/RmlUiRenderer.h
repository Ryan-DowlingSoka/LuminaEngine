#pragma once

// Rml::RenderInterface on the RHI. Frame: BeginFrame -> Context::Render (defers draws) -> EndFrame (uploads + replay).
// Draws are deferred so texture uploads can run outside the render pass.

#include "Containers/Array.h"
#include "Renderer/RHIFwd.h"
#include "Renderer/RenderResource.h"

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
            FRHIImageRef       Image;
            FRHIBindingSetRef  BindingSet;
            class CTexture*    AssetKeepalive = nullptr;   // rooted while held; released on ReleaseTexture
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

        bool                        CreatePipeline();
        Rml::TextureHandle          RegisterTexturePending(TVector<uint8>&& Bytes, int Width, int Height);
        void                        UploadPendingTextures();
        void                        IssueDrawCall(FDrawCall& Draw);

        FRHIInputLayoutRef          InputLayout;
        FRHIBindingLayoutRef        BindingLayout;
        FRHIGraphicsPipelineRef     Pipeline;
        FRHISamplerRef              Sampler;
        FRHIImageRef                DefaultWhiteImage;
        FRHIBindingSetRef           DefaultWhiteBindingSet;

        THashMap<Rml::CompiledGeometryHandle, FGeometry>    Geometries;
        THashMap<Rml::TextureHandle, FTexture>              Textures;
        Rml::CompiledGeometryHandle                         NextGeometryHandle = 1;
        Rml::TextureHandle                                  NextTextureHandle = 1;

        TVector<FPendingTexture>    PendingTextureUploads;
        TVector<FDrawCall>          DrawCalls;

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
