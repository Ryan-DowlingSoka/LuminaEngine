#include "pch.h"
#include "RmlUiRenderer.h"

#include "Core/Engine/Engine.h"
#include "Log/Log.h"
#include "Renderer/CommandList.h"
#include "Renderer/Format.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderResource.h"
#include "Renderer/RenderTypes.h"
#include "Renderer/RHIGlobals.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Vertex.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Assets/AssetTypes/Textures/Texture.h"
#include "Core/Object/ObjectCore.h"
#include "FileSystem/FileSystem.h"

namespace Lumina
{
    // Must match RmlUiVert.slang.
    struct FRmlUiPushConstants
    {
        glm::mat4 MVP;
        glm::vec2 Translation;
        glm::vec2 _Pad;
    };
    static_assert(sizeof(FRmlUiPushConstants) == 80, "Push-constant size must match shader layout exactly.");
    static_assert(sizeof(FRmlUiPushConstants) <= MaxPushConstantSize, "Push-constants exceed RHI cap.");

    // Vertex layout: pos(8) colour(4) uv(8).
    static_assert(sizeof(Rml::Vertex) == 20, "Rml::Vertex layout drifted; renderer input layout must be updated.");

    FRmlUiRenderer::FRmlUiRenderer() = default;
    FRmlUiRenderer::~FRmlUiRenderer() { Shutdown(); }

    bool FRmlUiRenderer::Initialize()
    {
        if (bInitialized)
        {
            return true;
        }
        if (GRenderContext == nullptr)
        {
            LOG_ERROR("[RmlUi] Initialize called before GRenderContext was alive.");
            return false;
        }

        if (!CreatePipeline())
        {
            return false;
        }

        // 1x1 white fallback for untextured geometry; uploaded on first BeginFrame.
        constexpr uint8 WhitePixel[4] = {255, 255, 255, 255};
        TVector<uint8> Bytes;
        Bytes.assign(WhitePixel, WhitePixel + 4);
        const Rml::TextureHandle DefaultHandle = RegisterTexturePending(Move(Bytes), 1, 1);
        if (DefaultHandle != 0)
        {
            DefaultWhiteImage      = Textures[DefaultHandle].Image;
            DefaultWhiteBindingSet = Textures[DefaultHandle].BindingSet;
        }

        bInitialized = true;
        return true;
    }

    void FRmlUiRenderer::Shutdown()
    {
        if (!bInitialized)
        {
            return;
        }

        DrawCalls.clear();
        PendingTextureUploads.clear();
        Geometries.clear();
        for (auto& KV : Textures)
        {
            if (KV.second.AssetKeepalive != nullptr)
            {
                KV.second.AssetKeepalive->RemoveFromRoot();
                KV.second.AssetKeepalive = nullptr;
            }
        }
        Textures.clear();
        DefaultWhiteImage.SafeRelease();
        DefaultWhiteBindingSet.SafeRelease();
        Pipeline.SafeRelease();
        BindingLayout.SafeRelease();
        InputLayout.SafeRelease();
        Sampler.SafeRelease();
        bInitialized = false;
    }

    bool FRmlUiRenderer::CreatePipeline()
    {
        FVertexAttributeDesc Attribs[3];
        Attribs[0].Format       = EFormat::RG32_FLOAT;
        Attribs[0].BufferIndex  = 0;
        Attribs[0].Offset       = 0;
        Attribs[0].ElementStride= sizeof(Rml::Vertex);
        Attribs[1].Format       = EFormat::RGBA8_UNORM;
        Attribs[1].BufferIndex  = 0;
        Attribs[1].Offset       = 8;
        Attribs[1].ElementStride= sizeof(Rml::Vertex);
        Attribs[2].Format       = EFormat::RG32_FLOAT;
        Attribs[2].BufferIndex  = 0;
        Attribs[2].Offset       = 12;
        Attribs[2].ElementStride= sizeof(Rml::Vertex);
        InputLayout = GRenderContext->CreateInputLayout(Attribs, 3);

        FBindingLayoutDesc LayoutDesc;
        LayoutDesc.SetDebugName("RmlUiBindings")
                  .SetBindingIndex(0)
                  .SetVisibility(ERHIShaderType::Fragment)
                  .AddItem(FBindingLayoutItem::Texture_SRV(0));
        BindingLayout = GRenderContext->CreateBindingLayout(LayoutDesc);

        // Bilinear + clamp; UV wrap is never wanted.
        FSamplerDesc SamplerInfo;
        SamplerInfo.DebugName = "RmlUiSampler";
        SamplerInfo.SetAllFilters(true).SetAllAddressModes(ESamplerAddressMode::Clamp);
        Sampler = GRenderContext->CreateSampler(SamplerInfo);

        FRHIVertexShaderRef VS = FShaderLibrary::GetVertexShader(FName("RmlUiVert"));
        FRHIPixelShaderRef  PS = FShaderLibrary::GetPixelShader(FName("RmlUiPixel"));
        if (!VS || !PS)
        {
            LOG_ERROR("[RmlUi] RmlUiVert.slang / RmlUiPixel.slang not found in shader library.");
            return false;
        }

        // Premultiplied alpha: RmlUi pre-multiplies vertex colors.
        FBlendState BlendState;
        BlendState.Targets[0].EnableBlend()
            .SetSrcBlend(EBlendFactor::One)
            .SetDestBlend(EBlendFactor::OneMinusSrcAlpha)
            .SetBlendOp(EBlendOp::Add)
            .SetSrcBlendAlpha(EBlendFactor::One)
            .SetDestBlendAlpha(EBlendFactor::OneMinusSrcAlpha)
            .SetBlendOpAlpha(EBlendOp::Add);

        FDepthStencilState DepthState;
        DepthState.DisableDepthTest().DisableDepthWrite().DisableStencil();

        // No culling; RmlUi doesn't normalize winding.
        FRasterState RasterState;
        RasterState.SetCullNone().EnableScissor();

        FRenderState RenderState;
        RenderState.SetBlendState(BlendState)
                   .SetDepthStencilState(DepthState)
                   .SetRasterState(RasterState);

        // Pipeline format-bound to the engine viewport; world RTs share that format today.
        FRHIImage* TargetImage = FEngine::GetEngineViewport()->GetRenderTarget();
        if (TargetImage == nullptr)
        {
            LOG_ERROR("[RmlUi] Engine viewport has no render target yet.");
            return false;
        }
        FRenderPassDesc PassDesc;
        FRenderPassDesc::FAttachment Attachment;
        Attachment.SetImage(TargetImage)
                  .SetFormat(TargetImage->GetFormat())
                  .SetLoadOp(ERenderLoadOp::Load);
        PassDesc.AddColorAttachment(Attachment).SetRenderArea(TargetImage->GetExtent());

        FGraphicsPipelineDesc PipelineDesc;
        PipelineDesc.SetDebugName("RmlUiPipeline")
                    .SetPrimType(EPrimitiveType::TriangleList)
                    .SetInputLayout(InputLayout)
                    .SetVertexShader(VS)
                    .SetPixelShader(PS)
                    .SetRenderState(RenderState)
                    .AddBindingLayout(BindingLayout);

        Pipeline = GRenderContext->CreateGraphicsPipeline(PipelineDesc, PassDesc);
        if (!Pipeline)
        {
            LOG_ERROR("[RmlUi] Failed to create graphics pipeline.");
            return false;
        }

        return true;
    }

    void FRmlUiRenderer::BeginFrame(ICommandList& CmdList, FRHIImage* Target, const glm::uvec2& ViewportSize, const glm::uvec2& LogicalSize)
    {
        CurrentCmdList   = &CmdList;
        CurrentTarget    = Target;
        CurrentSize      = ViewportSize;
        bRenderPassOpen  = false;

        DrawCalls.clear();

        // Logical size drives the projection (so layout pixels span the full RT regardless of
        // its aspect); ViewportSize drives the RHI viewport / scissor.
        const glm::uvec2 ProjSize = (LogicalSize.x > 0 && LogicalSize.y > 0) ? LogicalSize : ViewportSize;

        // pixel -> NDC ortho; no Y-flip since Vulkan viewport is +Y-down.
        const float W = ProjSize.x > 0 ? float(ProjSize.x) : 1.0f;
        const float H = ProjSize.y > 0 ? float(ProjSize.y) : 1.0f;
        ProjectionMatrix = glm::mat4(
            2.0f / W,  0.0f,       0.0f,  0.0f,
            0.0f,      2.0f / H,   0.0f,  0.0f,
            0.0f,      0.0f,       1.0f,  0.0f,
           -1.0f,     -1.0f,       0.0f,  1.0f);

        UserTransform = glm::mat4(1.0f);
        CachedMVP     = ProjectionMatrix;
        bScissorEnabled = false;

        // Cache pass desc so SetGraphicsState avoids Begin/End churn across draws.
        CurrentPassDesc = FRenderPassDesc{};
        if (Target != nullptr)
        {
            FRenderPassDesc::FAttachment Attachment;
            Attachment.SetImage(Target)
                      .SetLoadOp(ERenderLoadOp::Load)
                      .SetStoreOp(ERenderStoreOp::Store);
            CurrentPassDesc.AddColorAttachment(Attachment).SetRenderArea(ViewportSize);
        }
    }

    void FRmlUiRenderer::EndFrame()
    {
        if (CurrentCmdList == nullptr)
        {
            return;
        }
        ICommandList& CmdList = *CurrentCmdList;

        // Texture uploads must be outside a render pass.
        UploadPendingTextures();

        if (!DrawCalls.empty() && CurrentTarget != nullptr)
        {
            CmdList.SetImageState(CurrentTarget, AllSubresources, EResourceStates::RenderTarget);
            CmdList.CommitBarriers();

            CmdList.AddMarker("RmlUi", FColor(0.95f, 0.55f, 0.20f));

            for (FDrawCall& Draw : DrawCalls)
            {
                IssueDrawCall(Draw);
            }

            CmdList.EndRenderPass();
            bRenderPassOpen = false;
        }

        DrawCalls.clear();
        CurrentCmdList = nullptr;
        CurrentTarget  = nullptr;
    }

    void FRmlUiRenderer::IssueDrawCall(FDrawCall& Draw)
    {
        ICommandList& CmdList = *CurrentCmdList;

        auto GeomIt = Geometries.find(Draw.Geometry);
        if (GeomIt == Geometries.end())
        {
            return;
        }
        const FGeometry& Geom = GeomIt->second;
        if (!Geom.VertexBuffer || !Geom.IndexBuffer || Geom.IndexCount == 0)
        {
            return;
        }

        FRHIBindingSet* BindingSet = DefaultWhiteBindingSet.GetReference();
        if (Draw.Texture != 0)
        {
            auto TexIt = Textures.find(Draw.Texture);
            if (TexIt != Textures.end() && TexIt->second.BindingSet)
            {
                BindingSet = TexIt->second.BindingSet.GetReference();
            }
        }
        if (BindingSet == nullptr)
        {
            return;
        }

        FGraphicsState State;
        State.SetPipeline(Pipeline);
        State.SetRenderPass(CurrentPassDesc);

        State.AddBindingSet(BindingSet);
        State.SetVertexBuffer(FVertexBufferBinding{}.SetBuffer(Geom.VertexBuffer).SetSlot(0).SetOffset(0));
        State.SetIndexBuffer(FIndexBufferBinding{}.SetBuffer(Geom.IndexBuffer).SetFormat(EFormat::R32_UINT).SetOffset(0));

        State.AddViewport(FViewport(0.0f, float(CurrentSize.x), 0.0f, float(CurrentSize.y), 0.0f, 1.0f));

        if (Draw.bScissorEnabled)
        {
            State.AddScissor(FRect(Draw.Scissor.Position().x, Draw.Scissor.Position().x + Draw.Scissor.Width(),
                                   Draw.Scissor.Position().y, Draw.Scissor.Position().y + Draw.Scissor.Height()));
        }
        else
        {
            State.AddScissor(FRect(0, int(CurrentSize.x), 0, int(CurrentSize.y)));
        }

        CmdList.SetGraphicsState(State);

        FRmlUiPushConstants PC;
        PC.MVP         = Draw.MVP;
        PC.Translation = Draw.Translation;
        PC._Pad        = glm::vec2(0.0f);
        CmdList.SetPushConstants(&PC, sizeof(PC));

        CmdList.DrawIndexed(Geom.IndexCount, 1, 0, 0, 0);
    }

    Rml::CompiledGeometryHandle FRmlUiRenderer::CompileGeometry(Rml::Span<const Rml::Vertex> Vertices, Rml::Span<const int> Indices)
    {
        if (CurrentCmdList == nullptr)
        {
            // RmlUi can compile outside our frame (e.g. font-atlas re-bake).
            return 0;
        }

        const size_t VBSize = Vertices.size() * sizeof(Rml::Vertex);
        const size_t IBSize = Indices.size()  * sizeof(int);
        if (VBSize == 0 || IBSize == 0)
        {
            return 0;
        }

        // bKeepInitialState pins the resting state so auto-barriers stay correct across frames.
        FRHIBufferDesc VBDesc;
        VBDesc.Size = VBSize;
        VBDesc.Stride = sizeof(Rml::Vertex);
        VBDesc.DebugName = "RmlUiVB";
        VBDesc.Usage.SetMultipleFlags(EBufferUsageFlags::VertexBuffer);
        VBDesc.InitialState = EResourceStates::VertexBuffer;
        VBDesc.bKeepInitialState = true;

        FRHIBufferDesc IBDesc;
        IBDesc.Size = IBSize;
        IBDesc.Stride = sizeof(int);
        IBDesc.DebugName = "RmlUiIB";
        IBDesc.Usage.SetMultipleFlags(EBufferUsageFlags::IndexBuffer);
        IBDesc.InitialState = EResourceStates::IndexBuffer;
        IBDesc.bKeepInitialState = true;

        FRHIBufferRef VB = GRenderContext->CreateBuffer(CurrentCmdList, Vertices.data(), VBDesc);
        FRHIBufferRef IB = GRenderContext->CreateBuffer(CurrentCmdList, Indices.data(),  IBDesc);

        if (!VB || !IB)
        {
            return 0;
        }

        FGeometry Geom;
        Geom.VertexBuffer = Move(VB);
        Geom.IndexBuffer  = Move(IB);
        Geom.IndexCount   = uint32(Indices.size());

        const Rml::CompiledGeometryHandle Handle = NextGeometryHandle++;
        Geometries.emplace(Handle, Move(Geom));
        return Handle;
    }

    void FRmlUiRenderer::RenderGeometry(Rml::CompiledGeometryHandle Geometry, Rml::Vector2f Translation, Rml::TextureHandle Texture)
    {
        FDrawCall Draw;
        Draw.Geometry        = Geometry;
        Draw.Texture         = Texture;
        Draw.Translation     = glm::vec2(Translation.x, Translation.y);
        Draw.MVP             = CachedMVP;
        Draw.bScissorEnabled = bScissorEnabled;
        Draw.Scissor         = CurrentScissor;
        DrawCalls.push_back(Move(Draw));
    }

    void FRmlUiRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle Geometry)
    {
        Geometries.erase(Geometry);
    }

    Rml::TextureHandle FRmlUiRenderer::LoadTexture(Rml::Vector2i& OutDimensions, const Rml::String& Source)
    {
        // All paths in the engine are virtual; resolve the source as a CTexture asset
        // and reuse its already-uploaded RHIImage. No re-decode, no re-upload.
        const FStringView SourceView(Source.c_str(), Source.size());
        CTexture* Texture = LoadObject<CTexture>(SourceView);
        if (Texture == nullptr)
        {
            LOG_WARN("[RmlUi] LoadTexture: no CTexture asset at '{}'.", Source.c_str());
            return 0;
        }

        FRHIImage* RawImage = Texture->GetRHIRef();
        if (RawImage == nullptr)
        {
            LOG_WARN("[RmlUi] LoadTexture: '{}' has no RHI image (asset not yet uploaded?).", Source.c_str());
            return 0;
        }

        const FRHIImageDesc& ImgDesc = Texture->GetTextureResource().ImageDescription;
        if (ImgDesc.Extent.x == 0 || ImgDesc.Extent.y == 0)
        {
            LOG_WARN("[RmlUi] LoadTexture: '{}' has zero extent.", Source.c_str());
            return 0;
        }

        FBindingSetDesc SetDesc;
        SetDesc.AddItem(FBindingSetItem::TextureSRV(0, RawImage, Sampler));
        FRHIBindingSetRef BindingSet = GRenderContext->CreateBindingSet(SetDesc, BindingLayout);
        if (!BindingSet)
        {
            LOG_WARN("[RmlUi] LoadTexture: failed to create binding set for '{}'.", Source.c_str());
            return 0;
        }

        // Pin the asset for the lifetime of the RmlUi handle so an unload doesn't
        // dangle our binding set. Released in ReleaseTexture.
        Texture->AddToRoot();

        const Rml::TextureHandle Handle = NextTextureHandle++;
        FTexture Tex;
        Tex.Image           = FRHIImageRef(RawImage);
        Tex.BindingSet      = Move(BindingSet);
        Tex.AssetKeepalive  = Texture;
        Textures.emplace(Handle, Move(Tex));

        OutDimensions.x = int(ImgDesc.Extent.x);
        OutDimensions.y = int(ImgDesc.Extent.y);
        return Handle;
    }

    Rml::TextureHandle FRmlUiRenderer::GenerateTexture(Rml::Span<const Rml::byte> Bytes, Rml::Vector2i Dimensions)
    {
        TVector<uint8> Copy;
        Copy.assign(Bytes.data(), Bytes.data() + Bytes.size());
        return RegisterTexturePending(Move(Copy), Dimensions.x, Dimensions.y);
    }

    Rml::TextureHandle FRmlUiRenderer::RegisterTexturePending(TVector<uint8>&& Bytes, int Width, int Height)
    {
        if (Width <= 0 || Height <= 0 || Bytes.size() < size_t(Width) * size_t(Height) * 4)
        {
            return 0;
        }

        // Resting state ShaderResource; bKeepInitialState pins across frames.
        FRHIImageDesc Desc;
        Desc.Extent      = glm::uvec2(uint32(Width), uint32(Height));
        Desc.Format      = EFormat::RGBA8_UNORM;
        Desc.Dimension   = EImageDimension::Texture2D;
        Desc.NumMips     = 1;
        Desc.ArraySize   = 1;
        Desc.NumSamples  = 1;
        Desc.DebugName   = "RmlUiTexture";
        Desc.InitialState = EResourceStates::ShaderResource;
        Desc.bKeepInitialState = true;
        Desc.Flags.SetMultipleFlags(EImageCreateFlags::ShaderResource);

        FRHIImageRef Image = GRenderContext->CreateImage(Desc);
        if (!Image)
        {
            return 0;
        }

        FBindingSetDesc SetDesc;
        SetDesc.AddItem(FBindingSetItem::TextureSRV(0, Image, Sampler));
        FRHIBindingSetRef BindingSet = GRenderContext->CreateBindingSet(SetDesc, BindingLayout);
        if (!BindingSet)
        {
            return 0;
        }

        const Rml::TextureHandle Handle = NextTextureHandle++;
        FTexture Tex;
        Tex.Image      = Image;
        Tex.BindingSet = BindingSet;
        Textures.emplace(Handle, Move(Tex));

        FPendingTexture Pending;
        Pending.Handle = Handle;
        Pending.Width  = Width;
        Pending.Height = Height;
        Pending.Bytes  = Move(Bytes);
        PendingTextureUploads.push_back(Move(Pending));
        return Handle;
    }

    void FRmlUiRenderer::UploadPendingTextures()
    {
        if (PendingTextureUploads.empty() || CurrentCmdList == nullptr)
        {
            return;
        }
        ICommandList& CmdList = *CurrentCmdList;

        for (FPendingTexture& Pending : PendingTextureUploads)
        {
            auto It = Textures.find(Pending.Handle);
            if (It == Textures.end() || !It->second.Image)
            {
                continue;
            }
            FRHIImage* Image = It->second.Image.GetReference();
            CmdList.WriteImage(Image, 0, 0, Pending.Bytes.data(), uint32(Pending.Width) * 4, 0);
        }

        PendingTextureUploads.clear();
    }

    void FRmlUiRenderer::ReleaseTexture(Rml::TextureHandle Texture)
    {
        auto It = Textures.find(Texture);
        if (It != Textures.end())
        {
            if (It->second.AssetKeepalive != nullptr)
            {
                It->second.AssetKeepalive->RemoveFromRoot();
                It->second.AssetKeepalive = nullptr;
            }
            Textures.erase(It);
        }
    }

    void FRmlUiRenderer::EnableScissorRegion(bool bEnable)
    {
        bScissorEnabled = bEnable;
    }

    void FRmlUiRenderer::SetScissorRegion(Rml::Rectanglei Region)
    {
        CurrentScissor = Region;
    }

    void FRmlUiRenderer::SetTransform(const Rml::Matrix4f* Transform)
    {
        if (Transform)
        {
            // RmlUi defaults column-major (matches glm); RMLUI_MATRIX_ROW_MAJOR would require a transpose.
            std::memcpy(glm::value_ptr(UserTransform), Transform->data(), sizeof(float) * 16);
        }
        else
        {
            UserTransform = glm::mat4(1.0f);
        }
        CachedMVP = ProjectionMatrix * UserTransform;
    }
}
