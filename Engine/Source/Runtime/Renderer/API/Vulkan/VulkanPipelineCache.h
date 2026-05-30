#pragma once
#include <volk/volk.h>
#include "Containers/Array.h"
#include "Containers/Name.h"
#include "Core/Threading/Thread.h"
#include "Renderer/RenderResource.h"


namespace Lumina
{
    struct FRenderPassDesc;
    class IVulkanShader;
    struct FComputePipelineDesc;
    struct FGraphicsPipelineDesc;
    class FVulkanDevice;
}


namespace Lumina
{

    class FVulkanPipelineCache
    {
    public:

        struct FShaderPipelineTracker
        {
            TVector<FName> Shaders;
        };

        FRHIGraphicsPipeline* GetOrCreateGraphicsPipeline(FVulkanDevice* Device, const FGraphicsPipelineDesc& InDesc, const FRenderPassDesc& RenderPassDesc);
        FRHIComputePipeline* GetOrCreateComputePipeline(FVulkanDevice* Device, const FComputePipelineDesc& InDesc);

        // VkPipeline shared by all RHI pipelines whose desc canonicalizes identically (dynamic fields zeroed);
        // cache owns its lifetime. OutCanonicalHash identifies it for invalidation bookkeeping.
        VkPipeline GetOrCreateSharedVkPipeline(FVulkanDevice* Device, const FGraphicsPipelineDesc& InDesc, const FRenderPassDesc& RenderPassDesc, VkPipelineLayout Layout, size_t& OutCanonicalHash);

        void PostShaderRecompiled(const FRHIShader* Shader);
        void ReleasePipelines();

        // Diagnostics: a count that climbs every frame means a call site is varying the
        // pipeline-desc hash and minting a new pipeline per frame instead of hitting the cache.
        uint32 GetGraphicsPipelineCount() const { return (uint32)GraphicsPipelines.size(); }
        uint32 GetComputePipelineCount()  const { return (uint32)ComputePipelines.size(); }
        // The actual VkPipeline (PSO) count after dynamic-state collapsing -- <= the RHI
        // pipeline-object count above when blend/cull/depth/polygon variants are merged.
        uint32 GetSharedVkPipelineCount() const { return (uint32)SharedGraphicsPipelines.size(); }

    private:

        struct FSharedGraphicsPipeline
        {
            VkPipeline Pipeline = VK_NULL_HANDLE;
            VkDevice   Device    = VK_NULL_HANDLE;
        };

        FMutex ShaderMutex;
        THashMap<size_t, FRHIGraphicsPipelineRef>   GraphicsPipelines;
        THashMap<size_t, FRHIComputePipelineRef>    ComputePipelines;
        // Canonical-desc hash -> shared VkPipeline. Many GraphicsPipelines entries (which
        // differ only in dynamic state) point at one entry here.
        THashMap<size_t, FSharedGraphicsPipeline>   SharedGraphicsPipelines;

        void DestroyOrphanedSharedPipelines();

    };
}
