#include "pch.h"
#include "VulkanDevice.h"
#define VMA_IMPLEMENTATION
#include <volk/volk.h>
#include "vk_mem_alloc.h"
#include "VulkanMacros.h"
#include "VulkanRenderContext.h"
#include "VulkanResources.h"
#include "Core/Profiler/Profile.h"
#include "Memory/MemoryTracking.h"
#include "Renderer/RHIGlobals.h"
#include "TaskSystem/TaskSystem.h"


namespace Lumina
{
    constexpr uint64 DEDICATED_MEMORY_THRESHOLD = 2048llu * 2048;

    // 32 MiB block: 128 MiB OOMed on the 256 MiB BAR heap (no Resizable BAR).
    constexpr VkDeviceSize UPLOAD_POOL_BLOCK_SIZE = 32llu * 1024 * 1024;

    FVulkanMemoryAllocator::FVulkanMemoryAllocator(FVulkanRenderContext* InCxt, VkInstance Instance, VkPhysicalDevice PhysicalDevice, VkDevice Device)
        : RenderContext(InCxt)
    {
        VmaVulkanFunctions Functions = {};
        Functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        Functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo Info = {};
        Info.vulkanApiVersion       = VK_API_VERSION_1_4;
        Info.instance               = Instance;
        Info.physicalDevice         = PhysicalDevice;
        Info.device                 = Device;
        Info.pVulkanFunctions       = &Functions;
        Info.pAllocationCallbacks   = VK_ALLOC_CALLBACK;

        Info.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT | VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

        // The priority bit is only valid when VK_EXT_memory_priority was enabled on the device;
        // setting it otherwise is undefined per VMA. Pairs with VK_EXT_pageable_device_local_memory.
        if (RenderContext->SupportsMemoryPriority())
        {
            Info.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_PRIORITY_BIT;
        }

        VK_CHECK(vmaCreateAllocator(&Info, &Allocator));

        InitUploadPool();
    }

    void FVulkanMemoryAllocator::InitUploadPool()
    {
        // Sample with all upload-chunk usage flags so VMA picks a single compatible memory type.
        VkBufferCreateInfo SampleInfo = {};
        SampleInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        SampleInfo.size   = 1024;
        SampleInfo.usage  = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                          | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                          | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                          | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                          | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                          | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                          | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        SampleInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo SampleAlloc = {};
        SampleAlloc.usage = VMA_MEMORY_USAGE_AUTO;
        SampleAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        uint32_t MemoryTypeIndex = UINT32_MAX;
        if (vmaFindMemoryTypeIndexForBufferInfo(Allocator, &SampleInfo, &SampleAlloc, &MemoryTypeIndex) != VK_SUCCESS)
        {
            LOG_WARN("FVulkanMemoryAllocator: failed to find memory type for upload pool, falling back to default allocator for upload chunks.");
            return;
        }

        UploadBlockSize = UPLOAD_POOL_BLOCK_SIZE;

        VmaPoolCreateInfo PoolInfo = {};
        PoolInfo.memoryTypeIndex = MemoryTypeIndex;
        // TLSF (flags=0), NOT linear: deferred/out-of-order chunk frees stall the linear ring front → unbounded block growth.
        PoolInfo.flags          = 0;
        PoolInfo.blockSize      = UploadBlockSize;
        // Lazy: first upload triggers first block. Pre-warming OOMed startup on small BAR heaps.
        PoolInfo.minBlockCount  = 0;
        PoolInfo.maxBlockCount  = 0;

        const VkResult PoolResult = vmaCreatePool(Allocator, &PoolInfo, &UploadPool);
        if (PoolResult != VK_SUCCESS)
        {
            // Default allocator fallback works but is slower under sustained upload load.
            LOG_WARN("FVulkanMemoryAllocator: vmaCreatePool failed (VkResult={}, memoryTypeIndex={}, blockSize={} MiB); falling back to default allocator for upload chunks.",
                (int)PoolResult, MemoryTypeIndex, UploadBlockSize / (1024 * 1024));
            UploadPool = VK_NULL_HANDLE;
            UploadBlockSize = 0;
        }
    }

    void FVulkanMemoryAllocator::Shutdown()
    {
        GTaskSystem->WaitForAll();
        if (UploadPool != VK_NULL_HANDLE)
        {
            vmaDestroyPool(Allocator, UploadPool);
            UploadPool = VK_NULL_HANDLE;
        }
        vmaDestroyAllocator(Allocator);
    }

    VmaAllocation FVulkanMemoryAllocator::AllocateBuffer(const VkBufferCreateInfo* CreateInfo, VmaAllocationCreateFlags Flags, VkBuffer* vkBuffer, const char* AllocationName) const
    {
        LUMINA_PROFILE_SCOPE();
        LUMINA_PROFILE_TAG(std::format("Size: {}", StringUtils::FormatSize(CreateInfo->size)).c_str());
        LUMINA_MEMORY_SCOPE("VmaBuffers");

        VmaAllocationCreateInfo Info = {};
        Info.usage = VMA_MEMORY_USAGE_AUTO;
        Info.flags = Flags | VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT;

        if (Flags & VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT)
        {
            Info.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }

        VmaAllocation Allocation = nullptr;
        VmaAllocationInfo AllocationInfo;

        VK_CHECK(vmaCreateBuffer(Allocator, CreateInfo, &Info, vkBuffer, &Allocation, &AllocationInfo));
        DEBUG_ASSERT(Allocation, "Vulkan failed to allocate buffer memory!");

    #if LE_DEBUG
        if (AllocationName)
        {
            vmaSetAllocationName(Allocator, Allocation, AllocationName);
        }
    #endif

        return Allocation;
    }

    VmaAllocation FVulkanMemoryAllocator::AllocateUploadBuffer(const VkBufferCreateInfo* CreateInfo, VkBuffer* vkBuffer, const char* AllocationName) const
    {
        LUMINA_PROFILE_SCOPE();
        LUMINA_PROFILE_TAG(std::format("UploadSize: {}", StringUtils::FormatSize(CreateInfo->size)).c_str());
        LUMINA_MEMORY_SCOPE("VmaBuffers");

        VmaAllocation Allocation     = nullptr;
        VmaAllocationInfo AllocInfo  = {};
        VkResult Result              = VK_ERROR_OUT_OF_DEVICE_MEMORY;

        // Linear pool fast path; only if the request fits in a block.
        if (UploadPool != VK_NULL_HANDLE && CreateInfo->size <= UploadBlockSize)
        {
            VmaAllocationCreateInfo Info = {};
            Info.pool  = UploadPool;
            Info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            Result = vmaCreateBuffer(Allocator, CreateInfo, &Info, vkBuffer, &Allocation, &AllocInfo);
        }

        if (Result != VK_SUCCESS)
        {
            VmaAllocationCreateInfo Info = {};
            Info.usage = VMA_MEMORY_USAGE_AUTO;
            Info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                       | VMA_ALLOCATION_CREATE_MAPPED_BIT
                       | VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT
                       | VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT;
            VK_CHECK(vmaCreateBuffer(Allocator, CreateInfo, &Info, vkBuffer, &Allocation, &AllocInfo));
        }

        DEBUG_ASSERT(Allocation, "Vulkan failed to allocate upload buffer memory!");

    #if LE_DEBUG
        if (AllocationName)
        {
            vmaSetAllocationName(Allocator, Allocation, AllocationName);
        }
    #endif

        return Allocation;
    }
    
    VmaAllocation FVulkanMemoryAllocator::AllocateImage(const VkImageCreateInfo* CreateInfo, VmaAllocationCreateFlags Flags, VkImage* vkImage, const char* AllocationName) const
    {
        LUMINA_PROFILE_SCOPE();
        LUMINA_MEMORY_SCOPE("VmaImages");

        ASSERT(CreateInfo->extent.depth != 0);
    
        VmaAllocationCreateInfo Info = {};
        Info.usage = VMA_MEMORY_USAGE_AUTO;
        Info.flags = Flags | VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT;

        VkDeviceSize ImageSize = (uint64)CreateInfo->extent.width * CreateInfo->extent.height * CreateInfo->extent.depth * CreateInfo->arrayLayers;

        if (ImageSize > DEDICATED_MEMORY_THRESHOLD)
        {
            Info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            Info.priority = 1.00f;
        }
    
        VmaAllocation Allocation;
        VmaAllocationInfo AllocationInfo;
        
        VK_CHECK(vmaCreateImage(Allocator, CreateInfo, &Info, vkImage, &Allocation, &AllocationInfo));
        DEBUG_ASSERT(Allocation, "Vulkan failed to allocate image memory!");
    
    #if LE_DEBUG
        if (AllocationName && strlen(AllocationName) > 0)
        {
            vmaSetAllocationName(Allocator, Allocation, AllocationName);
        }
    #endif
        
        
        return Allocation;
    }
    
    void FVulkanMemoryAllocator::DestroyBuffer(VkBuffer Buffer, VmaAllocation Allocation) const
    {
        LUMINA_PROFILE_SCOPE();
        vmaDestroyBuffer(Allocator, Buffer, Allocation);
    }
    
    void FVulkanMemoryAllocator::DestroyImage(VkImage Image, VmaAllocation Allocation) const
    {
        LUMINA_PROFILE_SCOPE();
        vmaDestroyImage(Allocator, Image, Allocation);
    }
    
    void* FVulkanMemoryAllocator::GetMappedMemory(const FVulkanBuffer* Buffer) const
    {
        LUMINA_PROFILE_SCOPE();
        
        if (Buffer->LastUseCommandListID != 0)
        {
            FQueue* Queue = RenderContext->GetQueue(Buffer->LastUseQueue);
            Queue->WaitCommandList(Buffer->LastUseCommandListID, UINT64_MAX);
        }

        return Buffer->GetAllocation()->GetMappedData();
    }

    FVulkanDevice::FVulkanDevice(FVulkanRenderContext* RenderContext, VkInstance Instance, VkPhysicalDevice InPhysicalDevice, VkDevice InDevice)
        : Allocator(RenderContext, Instance, InPhysicalDevice, InDevice)
        , PhysicalDevice(InPhysicalDevice)
        , Device(InDevice)
    {
        vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &PhysicalDeviceMemoryProperties);
        vkGetPhysicalDeviceProperties(PhysicalDevice, &PhysicalDeviceProperties);

        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

        Features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        Features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        Features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;

        features2.pNext = &Features11;
        Features11.pNext = &Features12;
        Features12.pNext = &Features13;

        vkGetPhysicalDeviceFeatures2(PhysicalDevice, &features2);

        Features10 = features2.features;
    }

    FVulkanDevice::~FVulkanDevice()
    {
        Allocator.Shutdown();
        vkDestroyDevice(Device, nullptr);
    }

    void FVulkanMemoryAllocator::GetMemoryBudget(VmaBudget* OutBudgets)
    {
        vmaGetHeapBudgets(Allocator, OutBudgets);
    }
    
    void FVulkanMemoryAllocator::LogMemoryStats()
    {
        VmaTotalStatistics Stats;
        vmaCalculateStatistics(Allocator, &Stats);
        
        LOG_INFO("=== Vulkan Memory Statistics ===");
        LOG_INFO("Total Allocated: %.2f MB", Stats.total.statistics.allocationBytes / (1024.0f * 1024.0f));
        LOG_INFO("Total Block Count: %u", Stats.total.statistics.blockCount);
        LOG_INFO("Allocation Count: %u", Stats.total.statistics.allocationCount);
    }

}
