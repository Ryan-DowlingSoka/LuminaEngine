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

#include <glm/gtc/type_ptr.hpp>
#include <RmlUi/Core.h>
#include <RmlUi/Core/Vertex.h>

#include "Assets/AssetTypes/Textures/Texture.h"
#include "Assets/AssetTypes/Material/MaterialInterface.h"
#include "Assets/AssetRegistry/AssetRegistry.h"
#include "Assets/AssetRegistry/AssetData.h"
#include "Core/Object/Cast.h"
#include "Core/Object/ObjectCore.h"
#include "FileSystem/FileSystem.h"
#include "Renderer/GPUProfiler/GPUProfiler.h"
#include "Renderer/RenderManager.h"
#include "Renderer/TextureManager.h"
#include "Renderer/MaterialManager.h"

#include <chrono>

namespace Lumina
{
    // Matches RmlUiCommon.slang::FRmlUiPushConstants -- the device address of the
    // per-draw data buffer (a BDA pointer). Everything else is read from there.
    struct FRmlUiPushConstants
    {
        uint64 DrawsAddress;
    };
    static_assert(sizeof(FRmlUiPushConstants) == 8, "Push-constant must be a single device address.");
    static_assert(sizeof(FRmlUiPushConstants) <= MaxPushConstantSize, "Push-constants exceed RHI cap.");

    // 32 B push block for the UI material brush pass. Mirrors
    // UIMaterialGlobals.slang::FUIMaterialPushConstants.
    struct FUIMaterialBrushPush
    {
        glm::uvec4 ScreenSize;   // .xy = brush resolution
        float      Time;
        uint32     MaterialIndex;
        glm::uvec2 _Pad;
    };
    static_assert(sizeof(FUIMaterialBrushPush) == 32, "Must match the UIPixelPass.slang push block.");

    // RmlUi wants bilinear + clamp; stock bindless sampler slot 1.
    static constexpr uint32 GRmlUiSamplerIndex = (uint32)RHI::EBindlessSampler::LinearClamp;

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
            DefaultWhiteResourceID = Textures[DefaultHandle].ResourceID;
        }

        // Brushes cache (and root) their material; a rename/delete would otherwise
        // leave the document rendering the stale asset. Flag a re-validation on any
        // registry change instead of polling every frame.
        AssetRegistryUpdateHandle = FAssetRegistry::Get().GetOnAssetRegistryUpdated().AddLambda([this]()
        {
            bBrushRevalidatePending.store(true, std::memory_order_release);
        });

        bInitialized = true;
        return true;
    }

    void FRmlUiRenderer::Shutdown()
    {
        if (!bInitialized)
        {
            return;
        }

        FAssetRegistry::Get().GetOnAssetRegistryUpdated().Remove(AssetRegistryUpdateHandle);

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
            if (KV.second.BrushMaterial != nullptr)
            {
                KV.second.BrushMaterial->RemoveFromRoot();
                KV.second.BrushMaterial = nullptr;
            }
        }
        Textures.clear();
        DefaultWhiteImage.SafeRelease();
        MaterialBufferSet.SafeRelease();
        MaterialBufferLayout.SafeRelease();
        MaterialBufferCached = nullptr;
        Pipeline.SafeRelease();
        InputLayout.SafeRelease();
        bInitialized = false;
    }

    bool FRmlUiRenderer::CreatePipeline()
    {
        static_assert(sizeof(FUiVertex) == 24, "FUiVertex must match RmlUiVert.slang input layout (stride 24).");
        static_assert(sizeof(FUiDraw) == 96,   "FUiDraw must match RmlUiCommon.slang::FUiDraw (std430).");

        // Matches FUiVertex (stride 24): pos(8) colour(4) uv(8) drawIndex(4).
        FVertexAttributeDesc Attribs[4];
        Attribs[0].Format       = EFormat::RG32_FLOAT;     // POSITION
        Attribs[0].BufferIndex  = 0;
        Attribs[0].Offset       = 0;
        Attribs[0].ElementStride= sizeof(FUiVertex);
        Attribs[1].Format       = EFormat::RGBA8_UNORM;    // COLOR
        Attribs[1].BufferIndex  = 0;
        Attribs[1].Offset       = 8;
        Attribs[1].ElementStride= sizeof(FUiVertex);
        Attribs[2].Format       = EFormat::RG32_FLOAT;     // TEXCOORD0
        Attribs[2].BufferIndex  = 0;
        Attribs[2].Offset       = 12;
        Attribs[2].ElementStride= sizeof(FUiVertex);
        Attribs[3].Format       = EFormat::R32_UINT;       // TEXCOORD1 (draw index)
        Attribs[3].BufferIndex  = 0;
        Attribs[3].Offset       = 20;
        Attribs[3].ElementStride= sizeof(FUiVertex);
        InputLayout = GRenderContext->CreateInputLayout(Attribs, 4);

        // Textures are sampled bindless through the engine texture table (set 0
        // here): no per-texture descriptor set, draws pass a resource id by push
        // constant. Layout/table must be alive (CreateImage already needs it).
        if (GRenderManager == nullptr || GRenderManager->GetTextureManager().GetLayout() == nullptr)
        {
            LOG_ERROR("[RmlUi] Texture manager bindless layout not available.");
            return false;
        }
        FRHIBindingLayout* BindlessLayout = GRenderManager->GetTextureManager().GetLayout();

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
                    .AddBindingLayout(BindlessLayout);

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
        GPU_PROFILE_SCOPE(CurrentCmdList, "RmlUI");

        // Texture uploads must be outside a render pass.
        UploadPendingTextures();

        // UI materials evaluate into their brush RTs before the UI samples them.
        // Also outside the UI render pass (each brush opens its own pass).
        RenderMaterialBrushes();

        if (DrawCalls.empty() || CurrentTarget == nullptr)
        {
            DrawCalls.clear();
            CurrentCmdList = nullptr;
            CurrentTarget  = nullptr;
            return;
        }

        LUMINA_PROFILE_SCOPE();
        
        BatchVertices.clear();
        BatchIndices.clear();
        BatchDrawData.clear();

        const float FullW = float(CurrentSize.x);
        const float FullH = float(CurrentSize.y);

        for (const FDrawCall& Draw : DrawCalls)
        {
            auto GeomIt = Geometries.find(Draw.Geometry);
            if (GeomIt == Geometries.end())
            {
                continue;
            }
            const FGeometry& Geom = GeomIt->second;
            if (Geom.VertexData.empty() || Geom.IndexData.empty() || Geom.IndexCount == 0)
            {
                continue;
            }

            // Bindless: untextured geometry samples the 1x1 white default's slot.
            int32 ResourceID = DefaultWhiteResourceID;
            if (Draw.Texture != 0)
            {
                auto TexIt = Textures.find(Draw.Texture);
                if (TexIt != Textures.end() && TexIt->second.ResourceID >= 0)
                {
                    ResourceID = TexIt->second.ResourceID;
                }
            }
            if (ResourceID < 0)
            {
                continue;
            }

            // Per-draw entry; this draw's vertices reference it by index.
            const uint32 DrawIndex = uint32(BatchDrawData.size());
            FUiDraw DD;
            DD.MVP      = Draw.MVP;
            DD.ClipRect = Draw.bScissorEnabled
                ? glm::vec4(float(Draw.Scissor.Position().x), float(Draw.Scissor.Position().y),
                            float(Draw.Scissor.Position().x + Draw.Scissor.Width()),
                            float(Draw.Scissor.Position().y + Draw.Scissor.Height()))
                : glm::vec4(0.0f, 0.0f, FullW, FullH);
            DD.TextureID    = uint32(ResourceID);
            DD.SamplerIndex = GRmlUiSamplerIndex;
            DD.Pad0 = 0;
            DD.Pad1 = 0;
            BatchDrawData.push_back(DD);

            const Rml::Vertex* SrcVerts = reinterpret_cast<const Rml::Vertex*>(Geom.VertexData.data());
            const uint32 VtxCount = uint32(Geom.VertexData.size() / sizeof(Rml::Vertex));
            const int*   SrcInds  = reinterpret_cast<const int*>(Geom.IndexData.data());

            const uint32 BaseVertex = uint32(BatchVertices.size());

            // Convert to the GPU vertex: bake translation, tag with the draw index.
            BatchVertices.reserve(BatchVertices.size() + VtxCount);
            for (uint32 v = 0; v < VtxCount; ++v)
            {
                const Rml::Vertex& Src = SrcVerts[v];
                FUiVertex V;
                V.Position[0] = Src.position.x + Draw.Translation.x;
                V.Position[1] = Src.position.y + Draw.Translation.y;
                Memory::Memcpy(&V.Colour, &Src.colour, sizeof(uint32));
                V.UV[0]     = Src.tex_coord.x;
                V.UV[1]     = Src.tex_coord.y;
                V.DrawIndex = DrawIndex;
                BatchVertices.push_back(V);
            }

            // Rebase indices so we draw with VertexOffset = 0.
            BatchIndices.reserve(BatchIndices.size() + Geom.IndexCount);
            for (uint32 i = 0; i < Geom.IndexCount; ++i)
            {
                BatchIndices.push_back(uint32(SrcInds[i]) + BaseVertex);
            }
        }

        if (!BatchIndices.empty() && !BatchDrawData.empty())
        {
            CmdList.SetImageState(CurrentTarget, AllSubresources, EResourceStates::RenderTarget);
            CmdList.CommitBarriers();

            // Three transient allocations for the whole frame: vertices, indices,
            // and the per-draw data (read in-shader via its device address).
            const size_t VBytes = BatchVertices.size() * sizeof(FUiVertex);
            const size_t IBytes = BatchIndices.size()  * sizeof(uint32);
            const size_t DBytes = BatchDrawData.size() * sizeof(FUiDraw);
            FTransientAlloc VBAlloc = CmdList.AllocateTransient(VBytes, alignof(FUiVertex));
            FTransientAlloc IBAlloc = CmdList.AllocateTransient(IBytes, sizeof(uint32));
            FTransientAlloc DBAlloc = CmdList.AllocateTransient(DBytes, 16);
            if (VBAlloc.Buffer != nullptr && IBAlloc.Buffer != nullptr && DBAlloc.Buffer != nullptr)
            {
                Memory::Memcpy(VBAlloc.Cpu, BatchVertices.data(), VBytes);
                Memory::Memcpy(IBAlloc.Cpu, BatchIndices.data(),  IBytes);
                Memory::Memcpy(DBAlloc.Cpu, BatchDrawData.data(), DBytes);

                // One graphics state for the whole UI: pipeline, bindless table,
                // the shared VB/IB, full viewport + scissor. Clipping is per-draw
                // in the shader (ClipRect), so no dynamic scissor is needed.
                FGraphicsState State;
                State.SetPipeline(Pipeline);
                State.SetRenderPass(CurrentPassDesc);
                State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
                State.SetVertexBuffer(FVertexBufferBinding{}.SetBuffer(VBAlloc.Buffer).SetSlot(0).SetOffset(VBAlloc.Offset));
                State.SetIndexBuffer(FIndexBufferBinding{}.SetBuffer(IBAlloc.Buffer).SetFormat(EFormat::R32_UINT).SetOffset(IBAlloc.Offset));
                State.AddViewport(FViewport(0.0f, FullW, 0.0f, FullH, 0.0f, 1.0f));
                State.AddScissor(FRect(0, int(CurrentSize.x), 0, int(CurrentSize.y)));
                CmdList.SetGraphicsState(State);
                
                FRmlUiPushConstants PC;
                PC.DrawsAddress = DBAlloc.Gpu;
                CmdList.SetPushConstants(&PC, sizeof(PC));

                CmdList.DrawIndexed(uint32(BatchIndices.size()), 1, 0, 0, 0);
            }

            CmdList.EndRenderPass();
            bRenderPassOpen = false;
        }

        DrawCalls.clear();
        CurrentCmdList = nullptr;
        CurrentTarget  = nullptr;
    }

    Rml::CompiledGeometryHandle FRmlUiRenderer::CompileGeometry(Rml::Span<const Rml::Vertex> Vertices, Rml::Span<const int> Indices)
    {
        const size_t VBSize = Vertices.size() * sizeof(Rml::Vertex);
        const size_t IBSize = Indices.size()  * sizeof(int);
        if (VBSize == 0 || IBSize == 0)
        {
            return 0;
        }

        // CPU-side cache only. At EndFrame all referenced geometry is concatenated
        // into one transient VB/IB (translation baked in).
        FGeometry Geom;
        const uint8* VBytes = reinterpret_cast<const uint8*>(Vertices.data());
        const uint8* IBytes = reinterpret_cast<const uint8*>(Indices.data());
        Geom.VertexData.assign(VBytes, VBytes + VBSize);
        Geom.IndexData.assign(IBytes, IBytes + IBSize);
        Geom.IndexCount = uint32(Indices.size());

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
        Rml::String Path = Source;
        if (Path.rfind("material:", 0) == 0)
        {
            Path = Path.substr(std::strlen("material:"));
        }

        // Optional `?w=NNN&h=NNN` brush-resolution override (materials only).
        uint32 Width = 256, Height = 256;
        const size_t QueryPos = Path.find('?');
        if (QueryPos != Rml::String::npos)
        {
            const Rml::String Query = Path.substr(QueryPos + 1);
            Path = Path.substr(0, QueryPos);
            auto ReadParam = [&](const char* Key, uint32& Out)
            {
                const Rml::String Token = Rml::String(Key) + "=";
                const size_t At = Query.find(Token);
                if (At != Rml::String::npos)
                {
                    const int Parsed = std::atoi(Query.c_str() + At + Token.size());
                    if (Parsed > 0) Out = (uint32)(Parsed < 4096 ? Parsed : 4096);
                }
            };
            ReadParam("w", Width);
            ReadParam("h", Height);
        }

        // RmlUi joins the src against the document directory, stripping the
        // leading '/'. Engine virtual paths are absolute, so restore it before
        // the registry's exact-path lookup.
        if (!Path.empty() && Path[0] != '/')
        {
            Path = "/" + Path;
        }

        const FStringView PathView(Path.c_str(), Path.size());
        CObject* Asset = LoadObject<CObject>(PathView);
        if (Asset == nullptr)
        {
            LOG_WARN("[RmlUi] LoadTexture: no asset at '{}'.", Path.c_str());
            return 0;
        }

        if (CMaterialInterface* Material = Cast<CMaterialInterface>(Asset))
        {
            return LoadMaterialBrush(OutDimensions, Material, PathView, Width, Height);
        }
        if (CTexture* Texture = Cast<CTexture>(Asset))
        {
            return LoadTextureAsset(OutDimensions, Texture);
        }

        LOG_WARN("[RmlUi] LoadTexture: '{}' is neither a material nor a texture.", Path.c_str());
        return 0;
    }

    Rml::TextureHandle FRmlUiRenderer::LoadTextureAsset(Rml::Vector2i& OutDimensions, CTexture* Texture)
    {
        // Reuse the asset's already-uploaded RHIImage. No re-decode, no re-upload.
        FRHIImage* RawImage = Texture->GetRHIRef();
        if (RawImage == nullptr)
        {
            LOG_WARN("[RmlUi] LoadTexture: '{}' has no RHI image (asset not yet uploaded?).", Texture->GetName().c_str());
            return 0;
        }

        const FRHIImageDesc& ImgDesc = Texture->GetTextureResource().ImageDescription;
        if (ImgDesc.Extent.x == 0 || ImgDesc.Extent.y == 0)
        {
            LOG_WARN("[RmlUi] LoadTexture: '{}' has zero extent.", Texture->GetName().c_str());
            return 0;
        }

        // Sampled bindless: the image is registered in the texture table on
        // creation, so its resource id is the index the shader samples.
        const int32 ResourceID = RawImage->GetResourceID();
        if (ResourceID < 0)
        {
            LOG_WARN("[RmlUi] LoadTexture: '{}' has no bindless resource id.", Texture->GetName().c_str());
            return 0;
        }

        // Pin the asset for the lifetime of the RmlUi handle so an unload doesn't
        // dangle the bindless slot. Released in ReleaseTexture.
        Texture->AddToRoot();

        const Rml::TextureHandle Handle = NextTextureHandle++;
        FTexture Tex;
        Tex.Image           = FRHIImageRef(RawImage);
        Tex.ResourceID      = ResourceID;
        Tex.AssetKeepalive  = Texture;
        Textures.emplace(Handle, Move(Tex));

        OutDimensions.x = int(ImgDesc.Extent.x);
        OutDimensions.y = int(ImgDesc.Extent.y);
        return Handle;
    }

    Rml::TextureHandle FRmlUiRenderer::LoadMaterialBrush(Rml::Vector2i& OutDimensions, CMaterialInterface* Material, const FStringView& SourcePath, uint32 Width, uint32 Height)
    {
        if (Material->GetMaterialType() != EMaterialType::UI)
        {
            LOG_WARN("[RmlUi] LoadTexture: '{}' is not a UI material (MaterialType must be UI).", Material->GetName().c_str());
            return 0;
        }

        // Persistent RGBA8 brush RT; resting state ShaderResource so the UI can
        // sample it, transitioned to RenderTarget each frame in RenderMaterialBrushes.
        FRHIImageDesc Desc;
        Desc.Extent      = glm::uvec2(Width, Height);
        Desc.Format      = EFormat::RGBA8_UNORM;
        Desc.Dimension   = EImageDimension::Texture2D;
        Desc.NumMips     = 1;
        Desc.ArraySize   = 1;
        Desc.NumSamples  = 1;
        Desc.DebugName   = "UIMaterialBrush";
        Desc.InitialState = EResourceStates::ShaderResource;
        Desc.bKeepInitialState = true;
        Desc.Flags.SetMultipleFlags(EImageCreateFlags::RenderTarget, EImageCreateFlags::ShaderResource);

        FRHIImageRef Image = GRenderContext->CreateImage(Desc);
        if (!Image || Image->GetResourceID() < 0)
        {
            LOG_WARN("[RmlUi] LoadTexture: failed to create brush RT for '{}'.", Material->GetName().c_str());
            return 0;
        }

        Material->AddToRoot();

        const Rml::TextureHandle Handle = NextTextureHandle++;
        FTexture Tex;
        Tex.Image           = Image;
        Tex.ResourceID      = Image->GetResourceID();
        Tex.BrushMaterial   = Material;
        Tex.BrushSize       = glm::uvec2(Width, Height);
        Tex.BrushSourcePath = FString(SourcePath.data(), SourcePath.size());
        Textures.emplace(Handle, Move(Tex));

        OutDimensions.x = int(Width);
        OutDimensions.y = int(Height);
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
        if (!Image || Image->GetResourceID() < 0)
        {
            return 0;
        }

        const Rml::TextureHandle Handle = NextTextureHandle++;
        FTexture Tex;
        Tex.Image      = Image;
        Tex.ResourceID = Image->GetResourceID();
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
            if (It->second.BrushMaterial != nullptr)
            {
                It->second.BrushMaterial->RemoveFromRoot();
                It->second.BrushMaterial = nullptr;
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

    bool FRmlUiRenderer::EnsureMaterialBufferSet()
    {
        if (GRenderManager == nullptr)
        {
            return false;
        }
        FRHIBuffer* MatBuffer = GRenderManager->GetMaterialManager().GetMaterialBuffer();
        if (MatBuffer == nullptr)
        {
            return false;
        }
        if (MaterialBufferSet && MaterialBufferCached == MatBuffer)
        {
            return true;
        }

        if (!MaterialBufferLayout)
        {
            // Set 0, binding 5 -- mirrors uMaterialUniforms in UIMaterialGlobals.slang.
            FBindingLayoutDesc LayoutDesc;
            LayoutDesc.SetDebugName("UIMaterialBuffer")
                      .SetBindingIndex(0)
                      .SetVisibility(ERHIShaderType::Fragment)
                      .AddItem(FBindingLayoutItem::Buffer_SRV(5));
            MaterialBufferLayout = GRenderContext->CreateBindingLayout(LayoutDesc);
        }

        FBindingSetDesc SetDesc;
        SetDesc.AddItem(FBindingSetItem::BufferSRV(5, MatBuffer));
        MaterialBufferSet = GRenderContext->CreateBindingSet(SetDesc, MaterialBufferLayout);
        MaterialBufferCached = MatBuffer;
        return MaterialBufferSet != nullptr;
    }

    void FRmlUiRenderer::RevalidateBrushes(ICommandList& CmdList)
    {
        // Toggle each brush's stale state from whether its source path still
        // resolves to the material it cached. The material stays rooted either
        // way, so a rename-back (or an asset reappearing) just resumes rendering
        // without a render-thread reload. Driven by the asset-registry broadcast,
        // so this runs only on asset changes -- never per frame.
        for (auto& KV : Textures)
        {
            FTexture& Tex = KV.second;
            if (Tex.BrushMaterial == nullptr || Tex.BrushSourcePath.empty())
            {
                continue;
            }

            const FAssetData* Data = FAssetRegistry::Get().GetAssetByPath(
                FStringView(Tex.BrushSourcePath.c_str(), Tex.BrushSourcePath.size()));
            const bool bResolves = (Data != nullptr) && (Data->AssetGUID == Tex.BrushMaterial->GetGUID());

            if (!bResolves && !Tex.bBrushStale)
            {
                // Source renamed/deleted: clear to transparent and stop rendering
                // so the document breaks instead of showing the old asset.
                FRHIImage* StaleRT = Tex.Image.GetReference();
                CmdList.SetImageState(StaleRT, AllSubresources, EResourceStates::CopyDest);
                CmdList.CommitBarriers();
                CmdList.ClearImageColor(StaleRT, FColor(0.0f, 0.0f, 0.0f, 0.0f));
                CmdList.SetImageState(StaleRT, AllSubresources, EResourceStates::ShaderResource);
                CmdList.CommitBarriers();
                Tex.bBrushStale = true;
            }
            else if (bResolves && Tex.bBrushStale)
            {
                // Asset returned at this path (e.g. renamed back): resume next frame.
                Tex.bBrushStale = false;
            }
        }
    }

    void FRmlUiRenderer::RenderMaterialBrushes()
    {
        if (CurrentCmdList == nullptr || GRenderManager == nullptr)
        {
            return;
        }
        ICommandList& CmdList = *CurrentCmdList;

        // Drop brushes whose source asset went away. Event-driven (set on the
        // registry broadcast), so there is no per-frame validation cost.
        if (bBrushRevalidatePending.exchange(false, std::memory_order_acquire))
        {
            RevalidateBrushes(CmdList);
        }

        if (DrawCalls.empty())
        {
            return;
        }

        // Monotonic wall clock drives animated UI materials (GetTime() in-shader).
        static const auto StartTime = std::chrono::steady_clock::now();
        const float Time = std::chrono::duration<float>(std::chrono::steady_clock::now() - StartTime).count();

        FRasterState RasterState;
        RasterState.SetCullNone();
        FDepthStencilState DepthState;
        DepthState.DisableDepthTest().DisableDepthWrite().DisableStencil();
        FRenderState BrushRenderState;
        BrushRenderState.SetRasterState(RasterState).SetDepthStencilState(DepthState);

        // Only render brushes referenced this frame (brushes are shared across all
        // contexts; this scopes work to the drawing context). De-dup repeats.
        TVector<Rml::TextureHandle> Rendered;
        bool bEnsured = false;

        for (const FDrawCall& Draw : DrawCalls)
        {
            if (Draw.Texture == 0)
            {
                continue;
            }
            auto It = Textures.find(Draw.Texture);
            if (It == Textures.end() || It->second.BrushMaterial == nullptr || It->second.bBrushStale)
            {
                continue;
            }

            bool bAlready = false;
            for (Rml::TextureHandle H : Rendered)
            {
                if (H == Draw.Texture) { bAlready = true; break; }
            }
            if (bAlready)
            {
                continue;
            }
            Rendered.push_back(Draw.Texture);

            FTexture& Tex = It->second;
            CMaterialInterface* Material = Tex.BrushMaterial;
            if (!Material->IsReadyForRender() || Material->GetMaterialType() != EMaterialType::UI)
            {
                continue;
            }
            FRHIVertexShader* VS = Material->GetVertexShader();
            FRHIPixelShader*  PS = Material->GetPixelShader();
            if (VS == nullptr || PS == nullptr)
            {
                continue;
            }
            if (!bEnsured)
            {
                if (!EnsureMaterialBufferSet())
                {
                    return;
                }
                bEnsured = true;
            }

            FRHIImage* RT = Tex.Image.GetReference();

            FRenderPassDesc::FAttachment Attachment;
            Attachment.SetImage(RT).SetLoadOp(ERenderLoadOp::Clear);

            FRenderPassDesc RenderPass;
            RenderPass.AddColorAttachment(Attachment).SetRenderArea(RT->GetExtent());

            // Per-frame create relies on the pipeline cache and picks up live
            // recompiles (the material's VS/PS pointers change on compile).
            FGraphicsPipelineDesc Desc;
            Desc.SetDebugName("UI Material Brush");
            Desc.SetRenderState(BrushRenderState);
            Desc.AddBindingLayout(MaterialBufferLayout);
            Desc.AddBindingLayout(GRenderManager->GetTextureManager().GetLayout());
            Desc.SetVertexShader(VS);
            Desc.SetPixelShader(PS);

            FRHIGraphicsPipelineRef BrushPipeline = GRenderContext->CreateGraphicsPipeline(Desc, RenderPass);
            if (!BrushPipeline)
            {
                continue;
            }

            CmdList.SetImageState(RT, AllSubresources, EResourceStates::RenderTarget);
            CmdList.CommitBarriers();

            FGraphicsState State;
            State.SetPipeline(BrushPipeline);
            State.SetRenderPass(RenderPass);
            State.AddBindingSet(MaterialBufferSet.GetReference());
            State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            State.AddViewport(FViewport(0.0f, float(Tex.BrushSize.x), 0.0f, float(Tex.BrushSize.y), 0.0f, 1.0f));
            State.AddScissor(FRect(0, int(Tex.BrushSize.x), 0, int(Tex.BrushSize.y)));

            CmdList.SetGraphicsState(State);

            FUIMaterialBrushPush PC = {};
            PC.ScreenSize    = glm::uvec4(Tex.BrushSize.x, Tex.BrushSize.y, 0u, 0u);
            PC.Time          = Time;
            PC.MaterialIndex = (uint32)Material->GetMaterialIndex();
            CmdList.SetPushConstants(&PC, sizeof(PC));

            CmdList.Draw(3, 1, 0, 0);

            // Back to ShaderResource so the UI pass can sample it; CommitBarriers
            // also closes the brush render pass.
            CmdList.SetImageState(RT, AllSubresources, EResourceStates::ShaderResource);
            CmdList.CommitBarriers();
        }
    }
}
