#pragma once
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

        void PostShaderRecompiled(const FRHIShader* Shader);
        void ReleasePipelines();

        // Diagnostics: a count that climbs every frame means a call site is varying the
        // pipeline-desc hash and minting a new pipeline per frame instead of hitting the cache.
        uint32 GetGraphicsPipelineCount() const { return (uint32)GraphicsPipelines.size(); }
        uint32 GetComputePipelineCount()  const { return (uint32)ComputePipelines.size(); }

    private:

        FMutex ShaderMutex;
        THashMap<size_t, FRHIGraphicsPipelineRef>   GraphicsPipelines;
        THashMap<size_t, FRHIComputePipelineRef>    ComputePipelines;
        
    };
}
