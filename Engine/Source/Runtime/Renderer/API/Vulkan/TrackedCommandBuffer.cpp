#include "pch.h"
#include "TrackedCommandBuffer.h"
#include "VulkanRenderContext.h"


namespace Lumina
{
    FTrackedCommandBuffer::FTrackedCommandBuffer(FVulkanDevice* InDevice, VkCommandBuffer InBuffer, VkCommandPool InPool, FQueue* InQueue)
        : IDeviceChild(InDevice)
        , CommandBuffer(InBuffer)
        , CommandPool(InPool)
        , Queue(InQueue)
    {
        // The Tracy GPU context lives on the FQueue (one per queue), not per command buffer.
    }

    FTrackedCommandBuffer::~FTrackedCommandBuffer()
    {
        vkDestroyCommandPool(Device->GetDevice(), CommandPool, VK_ALLOC_CALLBACK);
        CommandPool = VK_NULL_HANDLE;
    }

    void FTrackedCommandBuffer::AddReferencedResource(IRHIResource* InResource)
    {
        ReferencedResources.emplace_back(InResource);
    }

    void FTrackedCommandBuffer::AddStagingResource(FRHIBuffer* InResource)
    {
        ReferencedStagingResources.emplace_back(InResource);
    }

    void FTrackedCommandBuffer::ClearReferencedResources()
    {
        ReferencedResources.clear();
        ReferencedStagingResources.clear();
    }
}
