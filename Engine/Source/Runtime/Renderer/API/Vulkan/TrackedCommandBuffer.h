#pragma once


#include "VulkanDevice.h"
#include <tracy/TracyVulkan.hpp>
#include "Memory/RefCounted.h"
#include "Renderer/RenderResource.h"


namespace Lumina
{
    class FVulkanDevice;
    class FQueue;

    class FTrackedCommandBuffer : public IRefCounted, public IDeviceChild
    {
    public:

        FTrackedCommandBuffer(FVulkanDevice* InDevice, VkCommandBuffer InBuffer, VkCommandPool InPool, FQueue* InQueue);
        ~FTrackedCommandBuffer() override;
        LE_NO_COPY(FTrackedCommandBuffer);
        LE_DEFAULT_MOVE(FTrackedCommandBuffer);

        void AddReferencedResource(IRHIResource* InResource);

        void AddStagingResource(FRHIBuffer* InResource);
        
        void ClearReferencedResources();

        VkCommandBuffer             CommandBuffer;
        VkCommandPool               CommandPool;
        FQueue*                     Queue;

        uint64                      SubmissionID = 0;
        uint64                      RecordingID = 0;
        
        TracyVkCtx                  TracyContext = nullptr;
        
        
        // Keeps resources alive for the command buffer's lifetime.
        TFixedVector<FRHIResourceRef, 100>       ReferencedResources;
        TFixedVector<FRHIBufferRef, 50>          ReferencedStagingResources;

    };
}
