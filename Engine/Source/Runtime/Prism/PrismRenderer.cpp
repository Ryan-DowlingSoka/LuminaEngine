#include "PCH.h"
#include "PrismRenderer.h"
#include "PrismFontAtlas.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderManager.h"
#include "Renderer/RHIGlobals.h"
#include "Renderer/TextureManager.h"
#include "Renderer/RenderTypes.h"
#include "Renderer/CommandList.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/RenderGraph/RenderGraphPass.h"

namespace Lumina::Prism
{
    namespace
    {
        struct FPrismPushConstants
        {
            glm::vec2 InvScreenSize;
            glm::vec2 _Pad;
        };

        // A small floor so we don't do a round-trip through the allocator
        // for the first few frames while the batch is still stabilizing.
        constexpr uint32 kInitialVertexCapacity = 1024;
        constexpr uint32 kInitialIndexCapacity  = 1536;
    }

    void FPrismRenderer::BeginFrame(const glm::vec2& WindowSize)
    {
        CachedWindowSize = WindowSize;
        VertexStream.Reset();
    }

    void FPrismRenderer::Submit(const FPrismDrawList& DrawList)
    {
        // The default atlas lazily uploads on first access. Once it's
        // ready the tessellator emits real textured glyph quads; before
        // that, text draws fall back to colored placeholder boxes so
        // the first frame still produces sensible output.
        FPrismFontAtlas& Atlas = FPrismFontAtlas::GetDefault();
        Tessellator.Tessellate(DrawList, VertexStream, &Atlas);
    }

    void FPrismRenderer::EndFrame()
    {
    }

    void FPrismRenderer::EnsureInputLayout()
    {
        if (InputLayout)
        {
            return;
        }

        FVertexAttributeDesc Attrs[6];

        Attrs[0].Format = EFormat::RG32_FLOAT;
        Attrs[0].SetElementStride(sizeof(FPrismVertex));
        Attrs[0].SetOffset(offsetof(FPrismVertex, Position));

        Attrs[1].Format = EFormat::RG32_FLOAT;
        Attrs[1].SetElementStride(sizeof(FPrismVertex));
        Attrs[1].SetOffset(offsetof(FPrismVertex, UV));

        Attrs[2].Format = EFormat::RGBA32_FLOAT;
        Attrs[2].SetElementStride(sizeof(FPrismVertex));
        Attrs[2].SetOffset(offsetof(FPrismVertex, Color));

        Attrs[3].Format = EFormat::RGBA32_FLOAT;
        Attrs[3].SetElementStride(sizeof(FPrismVertex));
        Attrs[3].SetOffset(offsetof(FPrismVertex, ClipRect));

        Attrs[4].Format = EFormat::RG32_FLOAT;
        Attrs[4].SetElementStride(sizeof(FPrismVertex));
        Attrs[4].SetOffset(offsetof(FPrismVertex, LocalSize));

        Attrs[5].Format = EFormat::RGBA32_FLOAT;
        Attrs[5].SetElementStride(sizeof(FPrismVertex));
        Attrs[5].SetOffset(offsetof(FPrismVertex, Params));

        InputLayout = GRenderContext->CreateInputLayout(Attrs, eastl::size(Attrs));
    }

    void FPrismRenderer::EnsureBuffers(uint32 VertexCount, uint32 IndexCount)
    {
        if (VertexCount > VertexCapacity || !VertexBuffer)
        {
            // Overshoot so we don't thrash when the batch size drifts.
            const uint32 NewCap = glm::max(VertexCapacity * 2u, glm::max(VertexCount, kInitialVertexCapacity));
            FRHIBufferDesc Desc;
            Desc.DebugName         = "Prism.Vertices";
            Desc.Size              = (uint64)NewCap * sizeof(FPrismVertex);
            Desc.Stride            = sizeof(FPrismVertex);
            Desc.Usage              .SetFlag(BUF_VertexBuffer);
            Desc.InitialState      = EResourceStates::CopyDest;
            Desc.bKeepInitialState = true;
            VertexBuffer   = GRenderContext->CreateBuffer(Desc);
            VertexCapacity = NewCap;
        }

        if (IndexCount > IndexCapacity || !IndexBuffer)
        {
            const uint32 NewCap = glm::max(IndexCapacity * 2u, glm::max(IndexCount, kInitialIndexCapacity));
            FRHIBufferDesc Desc;
            Desc.DebugName         = "Prism.Indices";
            Desc.Size              = (uint64)NewCap * sizeof(uint32);
            Desc.Stride            = sizeof(uint32);
            Desc.Usage              .SetFlag(BUF_IndexBuffer);
            Desc.InitialState      = EResourceStates::CopyDest;
            Desc.bKeepInitialState = true;
            
            IndexBuffer   = GRenderContext->CreateBuffer(Desc);
            IndexCapacity = NewCap;
        }
    }

    void FPrismRenderer::AddPassToRenderGraph(FRenderGraph& RenderGraph, FRHIImage* Target)
    {
        if (!Target || VertexStream.Indices.empty() || CachedWindowSize.x <= 0.0f || CachedWindowSize.y <= 0.0f)
        {
            return;
        }

        EnsureInputLayout();
        EnsureBuffers((uint32)VertexStream.Vertices.size(), (uint32)VertexStream.Indices.size());

        FRGPassDescriptor* Descriptor = RenderGraph.AllocDescriptor();
        RenderGraph.AddPass(RG_Raster, "Prism UI", Descriptor, [this, Target](ICommandList& CmdList)
        {
            LUMINA_PROFILE_SECTION_COLORED("Prism UI", tracy::Color::MediumPurple);

            const uint32 VertexCount = (uint32)VertexStream.Vertices.size();
            const uint32 IndexCount  = (uint32)VertexStream.Indices.size();

            CmdList.WriteBuffer(VertexBuffer.GetReference(), VertexStream.Vertices.data(), VertexCount * sizeof(FPrismVertex));
            CmdList.WriteBuffer(IndexBuffer.GetReference(),  VertexStream.Indices.data(),  IndexCount  * sizeof(uint32));

            FRHIVertexShaderRef VertexShader = FShaderLibrary::GetVertexShader("PrismVert.slang");
            FRHIPixelShaderRef  PixelShader  = FShaderLibrary::GetPixelShader ("PrismPixel.slang");
            if (!VertexShader || !PixelShader)
            {
                return;
            }

            FRenderPassDesc::FAttachment Attachment; Attachment
                .SetImage(Target)
                .SetLoadOp(ERenderLoadOp::Load);

            FRenderPassDesc RenderPass; RenderPass
                .AddColorAttachment(Attachment)
                .SetRenderArea(Target->GetExtent());

            // Premultiplied alpha, the tessellator folds alpha into the
            // RGB channels so the blend is (One, OneMinusSrcAlpha).
            FBlendState BlendState; BlendState
                .Targets[0]
                    .SetBlendEnable(true)
                    .SetSrcBlend(EBlendFactor::One)
                    .SetDestBlend(EBlendFactor::OneMinusSrcAlpha)
                    .SetBlendOp(EBlendOp::Add)
                    .SetSrcBlendAlpha(EBlendFactor::One)
                    .SetDestBlendAlpha(EBlendFactor::OneMinusSrcAlpha)
                    .SetBlendOpAlpha(EBlendOp::Add);

            FRasterState RasterState; RasterState
                .SetCullNone();

            FDepthStencilState DepthState; DepthState
                .DisableDepthTest()
                .DisableDepthWrite();

            FRenderState RenderState; RenderState
                .SetRasterState(RasterState)
                .SetDepthStencilState(DepthState)
                .SetBlendState(BlendState);

            FGraphicsPipelineDesc PipelineDesc; PipelineDesc
                .SetDebugName("Prism UI")
                .SetPrimType(EPrimitiveType::TriangleList)
                .SetRenderState(RenderState)
                .SetInputLayout(InputLayout)
                .SetVertexShader(VertexShader)
                .SetPixelShader(PixelShader)
                .AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());

            FVertexBufferBinding VBinding; VBinding
                .SetBuffer(VertexBuffer.GetReference())
                .SetSlot(0)
                .SetOffset(0);

            FIndexBufferBinding IBinding; IBinding
                .SetBuffer(IndexBuffer.GetReference())
                .SetFormat(EFormat::R32_UINT)
                .SetOffset(0);

            const float SizeX = (float)Target->GetSizeX();
            const float SizeY = (float)Target->GetSizeY();
            FViewportState Viewport;
            Viewport.Viewports.emplace_back(FViewport(SizeX, SizeY));
            Viewport.Scissors.emplace_back(FRect((int)SizeX, (int)SizeY));

            FGraphicsState GraphicsState; GraphicsState
                .SetRenderPass(RenderPass)
                .SetViewportState(Viewport)
                .SetPipeline(GRenderContext->CreateGraphicsPipeline(PipelineDesc, RenderPass))
                .AddVertexBuffer(VBinding)
                .SetIndexBuffer(IBinding)
                .AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());

            CmdList.SetGraphicsState(GraphicsState);

            FPrismPushConstants Push{};
            Push.InvScreenSize = glm::vec2(1.0f / CachedWindowSize.x, 1.0f / CachedWindowSize.y);
            CmdList.SetPushConstants(&Push, sizeof(Push));

            CmdList.DrawIndexed(IndexCount, 1, 0, 0, 0);
        });
    }
}
