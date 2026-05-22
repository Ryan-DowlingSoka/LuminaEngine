#include "pch.h"
#include "VulkanPipelineCache.h"

#include "VulkanResources.h"
#include "Core/Profiler/Profile.h"

namespace Lumina
{
    // Pipeline compatibility (Vulkan dynamic rendering) depends only on attachment
    // formats, sample count and view mask -- NOT the actual images, load/store ops,
    // clear color or render area. Keying the cache on the full render pass desc minted
    // a fresh pipeline set per scene (new RTs => new image pointers), so the global
    // cache grew without bound. Mirror exactly the inputs vkCreateGraphicsPipelines reads.
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

        auto NewPipeline = TRefCountPtr<FVulkanGraphicsPipeline>::Create(Device, InDesc, RenderPassDesc);


        GraphicsPipelines.emplace(Hash, NewPipeline);
        return NewPipeline;
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
        }
        else
        {
            GraphicsPipelines.clear();
            ComputePipelines.clear();
        }
    }
    
    void FVulkanPipelineCache::ReleasePipelines()
    {
        FScopeLock Lock(ShaderMutex);

        GraphicsPipelines.clear();
        ComputePipelines.clear();
    }
}
