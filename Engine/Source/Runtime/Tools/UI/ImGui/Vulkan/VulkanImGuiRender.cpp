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
#include "Memory/Memcpy.h"
#include "Renderer/RHICore.h"
#include "Renderer/RHITexture.h"
#include "Tools/Import/ImportHelpers.h"

namespace Lumina
{
    // Mirrors FImGuiArgs in ImGuiVert/Pixel.slang (32 B scalar).
    struct FNewImGuiArgs
    {
        float  Scale[2];
        float  Translate[2];
        uint32 TextureID;
        uint32 SamplerIndex;
        uint64 VertexAddr;
    };

    void FVulkanImGuiRender::Initialize()
    {
        IImGuiRenderer::Initialize();
        LUMINA_PROFILE_SCOPE();

        ImGui_ImplGlfw_InitForVulkan(Windowing::GetPrimaryWindowHandle()->GetWindow(), true);

        ImGuiIO& IO = ImGui::GetIO();
        IO.BackendRendererName = "Lumina_RHI";
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // 16-bit indices + VtxOffset for >64K meshes
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;   // ImTextureData create/update/destroy path

        // The VS pulls ImDrawVert from the transient ring by device address; the shader hard-codes
        // the field offsets, so pin the layout here.
        static_assert(sizeof(ImDrawVert) == 20, "ImDrawVert layout drifted; ImGuiVert.slang must be updated.");
    }

    void FVulkanImGuiRender::Deinitialize()
    {
        RHI::WaitDeviceIdle();

        FRecursiveScopeLock Lock(Mutex);

        NewPipeline.Reset();
        NewDepthState.Reset();
        for (auto& KV : NewFontTextures)
        {
            RHI::Textures::Release(KV.second);
        }
        NewFontTextures.clear();

        for (auto& KV : PathTextures)
        {
            RHI::Textures::Release(KV.second);
        }
        PathTextures.clear();

        ImGui_ImplGlfw_Shutdown();
        ImPlot::DestroyContext();
        ClearSnapshots();
        ImGui::DestroyContext();
    }

    void FVulkanImGuiRender::OnStartFrame(const FUpdateContext& UpdateContext)
    {
        LUMINA_PROFILE_SCOPE();

        {
            LUMINA_PROFILE_SECTION_COLORED("ImGui_ImplGlfw_NewFrame", tracy::Color::Aquamarine);
            ImGui_ImplGlfw_NewFrame();
        }
        {
            LUMINA_PROFILE_SECTION_COLORED("ImGui::NewFrame", tracy::Color::Aquamarine);
            ImGui::NewFrame();
        }
    }

    void FVulkanImGuiRender::ProcessTextureUpdates_GameThread()
    {
        LUMINA_PROFILE_SCOPE();
        FRecursiveScopeLock Lock(Mutex);

        // Create + upload the font atlas into the new texture heap, so its ImTextureID is a
        // new-heap ResourceID the new-RHI ImGui shaders sample directly.
        for (ImTextureData* Tex : ImGui::GetPlatformIO().Textures)
        {
            if (Tex->Status == ImTextureStatus_WantCreate || Tex->Status == ImTextureStatus_WantUpdates)
            {
                auto It = NewFontTextures.find(Tex->UniqueID);
                if (It == NewFontTextures.end())
                {
                    RHI::FManagedTexture Created = RHI::Textures::Create(RHI::FTexture2DDesc
                    {
                        .Width  = (uint32)Tex->Width,
                        .Height = (uint32)Tex->Height,
                        .Format = EFormat::RGBA8_UNORM,
                    });
                    It = NewFontTextures.insert_or_assign(Tex->UniqueID, Created).first;
                    Tex->SetTexID((ImTextureID)(uint32)Created.ResourceID());
                }

                if (Tex->GetPixels() != nullptr)
                {
                    const uint64 Bytes = (uint64)Tex->Width * (uint64)Tex->Height * 4;
                    RHI::Textures::Upload(It->second, 0, Tex->GetPixels(), Bytes, (uint32)Tex->Width);
                }

                Tex->SetStatus(ImTextureStatus_OK);
            }
            else if (Tex->Status == ImTextureStatus_WantDestroy && Tex->UnusedFrames >= (int)(RHI::kFramesInFlight * 2))
            {
                auto It = NewFontTextures.find(Tex->UniqueID);
                if (It != NewFontTextures.end())
                {
                    RHI::Textures::Release(It->second);
                    NewFontTextures.erase(It);
                }
                Tex->SetTexID(ImTextureID_Invalid);
                Tex->SetStatus(ImTextureStatus_Destroyed);
            }
        }
    }

    void FVulkanImGuiRender::OnEndFrame_NewRHI(RHI::FCmdListH CL, RHI::FTextureH Target, const FUIntVector2& Extent, FImDrawDataSnapshot& Snapshot)
    {
        RecordDrawData_NewRHI(CL, Snapshot.GetDrawData(), Target, Extent);
    }

    void FVulkanImGuiRender::RecordDrawData_NewRHI(RHI::FCmdListH CL, ImDrawData* DrawData, RHI::FTextureH Target, const FUIntVector2& Extent)
    {
        if (!NewPipeline)
        {
            RHI::FBlendDesc Blend;
            Blend.bBlendEnable   = true;
            Blend.ColorOp        = RHI::EBlend::Add;
            Blend.SrcColorFactor = RHI::EFactor::SrcAlpha;
            Blend.DstColorFactor = RHI::EFactor::OneMinusSrcAlpha;
            Blend.AlphaOp        = RHI::EBlend::Add;
            Blend.SrcAlphaFactor = RHI::EFactor::One;
            Blend.DstAlphaFactor = RHI::EFactor::OneMinusSrcAlpha;

            const RHI::FColorTarget ColorTarget { .Format = EFormat::BGRA8_UNORM, .Blend = Blend };
            RHI::FRasterDesc RasterDesc;
            RasterDesc.Topology     = RHI::ETopology::TriangleList;
            RasterDesc.ColorTargets = TSpan<const RHI::FColorTarget>(&ColorTarget, 1);

            NewPipeline = RHI::Core::CreateGraphicsPipeline("ImGuiVert.slang", "ImGuiPixel.slang", RasterDesc);
        }
        if (!NewDepthState)
        {
            NewDepthState = RHI::CreateDepthStencil(RHI::FDepthStencilDesc{});
        }

        // Always begin/end the pass so the swapchain image is cleared even with no draws.
        RHI::FRenderAttachment Color;
        Color.Texture  = Target;
        Color.LoadOp   = RHI::ELoadOp::Clear;
        Color.StoreOp  = RHI::EStoreOp::Store;
        Color.Color[0] = Color.Color[1] = Color.Color[2] = 0.0f;
        Color.Color[3] = 1.0f;

        RHI::FRenderPassDesc Pass;
        Pass.ColorAttachments = TSpan<const RHI::FRenderAttachment>(&Color, 1);
        Pass.RenderArea       = Extent;

        RHI::CmdBeginRenderPass(CL, Pass);

        const float FBW = (float)Extent.x;
        const float FBH = (float)Extent.y;

        if (DrawData != nullptr && DrawData->TotalVtxCount > 0 && DrawData->TotalIdxCount > 0 && FBW > 0.0f && FBH > 0.0f && NewPipeline)
        {
            const int32 TotalVtx = DrawData->TotalVtxCount;
            const int32 TotalIdx = DrawData->TotalIdxCount;

            RHI::FTransientAlloc VB = RHI::Core::AllocTransient((size_t)TotalVtx * sizeof(ImDrawVert), 16);
            RHI::FTransientAlloc IB = RHI::Core::AllocTransient((size_t)TotalIdx * sizeof(ImDrawIdx), 4);

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

            FNewImGuiArgs Args;
            Args.Scale[0]     = 2.0f / DrawData->DisplaySize.x;
            Args.Scale[1]     = 2.0f / DrawData->DisplaySize.y;
            Args.Translate[0] = -1.0f - DrawData->DisplayPos.x * Args.Scale[0];
            Args.Translate[1] = -1.0f - DrawData->DisplayPos.y * Args.Scale[1];
            Args.SamplerIndex = (uint32)RHI::EStockSampler::LinearWrap;
            Args.VertexAddr   = VB.Gpu;

            RHI::CmdSetDepthStencilState(CL, NewDepthState.Get());
            RHI::CmdSetCullMode(CL, RHI::ECullMode::None);
            RHI::CmdSetFrontFace(CL, RHI::EFrontFace::CCW);
            RHI::CmdSetPipeline(CL, NewPipeline.Get());

            const ImVec2 ClipOff   = DrawData->DisplayPos;
            const ImVec2 ClipScale = DrawData->FramebufferScale;
            const uint32 DefaultTex = RHI::Textures::DefaultResourceID();

            uint32 GlobalVtx = 0, GlobalIdx = 0;
            for (int32 n = 0; n < DrawData->CmdListsCount; ++n)
            {
                const ImDrawList* List = DrawData->CmdLists[n];
                for (int32 c = 0; c < List->CmdBuffer.Size; ++c)
                {
                    const ImDrawCmd& Cmd = List->CmdBuffer[c];
                    if (Cmd.UserCallback != nullptr)
                    {
                        continue;
                    }

                    float MinX = std::max((Cmd.ClipRect.x - ClipOff.x) * ClipScale.x, 0.0f);
                    float MinY = std::max((Cmd.ClipRect.y - ClipOff.y) * ClipScale.y, 0.0f);
                    float MaxX = std::min((Cmd.ClipRect.z - ClipOff.x) * ClipScale.x, FBW);
                    float MaxY = std::min((Cmd.ClipRect.w - ClipOff.y) * ClipScale.y, FBH);
                    if (MaxX <= MinX || MaxY <= MinY)
                    {
                        continue;
                    }

                    RHI::CmdSetScissor(CL, RHI::FRect{ (int)MinX, (int)MaxX, (int)MinY, (int)MaxY });

                    const int32 RawTexID = (int32)Cmd.GetTexID();
                    Args.TextureID = (RawTexID >= 0) ? (uint32)RawTexID : DefaultTex;
                    const RHI::GPUPtr ArgsPtr = RHI::Core::CopyTransient(Args);

                    RHI::CmdDrawIndexed(CL, IB.Gpu, 0, ArgsPtr, Cmd.ElemCount, 1,
                                        Cmd.IdxOffset + GlobalIdx, (int32)(Cmd.VtxOffset + GlobalVtx), 0, RHI::EIndexType::Uint16);
                }
                GlobalVtx += List->VtxBuffer.Size;
                GlobalIdx += List->IdxBuffer.Size;
            }
        }

        RHI::CmdEndRenderPass(CL);
    }

    ImTextureID FVulkanImGuiRender::GetOrCreateImTexture(FStringView Path)
    {
        FRecursiveScopeLock Lock(Mutex);

        const FName NamePath = Path;
        if (auto It = PathTextures.find(NamePath); It != PathTextures.end())
        {
            return (ImTextureID)(uint32)It->second.ResourceID();
        }

        // Decode straight into the new texture heap (no old-RHI image), so the ImTextureID is a
        // new-heap ResourceID the new-RHI ImGui shaders sample directly.
        TOptional<Import::Textures::FTextureImportResult> Result = Import::Textures::ImportTexture(Path, false);
        if (!Result.has_value() || Result->Pixels.empty())
        {
            return (ImTextureID)(uint32)RHI::Textures::DefaultResourceID();
        }

        RHI::FManagedTexture Texture = RHI::Textures::Create(RHI::FTexture2DDesc
        {
            .Width  = Result->Dimensions.x,
            .Height = Result->Dimensions.y,
            .Format = Result->Format,
        });
        RHI::Textures::Upload(Texture, 0, Result->Pixels.data(), Result->Pixels.size(), Result->Dimensions.x);

        const uint32 ResourceID = Texture.ResourceID();
        PathTextures.insert_or_assign(NamePath, Move(Texture));
        return (ImTextureID)(uint32)ResourceID;
    }

}
