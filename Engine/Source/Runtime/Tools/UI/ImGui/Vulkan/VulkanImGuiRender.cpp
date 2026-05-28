#include "pch.h"
#include "VulkanImGuiRender.h"

#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_glfw.h"

#include <GLFW/glfw3.h>
#include <algorithm>

#include "Core/Engine/Engine.h"
#include "Core/Profiler/Profile.h"
#include "Core/Windows/Window.h"
#include "Core/Math/Hash/Hash.h"
#include "Paths/Paths.h"
#include "Renderer/CommandList.h"
#include "Renderer/RenderContext.h"
#include "Renderer/RenderManager.h"
#include "Renderer/RenderResource.h"
#include "Renderer/RenderThread.h"
#include "Renderer/RenderTypes.h"
#include "Renderer/RHIGlobals.h"
#include "Renderer/TextureManager.h"
#include "Renderer/GPUProfiler/GPUProfiler.h"
#include "Renderer/API/Vulkan/VulkanRenderContext.h"
#include "Renderer/API/Vulkan/VulkanSwapchain.h"
#include "Tools/Import/ImportHelpers.h"

namespace Lumina
{
    static FVulkanImGuiRender* GImGuiBackend = nullptr;

    // Per secondary-viewport renderer state, stored in ImGuiViewport::RendererUserData.
    struct FImGuiViewportData
    {
        FVulkanSwapchain* Swapchain = nullptr;   // created on game thread; driven + destroyed on render thread
        FUIntVector2        Size = {0, 0};
    };

    // 24 B push block; must mirror Includes/ImGuiCommon.slang::FImGuiPushConstants.
    struct FImGuiPushConstants
    {
        float  Scale[2];
        float  Translate[2];
        uint32 TextureID;
        uint32 SamplerIndex;
    };
    static_assert(sizeof(FImGuiPushConstants) == 24, "Must match ImGuiCommon.slang::FImGuiPushConstants.");
    static_assert(sizeof(FImGuiPushConstants) <= MaxPushConstantSize, "ImGui push constants exceed RHI cap.");

    // ImGui samples with bilinear + wrap (matches the stock backend's repeat
    // sampler; some editor UIs tile textures via wrapped UVs).
    static constexpr uint32 GImGuiSamplerIndex = (uint32)RHI::EBindlessSampler::LinearWrap;

    static uint64 SubresKey(const FRHIImage* Image, const FTextureSubresourceSet& Sub)
    {
        uint64 Key = (uintptr_t)Image;
        Hash::HashCombine(Key, Sub.BaseMipLevel);
        Hash::HashCombine(Key, Sub.NumMipLevels);
        Hash::HashCombine(Key, Sub.BaseArraySlice);
        Hash::HashCombine(Key, Sub.NumArraySlices);
        return Key;
    }

    void FVulkanImGuiRender::Initialize()
    {
        IImGuiRenderer::Initialize();
        LUMINA_PROFILE_SCOPE();

        GImGuiBackend       = this;
        VulkanRenderContext = static_cast<FVulkanRenderContext*>(GRenderContext);
        BindlessLayout      = GRenderManager->GetTextureManager().GetLayout();

        ImGui_ImplGlfw_InitForVulkan(Windowing::GetPrimaryWindowHandle()->GetWindow(), true);

        ImGuiIO& IO = ImGui::GetIO();
        IO.BackendRendererName = "Lumina_Bindless";
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // 16-bit indices + VtxOffset for >64K meshes
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;   // ImTextureData create/update/destroy path
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;  // we drive secondary-window swapchains

        // Native ImDrawVert layout (pos float2 @0, uv float2 @8, col RGBA8 @16), stride 20.
        static_assert(sizeof(ImDrawVert) == 20, "ImDrawVert layout drifted; ImGui input layout must be updated.");
        FVertexAttributeDesc Attribs[3];
        Attribs[0].Format        = EFormat::RG32_FLOAT;   // POSITION
        Attribs[0].BufferIndex   = 0;
        Attribs[0].Offset        = offsetof(ImDrawVert, pos);
        Attribs[0].ElementStride = sizeof(ImDrawVert);
        Attribs[1].Format        = EFormat::RG32_FLOAT;   // TEXCOORD0
        Attribs[1].BufferIndex   = 0;
        Attribs[1].Offset        = offsetof(ImDrawVert, uv);
        Attribs[1].ElementStride = sizeof(ImDrawVert);
        Attribs[2].Format        = EFormat::RGBA8_UNORM;  // COLOR
        Attribs[2].BufferIndex   = 0;
        Attribs[2].Offset        = offsetof(ImDrawVert, col);
        Attribs[2].ElementStride = sizeof(ImDrawVert);
        InputLayout = GRenderContext->CreateInputLayout(Attribs, 3);

        ImGuiPlatformIO& PlatformIO   = ImGui::GetPlatformIO();
        PlatformIO.Renderer_CreateWindow  = &FVulkanImGuiRender::OnRendererCreateWindow;
        PlatformIO.Renderer_DestroyWindow = &FVulkanImGuiRender::OnRendererDestroyWindow;
        PlatformIO.Renderer_SetWindowSize = &FVulkanImGuiRender::OnRendererSetWindowSize;

        // 1x1 white fallback for failed loads / out-of-range texture ids.
        FName WhitePath = Paths::GetEngineResourceDirectory() + "/Textures/WhiteSquareTexture.png";
        WhiteImage = Import::Textures::CreateTextureFromImport(WhitePath.ToString(), false);
        if (WhiteImage != nullptr)
        {
            WhiteTextureID = WhiteImage->GetResourceID();
        }
    }

    void FVulkanImGuiRender::Deinitialize()
    {
        VulkanRenderContext->WaitIdle();

        FRecursiveScopeLock Lock(Mutex);

        PipelinesByFormat.clear();
        InputLayout.SafeRelease();

        // Release custom subresource SRVs registered in the bindless table.
        if (GRenderManager != nullptr)
        {
            RHI::FTextureManager& TexMgr = GRenderManager->GetTextureManager();
            for (auto& KV : SubresTextures)
            {
                TexMgr.ReleaseSubresourceSRV(KV.second.ResourceID);
            }
        }
        SubresTextures.clear();

        PathTextures.clear();
        FontTextures.clear();
        WhiteImage.SafeRelease();
        ReferencedImages.clear();

        for (TVector<FCapturedViewport>& Slot : SecondaryCaptures)
        {
            Slot.clear();
        }

        // DestroyContext tears down platform windows, routing each remaining
        // secondary viewport through OnRendererDestroyWindow (flush + destroy).
        ImGui_ImplGlfw_Shutdown();
        ImPlot::DestroyContext();
        ClearSnapshots();
        ImGui::DestroyContext();

        GImGuiBackend = nullptr;
    }

    void FVulkanImGuiRender::OnStartFrame(const FUpdateContext& UpdateContext)
    {
        LUMINA_PROFILE_SCOPE();

        {
            FRecursiveScopeLock Lock(Mutex);
            ReferencedImages.clear();
        }

        {
            LUMINA_PROFILE_SECTION_COLORED("ImGui_ImplGlfw_NewFrame", tracy::Color::Aquamarine);
            ImGui_ImplGlfw_NewFrame();
        }
        {
            LUMINA_PROFILE_SECTION_COLORED("ImGui::NewFrame", tracy::Color::Aquamarine);
            ImGui::NewFrame();
        }
    }

    void FVulkanImGuiRender::FillReferencedImagesSnapshot(TVector<FRHIImageRef>& Out)
    {
        FRecursiveScopeLock Lock(Mutex);
        Out.reserve(ReferencedImages.size());
        for (const FRHIImageRef& Image : ReferencedImages)
        {
            Out.push_back(Image);
        }
    }

    FRHIGraphicsPipeline* FVulkanImGuiRender::GetOrCreatePipeline(EFormat ColorFormat)
    {
        auto It = PipelinesByFormat.find((uint32)ColorFormat);
        if (It != PipelinesByFormat.end())
        {
            return It->second.GetReference();
        }

        FRHIVertexShaderRef VS = FShaderLibrary::GetVertexShader(FName("ImGuiVert"));
        FRHIPixelShaderRef  PS = FShaderLibrary::GetPixelShader(FName("ImGuiPixel"));
        if (!VS || !PS)
        {
            LOG_ERROR("[ImGui] ImGuiVert.slang / ImGuiPixel.slang not found in shader library.");
            return nullptr;
        }

        // Straight (non-premultiplied) alpha: SrcAlpha / OneMinusSrcAlpha, matching ImGui's vertex colours.
        FBlendState BlendState;
        BlendState.Targets[0].EnableBlend()
            .SetSrcBlend(EBlendFactor::SrcAlpha)
            .SetDestBlend(EBlendFactor::OneMinusSrcAlpha)
            .SetBlendOp(EBlendOp::Add)
            .SetSrcBlendAlpha(EBlendFactor::One)
            .SetDestBlendAlpha(EBlendFactor::OneMinusSrcAlpha)
            .SetBlendOpAlpha(EBlendOp::Add);

        FDepthStencilState DepthState;
        DepthState.DisableDepthTest().DisableDepthWrite().DisableStencil();

        FRasterState RasterState;
        RasterState.SetCullNone().EnableScissor();

        FRenderState RenderState;
        RenderState.SetBlendState(BlendState)
                   .SetDepthStencilState(DepthState)
                   .SetRasterState(RasterState);

        FRenderPassDesc PassDesc;
        FRenderPassDesc::FAttachment Attachment;
        Attachment.SetFormat(ColorFormat).SetLoadOp(ERenderLoadOp::Clear);
        PassDesc.AddColorAttachment(Attachment);

        FGraphicsPipelineDesc PipelineDesc;
        PipelineDesc.SetDebugName("ImGuiPipeline")
                    .SetPrimType(EPrimitiveType::TriangleList)
                    .SetInputLayout(InputLayout)
                    .SetVertexShader(VS)
                    .SetPixelShader(PS)
                    .SetRenderState(RenderState)
                    .AddBindingLayout(BindlessLayout);

        FRHIGraphicsPipelineRef Pipeline = GRenderContext->CreateGraphicsPipeline(PipelineDesc, PassDesc);
        if (!Pipeline)
        {
            LOG_ERROR("[ImGui] Failed to create graphics pipeline for format {}.", (uint32)ColorFormat);
            return nullptr;
        }

        PipelinesByFormat.insert_or_assign((uint32)ColorFormat, Pipeline);
        return Pipeline.GetReference();
    }

    void FVulkanImGuiRender::RecordDrawData(ICommandList& CmdList, ImDrawData* DrawData, FRHIImage* Target)
    {
        if (DrawData == nullptr || DrawData->CmdListsCount == 0 || Target == nullptr)
        {
            return;
        }

        const float FBW = DrawData->DisplaySize.x * DrawData->FramebufferScale.x;
        const float FBH = DrawData->DisplaySize.y * DrawData->FramebufferScale.y;
        if (FBW <= 0.0f || FBH <= 0.0f)
        {
            return;
        }

        FRHIGraphicsPipeline* Pipeline = GetOrCreatePipeline(Target->GetFormat());
        if (Pipeline == nullptr)
        {
            return;
        }

        const int32 TotalVtx = DrawData->TotalVtxCount;
        const int32 TotalIdx = DrawData->TotalIdxCount;
        if (TotalVtx == 0 || TotalIdx == 0)
        {
            return;
        }

        const ImVec2 ClipOff   = DrawData->DisplayPos;
        const ImVec2 ClipScale = DrawData->FramebufferScale;

        FImGuiPushConstants PC;
        PC.Scale[0]     = 2.0f / DrawData->DisplaySize.x;
        PC.Scale[1]     = 2.0f / DrawData->DisplaySize.y;
        PC.Translate[0] = -1.0f - DrawData->DisplayPos.x * PC.Scale[0];
        PC.Translate[1] = -1.0f - DrawData->DisplayPos.y * PC.Scale[1];
        PC.SamplerIndex = GImGuiSamplerIndex;

        FTransientAlloc VB = CmdList.AllocateTransient((size_t)TotalVtx * sizeof(ImDrawVert), 16);
        FTransientAlloc IB = CmdList.AllocateTransient((size_t)TotalIdx * sizeof(ImDrawIdx), 4);
        if (!VB || !IB)
        {
            return;
        }

        ImDrawVert* VtxDst = static_cast<ImDrawVert*>(VB.Cpu);
        ImDrawIdx*  IdxDst = static_cast<ImDrawIdx*>(IB.Cpu);
        for (int32 n = 0; n < DrawData->CmdListsCount; ++n)
        {
            const ImDrawList* List = DrawData->CmdLists[n];
            Memory::Memcpy(VtxDst, List->VtxBuffer.Data, (size_t)List->VtxBuffer.Size * sizeof(ImDrawVert));
            Memory::Memcpy(IdxDst, List->IdxBuffer.Data, (size_t)List->IdxBuffer.Size * sizeof(ImDrawIdx));
            VtxDst += List->VtxBuffer.Size;
            IdxDst += List->IdxBuffer.Size;
        }

        FRenderPassDesc::FAttachment Attachment;
        Attachment.SetImage(Target).SetLoadOp(ERenderLoadOp::Clear).SetStoreOp(ERenderStoreOp::Store);
        FRenderPassDesc Pass;
        Pass.AddColorAttachment(Attachment).SetRenderArea(Target->GetExtent());

        FGraphicsState State;
        State.SetPipeline(Pipeline);
        State.SetRenderPass(Pass);
        State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
        State.SetVertexBuffer(FVertexBufferBinding{}.SetBuffer(VB.Buffer).SetSlot(0).SetOffset(VB.Offset));
        State.SetIndexBuffer(FIndexBufferBinding{}.SetBuffer(IB.Buffer).SetFormat(EFormat::R16_UINT).SetOffset(IB.Offset));
        State.AddViewport(FViewport(0.0f, FBW, 0.0f, FBH, 0.0f, 1.0f));
        State.AddScissor(FRect(0, (int)FBW, 0, (int)FBH));

        // Caller already transitioned images; suppress automatic barriers (bindless is invisible to them).
        CmdList.DisableAutomaticBarriers();
        CmdList.SetGraphicsState(State);

        int64 LastTexID  = INT64_MIN;
        FRect LastScissor(-1, -1, -1, -1);

        uint32 GlobalVtxOffset = 0;
        uint32 GlobalIdxOffset = 0;
        for (int32 n = 0; n < DrawData->CmdListsCount; ++n)
        {
            const ImDrawList* List = DrawData->CmdLists[n];
            for (int32 c = 0; c < List->CmdBuffer.Size; ++c)
            {
                const ImDrawCmd& Cmd = List->CmdBuffer[c];
                if (Cmd.UserCallback != nullptr)
                {
                    if (Cmd.UserCallback == ImDrawCallback_ResetRenderState)
                    {
                        CmdList.SetGraphicsState(State);
                        LastTexID = INT64_MIN;
                        LastScissor = FRect(-1, -1, -1, -1);
                    }
                    else
                    {
                        Cmd.UserCallback(List, &Cmd);
                    }
                    continue;
                }

                // Project the clip rect into framebuffer pixels and clamp to the target.
                float ClipMinX = (Cmd.ClipRect.x - ClipOff.x) * ClipScale.x;
                float ClipMinY = (Cmd.ClipRect.y - ClipOff.y) * ClipScale.y;
                float ClipMaxX = (Cmd.ClipRect.z - ClipOff.x) * ClipScale.x;
                float ClipMaxY = (Cmd.ClipRect.w - ClipOff.y) * ClipScale.y;
                ClipMinX = std::max(ClipMinX, 0.0f);
                ClipMinY = std::max(ClipMinY, 0.0f);
                ClipMaxX = std::min(ClipMaxX, FBW);
                ClipMaxY = std::min(ClipMaxY, FBH);
                if (ClipMaxX <= ClipMinX || ClipMaxY <= ClipMinY)
                {
                    continue;
                }

                const FRect Scissor((int)ClipMinX, (int)ClipMaxX, (int)ClipMinY, (int)ClipMaxY);
                if (Scissor != LastScissor)
                {
                    CmdList.SetScissor(Scissor);
                    LastScissor = Scissor;
                }

                const int32 RawTexID = (int32)Cmd.GetTexID();
                const int64 TexID    = (RawTexID >= 0) ? RawTexID : WhiteTextureID;
                if (TexID != LastTexID)
                {
                    PC.TextureID = (uint32)TexID;
                    CmdList.SetPushConstants(&PC, sizeof(PC));
                    LastTexID = TexID;
                }

                CmdList.DrawIndexed(Cmd.ElemCount, 1, Cmd.IdxOffset + GlobalIdxOffset,
                                    (int32)(Cmd.VtxOffset + GlobalVtxOffset), 0);
            }
            GlobalVtxOffset += List->VtxBuffer.Size;
            GlobalIdxOffset += List->IdxBuffer.Size;
        }

        CmdList.EndRenderPass();
        CmdList.EnableAutomaticBarriers();
    }

    void FVulkanImGuiRender::OnEndFrame(ICommandList& CmdList, FImDrawDataSnapshot& Snapshot)
    {
        GPU_PROFILE_SCOPE(&CmdList, "ImGui");

        FRHIImage* Target = FEngine::GetEngineViewport()->GetRenderTarget();

        // Upload any pending font-atlas pixels on this list, before sampling them.
        FlushFontUploads_RenderThread(CmdList);

        // Sampled user images (RTs, thumbnails) -> ShaderResource; keep alive
        // while the GPU reads them. The font atlas rests in ShaderResource.
        for (const FRHIImageRef& Image : Snapshot.ReferencedImages)
        {
            if (Image.GetReference() == Target)
            {
                continue;
            }
            CmdList.SetImageState(Image, AllSubresources, EResourceStates::ShaderResource);
            CmdList.KeepAlive(Image);
        }
        CmdList.SetImageState(Target, AllSubresources, EResourceStates::RenderTarget);
        CmdList.CommitBarriers();

        RecordDrawData(CmdList, Snapshot.GetDrawData(), Target);

        constexpr uint64 CleanupIntervalFrames = 60;
        constexpr uint64 UnusedFrameThreshold  = 120;
        const uint64 CurrentFrame = GEngine->GetUpdateContext().GetFrame();
        if (CurrentFrame - LastCleanupFrame >= CleanupIntervalFrames)
        {
            LastCleanupFrame = CurrentFrame;
            FRecursiveScopeLock Lock(Mutex);

            for (auto It = PathTextures.begin(); It != PathTextures.end(); )
            {
                if (CurrentFrame - It->second.LastUseFrame > UnusedFrameThreshold)
                {
                    It = PathTextures.erase(It);
                }
                else
                {
                    ++It;
                }
            }

            RHI::FTextureManager& TexMgr = GRenderManager->GetTextureManager();
            for (auto It = SubresTextures.begin(); It != SubresTextures.end(); )
            {
                if (CurrentFrame - It->second.LastUseFrame > UnusedFrameThreshold)
                {
                    TexMgr.ReleaseSubresourceSRV(It->second.ResourceID);
                    It = SubresTextures.erase(It);
                }
                else
                {
                    ++It;
                }
            }
        }
    }

    ImTextureID FVulkanImGuiRender::GetOrCreateImTexture(FStringView Path)
    {
        FRecursiveScopeLock Lock(Mutex);

        const uint64 Frame = GEngine->GetUpdateContext().GetFrame();
        FName NamePath = Path;

        auto It = PathTextures.find(NamePath);
        if (It != PathTextures.end())
        {
            It->second.LastUseFrame = Frame;
            if (It->second.Image != nullptr)
            {
                ReferencedImages.push_back(It->second.Image);
            }
            return (ImTextureID)(uint32)It->second.ResourceID;
        }

        FRHIImageRef Image = Import::Textures::CreateTextureFromImport(Path, false);
        if (Image == nullptr || Image->GetResourceID() < 0)
        {
            return (ImTextureID)(uint32)WhiteTextureID;
        }

        FPathEntry Entry;
        Entry.Image        = Image;
        Entry.ResourceID   = Image->GetResourceID();
        Entry.LastUseFrame = Frame;
        ReferencedImages.push_back(Image);
        PathTextures.insert_or_assign(NamePath, Move(Entry));

        return (ImTextureID)(uint32)Image->GetResourceID();
    }

    ImTextureID FVulkanImGuiRender::GetOrCreateImTexture(FRHIImage* Image, const FTextureSubresourceSet& Subresources)
    {
        if (Image == nullptr)
        {
            return (ImTextureID)(uint32)WhiteTextureID;
        }

        FRecursiveScopeLock Lock(Mutex);
        ReferencedImages.push_back(FRHIImageRef(Image));

        // The common case: whole-image default view is the auto-registered
        // bindless slot. No descriptor work at all.
        if (Subresources == AllSubresources)
        {
            const int32 ResourceID = Image->GetResourceID();
            return (ImTextureID)(uint32)((ResourceID >= 0) ? ResourceID : WhiteTextureID);
        }

        // A specific mip/slice needs its own bindless SRV (texture editor).
        const uint64 Frame = GEngine->GetUpdateContext().GetFrame();
        const uint64 Key   = SubresKey(Image, Subresources);

        auto It = SubresTextures.find(Key);
        if (It != SubresTextures.end())
        {
            It->second.LastUseFrame = Frame;
            return (ImTextureID)(uint32)It->second.ResourceID;
        }

        const int32 ResourceID = GRenderManager->GetTextureManager().RegisterSubresourceSRV(Image, Subresources);
        if (ResourceID < 0)
        {
            return (ImTextureID)(uint32)WhiteTextureID;
        }

        FSubresEntry Entry;
        Entry.ResourceID   = ResourceID;
        Entry.LastUseFrame = Frame;
        SubresTextures.insert_or_assign(Key, Entry);

        return (ImTextureID)(uint32)ResourceID;
    }

    void FVulkanImGuiRender::DestroyImTexture(uint64 Hash)
    {
        // Bindless IDs are owned by FRHIImage/the bindless table; nothing to free per-ID.
        (void)Hash;
    }

    void FVulkanImGuiRender::ProcessTextureUpdates_GameThread()
    {
        LUMINA_PROFILE_SCOPE();
        FRecursiveScopeLock Lock(Mutex);

        for (ImTextureData* Tex : ImGui::GetPlatformIO().Textures)
        {
            if (Tex->Status == ImTextureStatus_WantCreate)
            {
                CreateFontTexture(Tex);
            }

            if (Tex->Status == ImTextureStatus_WantCreate || Tex->Status == ImTextureStatus_WantUpdates)
            {
                auto It = FontTextures.find(Tex->UniqueID);
                if (It != FontTextures.end() && It->second.Image != nullptr && Tex->GetPixels() != nullptr)
                {
                    FPendingFontUpload Pending;
                    Pending.Image  = It->second.Image;
                    Pending.Width  = (uint32)Tex->Width;
                    Pending.Height = (uint32)Tex->Height;
                    const size_t Bytes = (size_t)Tex->Width * (size_t)Tex->Height * 4;
                    Pending.Pixels.resize(Bytes);
                    Memory::Memcpy(Pending.Pixels.data(), Tex->GetPixels(), Bytes);
                    PendingFontUploads.push_back(Move(Pending));
                }
                Tex->SetStatus(ImTextureStatus_OK);
            }
            else if (Tex->Status == ImTextureStatus_WantDestroy && Tex->UnusedFrames >= (int)(FRAMES_IN_FLIGHT * 2))
            {
                DestroyFontTexture(Tex);
            }
        }
    }

    void FVulkanImGuiRender::CreateFontTexture(ImTextureData* Tex)
    {
        FRHIImageDesc Desc;
        Desc.Extent            = FUIntVector2((uint32)Tex->Width, (uint32)Tex->Height);
        Desc.Format            = EFormat::RGBA8_UNORM;   // ImGui only emits RGBA32 with RendererHasTextures
        Desc.Dimension         = EImageDimension::Texture2D;
        Desc.NumMips           = 1;
        Desc.ArraySize         = 1;
        Desc.NumSamples        = 1;
        Desc.DebugName         = "ImGuiFontAtlas";
        Desc.InitialState      = EResourceStates::ShaderResource;
        Desc.bKeepInitialState = true;
        Desc.Flags.SetMultipleFlags(EImageCreateFlags::ShaderResource);

        FRHIImageRef Image = GRenderContext->CreateImage(Desc);
        if (Image == nullptr || Image->GetResourceID() < 0)
        {
            LOG_ERROR("[ImGui] Failed to create font atlas texture {}x{}.", Tex->Width, Tex->Height);
            Tex->SetTexID((ImTextureID)(uint32)WhiteTextureID);
            return;
        }

        FFontEntry Entry;
        Entry.Image      = Image;
        Entry.ResourceID = Image->GetResourceID();
        FontTextures.insert_or_assign(Tex->UniqueID, Entry);

        Tex->SetTexID((ImTextureID)(uint32)Entry.ResourceID);
    }

    void FVulkanImGuiRender::FlushFontUploads_RenderThread(ICommandList& CmdList)
    {
        FRecursiveScopeLock Lock(Mutex);
        if (PendingFontUploads.empty())
        {
            return;
        }

        // Dedup newest-wins: back-to-back copies without barrier → SYNC-HAZARD-WRITE-AFTER-WRITE.
        // WriteImage leaves CopyDest; must transition to ShaderResource here or draw samples black.
        TFixedVector<FRHIImage*, 8> Uploaded;
        for (int32 i = (int32)PendingFontUploads.size() - 1; i >= 0; --i)
        {
            FPendingFontUpload& Pending = PendingFontUploads[i];
            if (Pending.Image == nullptr)
            {
                continue;
            }
            FRHIImage* Image = Pending.Image.GetReference();

            bool bAlready = false;
            for (FRHIImage* U : Uploaded)
            {
                if (U == Image) { bAlready = true; break; }
            }
            if (bAlready)
            {
                continue;   // a newer upload for this image already ran
            }

            Uploaded.push_back(Image);
            CmdList.WriteImage(Image, 0, 0, Pending.Pixels.data(), Pending.Width * 4, 0);
            CmdList.KeepAlive(Pending.Image);
        }

        for (FRHIImage* Image : Uploaded)
        {
            CmdList.SetImageState(Image, AllSubresources, EResourceStates::ShaderResource);
        }
        CmdList.CommitBarriers();

        PendingFontUploads.clear();
    }

    void FVulkanImGuiRender::DestroyFontTexture(ImTextureData* Tex)
    {
        auto It = FontTextures.find(Tex->UniqueID);
        if (It != FontTextures.end())
        {
            FontTextures.erase(It);
        }
        Tex->SetTexID(ImTextureID_Invalid);
        Tex->SetStatus(ImTextureStatus_Destroyed);
    }

    void FVulkanImGuiRender::OnRendererCreateWindow(ImGuiViewport* Viewport)
    {
        FVulkanImGuiRender* Self = GImGuiBackend;
        if (Self == nullptr)
        {
            return;
        }

        GLFWwindow* Window = (GLFWwindow*)Viewport->PlatformHandle;
        if (Window == nullptr)
        {
            return;
        }

        int FbW = 0, FbH = 0;
        glfwGetFramebufferSize(Window, &FbW, &FbH);
        const FUIntVector2 Extent((uint32)std::max(FbW, 1), (uint32)std::max(FbH, 1));

        FImGuiViewportData* Data = IM_NEW(FImGuiViewportData)();
        Data->Swapchain = Memory::New<FVulkanSwapchain>();
        Data->Swapchain->CreateSwapchain(Self->VulkanRenderContext->GetVulkanInstance(), Self->VulkanRenderContext, Window, Extent, false, /*bPrimary*/ false);
        Data->Size = Extent;
        Viewport->RendererUserData = Data;
    }

    void FVulkanImGuiRender::OnRendererDestroyWindow(ImGuiViewport* Viewport)
    {
        FImGuiViewportData* Data = (FImGuiViewportData*)Viewport->RendererUserData;
        if (Data == nullptr)
        {
            return;
        }

        // Drain GPU before destroy: ImGui kills the GLFW surface right after this hook;
        // any in-flight capture would recreate against a dead surface (failed_query_surface_support_details).
        if (Data->Swapchain != nullptr)
        {
            FlushRenderingCommands();
            GRenderContext->WaitIdle();
            Memory::Delete(Data->Swapchain);
        }

        IM_DELETE(Data);
        Viewport->RendererUserData = nullptr;
    }

    void FVulkanImGuiRender::OnRendererSetWindowSize(ImGuiViewport* Viewport, ImVec2 Size)
    {
        // Only record the size; render thread recreates the swapchain on next OUT_OF_DATE acquire.
        FImGuiViewportData* Data = (FImGuiViewportData*)Viewport->RendererUserData;
        if (Data != nullptr)
        {
            Data->Size = FUIntVector2((uint32)std::max(Size.x, 1.0f), (uint32)std::max(Size.y, 1.0f));
        }
    }

    void FVulkanImGuiRender::CaptureViewports_GameThread(uint8 Slot)
    {
        LUMINA_PROFILE_SCOPE();

        TVector<FCapturedViewport>& Captures = SecondaryCaptures[Slot];
        Captures.clear();

        ImGuiPlatformIO& PlatformIO = ImGui::GetPlatformIO();
        for (int32 i = 1; i < PlatformIO.Viewports.Size; ++i)   // index 0 is the main viewport
        {
            ImGuiViewport*      Viewport = PlatformIO.Viewports[i];
            FImGuiViewportData* Data     = (FImGuiViewportData*)Viewport->RendererUserData;
            ImDrawData*         DrawData = Viewport->DrawData;
            if (Data == nullptr || Data->Swapchain == nullptr || DrawData == nullptr || DrawData->TotalVtxCount == 0)
            {
                continue;
            }

            const float FbW = DrawData->DisplaySize.x * DrawData->FramebufferScale.x;
            const float FbH = DrawData->DisplaySize.y * DrawData->FramebufferScale.y;
            if (FbW <= 0.0f || FbH <= 0.0f)
            {
                continue;
            }

            Captures.emplace_back();
            FCapturedViewport& Cap = Captures.back();
            Cap.Swapchain  = Data->Swapchain;
            Cap.ScaleX     = 2.0f / DrawData->DisplaySize.x;
            Cap.ScaleY     = 2.0f / DrawData->DisplaySize.y;
            Cap.TranslateX = -1.0f - DrawData->DisplayPos.x * Cap.ScaleX;
            Cap.TranslateY = -1.0f - DrawData->DisplayPos.y * Cap.ScaleY;
            Cap.FbWidth    = FbW;
            Cap.FbHeight   = FbH;
            Cap.Vertices.reserve(DrawData->TotalVtxCount);
            Cap.Indices.reserve(DrawData->TotalIdxCount);

            const ImVec2 ClipOff   = DrawData->DisplayPos;
            const ImVec2 ClipScale = DrawData->FramebufferScale;
            uint32 GlobalVtx = 0, GlobalIdx = 0;
            for (int32 n = 0; n < DrawData->CmdListsCount; ++n)
            {
                const ImDrawList* List = DrawData->CmdLists[n];
                Cap.Vertices.insert(Cap.Vertices.end(), List->VtxBuffer.Data, List->VtxBuffer.Data + List->VtxBuffer.Size);
                Cap.Indices.insert(Cap.Indices.end(),   List->IdxBuffer.Data, List->IdxBuffer.Data + List->IdxBuffer.Size);

                for (int32 c = 0; c < List->CmdBuffer.Size; ++c)
                {
                    const ImDrawCmd& Cmd = List->CmdBuffer[c];
                    if (Cmd.UserCallback != nullptr)
                    {
                        continue;   // user callbacks aren't replayed for secondary viewports
                    }

                    const float MinX = std::max((Cmd.ClipRect.x - ClipOff.x) * ClipScale.x, 0.0f);
                    const float MinY = std::max((Cmd.ClipRect.y - ClipOff.y) * ClipScale.y, 0.0f);
                    const float MaxX = std::min((Cmd.ClipRect.z - ClipOff.x) * ClipScale.x, FbW);
                    const float MaxY = std::min((Cmd.ClipRect.w - ClipOff.y) * ClipScale.y, FbH);
                    if (MaxX <= MinX || MaxY <= MinY)
                    {
                        continue;
                    }

                    FCapturedCmd CC;
                    CC.ClipMinX   = MinX; CC.ClipMinY = MinY; CC.ClipMaxX = MaxX; CC.ClipMaxY = MaxY;
                    const int32 TexID = (int32)Cmd.GetTexID();
                    CC.TextureID  = (uint32)((TexID >= 0) ? TexID : WhiteTextureID);
                    CC.ElemCount  = Cmd.ElemCount;
                    CC.IdxOffset  = Cmd.IdxOffset + GlobalIdx;
                    CC.VtxOffset  = (int32)(Cmd.VtxOffset + GlobalVtx);
                    Cap.Cmds.push_back(CC);
                }
                GlobalVtx += List->VtxBuffer.Size;
                GlobalIdx += List->IdxBuffer.Size;
            }
        }
    }

    void FVulkanImGuiRender::RenderViewports_RenderThread(uint8 Slot)
    {
        LUMINA_PROFILE_SCOPE();

        for (FCapturedViewport& Cap : SecondaryCaptures[Slot])
        {
            RenderCapturedViewport(Cap);
        }
        SecondaryCaptures[Slot].clear();
    }

    void FVulkanImGuiRender::RenderCapturedViewport(FCapturedViewport& Cap)
    {
        if (Cap.Swapchain == nullptr || Cap.Indices.empty())
        {
            return;
        }

        // Compare against desired (not actual) extent to avoid recreating every frame when surface clamps.
        const FUIntVector2 WantExtent((uint32)Cap.FbWidth, (uint32)Cap.FbHeight);
        if (WantExtent.x > 0 && WantExtent.y > 0 && Cap.Swapchain->GetDesiredExtent() != WantExtent)
        {
            Cap.Swapchain->RecreateSwapchain(WantExtent);
        }

        Cap.Swapchain->WaitForFramePace();
        if (!Cap.Swapchain->AcquireNextImage())
        {
            return;
        }

        FRHIImage* Target = Cap.Swapchain->GetCurrentImage().GetReference();
        if (Target == nullptr)
        {
            return;
        }
        FRHIGraphicsPipeline* Pipeline = GetOrCreatePipeline(Target->GetFormat());
        if (Pipeline == nullptr)
        {
            return;
        }

        FRHICommandListRef CmdListRef = GRenderContext->CreateCommandList(FCommandListInfo::Graphics());
        CmdListRef->Open();
        ICommandList& CmdList = *CmdListRef;

        CmdList.SetImageState(Target, AllSubresources, EResourceStates::RenderTarget);
        CmdList.CommitBarriers();

        const size_t VBytes = Cap.Vertices.size() * sizeof(ImDrawVert);
        const size_t IBytes = Cap.Indices.size()  * sizeof(ImDrawIdx);
        FTransientAlloc VB = CmdList.AllocateTransient(VBytes, 16);
        FTransientAlloc IB = CmdList.AllocateTransient(IBytes, 4);
        if (VB && IB)
        {
            Memory::Memcpy(VB.Cpu, Cap.Vertices.data(), VBytes);
            Memory::Memcpy(IB.Cpu, Cap.Indices.data(),  IBytes);

            // The image extent may be clamped below the requested (captured) size,
            // so viewport/scissor/renderArea must use the ACTUAL image extent.
            const FUIntVector2 ImgExtent = Target->GetExtent();
            const float ImgW = (float)ImgExtent.x;
            const float ImgH = (float)ImgExtent.y;

            FRenderPassDesc::FAttachment Attachment;
            Attachment.SetImage(Target).SetLoadOp(ERenderLoadOp::Clear).SetStoreOp(ERenderStoreOp::Store);
            FRenderPassDesc Pass;
            Pass.AddColorAttachment(Attachment).SetRenderArea(ImgExtent);

            FImGuiPushConstants PC;
            PC.Scale[0]     = Cap.ScaleX;     PC.Scale[1]     = Cap.ScaleY;
            PC.Translate[0] = Cap.TranslateX; PC.Translate[1] = Cap.TranslateY;
            PC.SamplerIndex = GImGuiSamplerIndex;

            FGraphicsState State;
            State.SetPipeline(Pipeline);
            State.SetRenderPass(Pass);
            State.AddBindingSet(GRenderManager->GetTextureManager().GetDescriptorTable());
            State.SetVertexBuffer(FVertexBufferBinding{}.SetBuffer(VB.Buffer).SetSlot(0).SetOffset(VB.Offset));
            State.SetIndexBuffer(FIndexBufferBinding{}.SetBuffer(IB.Buffer).SetFormat(EFormat::R16_UINT).SetOffset(IB.Offset));
            State.AddViewport(FViewport(0.0f, ImgW, 0.0f, ImgH, 0.0f, 1.0f));
            State.AddScissor(FRect(0, (int)ImgW, 0, (int)ImgH));

            CmdList.DisableAutomaticBarriers();
            CmdList.SetGraphicsState(State);

            int64 LastTexID = INT64_MIN;
            FRect LastScissor(-1, -1, -1, -1);
            for (const FCapturedCmd& CC : Cap.Cmds)
            {
                // Clamp to the actual image extent (clip rects were captured against
                // the requested size, which can exceed the clamped swapchain).
                const FRect Scissor((int)std::min(CC.ClipMinX, ImgW), (int)std::min(CC.ClipMaxX, ImgW),
                                    (int)std::min(CC.ClipMinY, ImgH), (int)std::min(CC.ClipMaxY, ImgH));
                if (Scissor.MaxX <= Scissor.MinX || Scissor.MaxY <= Scissor.MinY)
                {
                    continue;
                }
                if (Scissor != LastScissor)
                {
                    CmdList.SetScissor(Scissor);
                    LastScissor = Scissor;
                }
                if ((int64)CC.TextureID != LastTexID)
                {
                    PC.TextureID = CC.TextureID;
                    CmdList.SetPushConstants(&PC, sizeof(PC));
                    LastTexID = (int64)CC.TextureID;
                }
                CmdList.DrawIndexed(CC.ElemCount, 1, CC.IdxOffset, CC.VtxOffset, 0);
            }

            CmdList.EndRenderPass();
            CmdList.EnableAutomaticBarriers();
        }

        CmdList.SetImageState(Target, AllSubresources, EResourceStates::Present);
        CmdList.CommitBarriers();
        CmdListRef->Close();

        ICommandList* RawList = &CmdList;
        VulkanRenderContext->SubmitWithSemaphores(&RawList, 1, ECommandQueue::Graphics,
            Cap.Swapchain->GetCurrentAcquireSemaphore(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            Cap.Swapchain->GetCurrentPresentSemaphore());

        Cap.Swapchain->Present();
    }
}
