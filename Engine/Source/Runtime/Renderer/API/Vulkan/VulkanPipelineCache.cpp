#include "pch.h"
#include "VulkanPipelineCache.h"

#include "VulkanResources.h"
#include "VulkanRenderContext.h"
#include "Renderer/RHIGlobals.h"
#include "Core/Profiler/Profile.h"

namespace Lumina
{
    // Zero dynamic-state fields so descs differing only in them map to one shared VkPipeline.
    static void CanonicalizeForDynamicState(FGraphicsPipelineDesc& Desc, const FDynamicPipelineStates& Dyn)
    {
        FRasterState&       Raster = Desc.RenderState.RasterState;
        FDepthStencilState& Depth  = Desc.RenderState.DepthStencilState;
        FBlendState&        Blend  = Desc.RenderState.BlendState;

        if (Dyn.bCullMode)         Raster.CullMode = ERasterCullMode::None;
        if (Dyn.bFrontFace)        Raster.FrontCounterClockwise = false;
        if (Dyn.bPolygonMode)      Raster.FillMode = ERasterFillMode::Solid;
        if (Dyn.bDepthTestEnable)  Depth.DepthTestEnable = false;
        if (Dyn.bDepthWriteEnable) Depth.DepthWriteEnable = false;
        if (Dyn.bDepthCompareOp)   Depth.DepthFunc = EComparisonFunc::Always;

        if (Dyn.bColorBlendEnable || Dyn.bColorBlendEquation || Dyn.bColorWriteMask)
        {
            for (uint32 i = 0; i < MaxRenderTargets; ++i)
            {
                FBlendState::RenderTarget& RT = Blend.Targets[i];
                if (Dyn.bColorBlendEnable)
                {
                    RT.bBlendEnable = false;
                }
                if (Dyn.bColorBlendEquation)
                {
                    RT.SrcBlend = EBlendFactor::One;  RT.DestBlend = EBlendFactor::Zero;  RT.BlendOp = EBlendOp::Add;
                    RT.SrcBlendAlpha = EBlendFactor::One;  RT.DestBlendAlpha = EBlendFactor::Zero;  RT.BlendOpAlpha = EBlendOp::Add;
                }
                if (Dyn.bColorWriteMask)
                {
                    RT.ColorWriteMask = EColorMask::All;
                }
            }
        }
    }

    static EFormat ResolveAttachmentFormat(const FRenderPassDesc::FAttachment& Attachment)
    {
        return Attachment.Format == EFormat::UNKNOWN
                   ? Attachment.Image->GetDescription().Format
                   : Attachment.Format;
    }

    static size_t HashRenderPassForPipeline(const FRenderPassDesc& Desc)
    {
        size_t Hash = 0;
        Hash::HashCombine(Hash, Desc.SampleCount);
        Hash::HashCombine(Hash, Desc.ViewMask);
        Hash::HashCombine(Hash, (uint32)Desc.ColorAttachments.size());
        for (const FRenderPassDesc::FAttachment& Attachment : Desc.ColorAttachments)
        {
            Hash::HashCombine(Hash, (uint32)ResolveAttachmentFormat(Attachment));
        }
        Hash::HashCombine(Hash, Desc.DepthAttachment.IsValid()
                                    ? (uint32)ResolveAttachmentFormat(Desc.DepthAttachment)
                                    : (uint32)EFormat::UNKNOWN);
        return Hash;
    }

    FRHIGraphicsPipeline* FVulkanPipelineCache::GetOrCreateGraphicsPipeline(FVulkanDevice* Device, const FGraphicsPipelineDesc& InDesc, const FRenderPassDesc& InRenderPassDesc)
    {
        LUMINA_PROFILE_SCOPE();

        FRenderPassDesc RenderPassDesc = InRenderPassDesc;
        RenderPassDesc.SampleCount = RenderPassDesc.DeriveSampleCount();

        size_t Hash = 0;
        Hash::HashCombine(Hash, InDesc);
        Hash::HashCombine(Hash, HashRenderPassForPipeline(RenderPassDesc));

        auto It = GraphicsPipelines.find(Hash);
        if (It != GraphicsPipelines.end())
        {
            return It->second;
        }

        auto NewPipeline = TRefCountPtr<FVulkanGraphicsPipeline>::Create(Device, InDesc, RenderPassDesc, this);


        GraphicsPipelines.emplace(Hash, NewPipeline);
        return NewPipeline;
    }

    VkPipeline FVulkanPipelineCache::GetOrCreateSharedVkPipeline(FVulkanDevice* Device, const FGraphicsPipelineDesc& InDesc, const FRenderPassDesc& InRenderPassDesc, VkPipelineLayout Layout, size_t& OutCanonicalHash)
    {
        LUMINA_PROFILE_SCOPE();

        FRenderPassDesc RenderPassDesc = InRenderPassDesc;
        RenderPassDesc.SampleCount = RenderPassDesc.DeriveSampleCount();

        const FDynamicPipelineStates& Dyn = static_cast<FVulkanRenderContext*>(GRenderContext)->GetDynamicPipelineStates();

        FGraphicsPipelineDesc CanonicalDesc = InDesc;
        CanonicalizeForDynamicState(CanonicalDesc, Dyn);

        size_t Hash = 0;
        Hash::HashCombine(Hash, CanonicalDesc);
        Hash::HashCombine(Hash, HashRenderPassForPipeline(RenderPassDesc));
        OutCanonicalHash = Hash;

        FScopeLock Lock(ShaderMutex);

        auto It = SharedGraphicsPipelines.find(Hash);
        if (It != SharedGraphicsPipelines.end())
        {
            return It->second.Pipeline;
        }

        VkPipeline NewVkPipeline = CreateGraphicsVkPipeline(Device, CanonicalDesc, RenderPassDesc, Layout, Dyn);
        SharedGraphicsPipelines.emplace(Hash, FSharedGraphicsPipeline{ NewVkPipeline, Device->GetDevice() });
        return NewVkPipeline;
    }

    void FVulkanPipelineCache::DestroyOrphanedSharedPipelines()
    {
        // A shared VkPipeline is live iff any RHI pipeline references its canonical hash.
        for (auto it = SharedGraphicsPipelines.begin(); it != SharedGraphicsPipelines.end(); )
        {
            bool bReferenced = false;
            for (auto& [OuterHash, Pipeline] : GraphicsPipelines)
            {
                if (static_cast<FVulkanGraphicsPipeline*>(Pipeline.GetReference())->GetSharedPipelineHash() == it->first)
                {
                    bReferenced = true;
                    break;
                }
            }

            if (!bReferenced)
            {
                vkDestroyPipeline(it->second.Device, it->second.Pipeline, VK_ALLOC_CALLBACK);
                it = SharedGraphicsPipelines.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    FRHIComputePipeline* FVulkanPipelineCache::GetOrCreateComputePipeline(FVulkanDevice* Device, const FComputePipelineDesc& InDesc)
    {
        LUMINA_PROFILE_SCOPE();

        size_t Hash = Hash::GetHash(InDesc);
        auto It = ComputePipelines.find(Hash);
        if (It != ComputePipelines.end())
        {
            return It->second;
        }
        
        auto NewPipeline = MakeRefCount<FVulkanComputePipeline>(Device, InDesc);

        FScopeLock Lock(ShaderMutex);
        ComputePipelines.emplace(Hash, NewPipeline);
        return NewPipeline;
    }

    void FVulkanPipelineCache::PostShaderRecompiled(const FRHIShader* Shader)
    {
        FScopeLock Lock(ShaderMutex);
        if (Shader)
        {
            const FString& ShaderName = Shader->GetShaderHeader().DebugName;

            for (auto it = GraphicsPipelines.begin(); it != GraphicsPipelines.end(); )
            {
                auto& Desc = it->second->GetDesc();

                bool bMatches =
                    (Desc.VS && Desc.VS->GetShaderHeader().DebugName == ShaderName) ||
                    (Desc.PS && Desc.PS->GetShaderHeader().DebugName == ShaderName) ||
                    (Desc.GS && Desc.GS->GetShaderHeader().DebugName == ShaderName);

                if (bMatches)
                {
                    it = GraphicsPipelines.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            for (auto it = ComputePipelines.begin(); it != ComputePipelines.end(); )
            {
                auto& Pipeline = it->second;
                if (Pipeline->GetDesc().CS && Pipeline->GetDesc().CS->GetShaderHeader().DebugName == ShaderName)
                {
                    it = ComputePipelines.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            // Drop any shared VkPipelines the erased graphics pipelines were the last to use.
            DestroyOrphanedSharedPipelines();
        }
        else
        {
            GraphicsPipelines.clear();
            ComputePipelines.clear();
            for (auto& [Hash, Pipeline] : SharedGraphicsPipelines)
            {
                vkDestroyPipeline(Pipeline.Device, Pipeline.Pipeline, VK_ALLOC_CALLBACK);
            }
            SharedGraphicsPipelines.clear();
        }
    }

    void FVulkanPipelineCache::ReleasePipelines()
    {
        FScopeLock Lock(ShaderMutex);

        GraphicsPipelines.clear();
        ComputePipelines.clear();
        for (auto& [Hash, Pipeline] : SharedGraphicsPipelines)
        {
            vkDestroyPipeline(Pipeline.Device, Pipeline.Pipeline, VK_ALLOC_CALLBACK);
        }
        SharedGraphicsPipelines.clear();
    }
}
