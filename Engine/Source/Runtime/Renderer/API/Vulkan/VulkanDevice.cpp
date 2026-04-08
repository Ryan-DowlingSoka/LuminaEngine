#include "pch.h"
#include "VulkanDevice.h"
#define VMA_IMPLEMENTATION
#include <volk/volk.h>
#include "vk_mem_alloc.h"
#include "Renderer/RHIGlobals.h"
#include "VulkanMacros.h"
#include "VulkanRenderContext.h"
#include "VulkanResources.h"
#include "Core/Profiler/Profile.h"


namespace Lumina
{
    constexpr uint64 DEDICATED_MEMORY_THRESHOLD = 2048llu * 2048;

    FVulkanMemoryAllocator::FVulkanMemoryAllocator(FVulkanRenderContext* InCxt, VkInstance Instance, VkPhysicalDevice PhysicalDevice, VkDevice Device)
    {
        VmaVulkanFunctions Functions = {};
        Functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        Functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    
        VmaAllocatorCreateInfo Info = {};
        Info.vulkanApiVersion = VK_API_VERSION_1_3;
        Info.instance = Instance;
        Info.physicalDevice = PhysicalDevice;
        Info.device = Device;
        Info.pVulkanFunctions = &Functions;
        Info.pAllocationCallbacks = VK_ALLOC_CALLBACK;
        
        Info.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT | VMA_ALLOCATOR_CREATE_EXT_MEMORY_PRIORITY_BIT | VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        
        VK_CHECK(vmaCreateAllocator(&Info, &Allocator));
        RenderContext = InCxt;
    }

    void FVulkanMemoryAllocator::Shutdown() const
    {
        vmaDestroyAllocator(Allocator);
    }

    VmaAllocation FVulkanMemoryAllocator::AllocateBuffer(const VkBufferCreateInfo* CreateInfo, VmaAllocationCreateFlags Flags, VkBuffer* vkBuffer, const char* AllocationName) const
    {
        LUMINA_PROFILE_SCOPE();
        
        VmaAllocationCreateInfo Info = {};
        Info.usage = VMA_MEMORY_USAGE_AUTO;
        Info.flags = Flags;
        
        if (CreateInfo->size > DEDICATED_MEMORY_THRESHOLD)
        {
            Info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            Info.priority = 1.0f;
        }
        
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
    
    VmaAllocation FVulkanMemoryAllocator::AllocateImage(const VkImageCreateInfo* CreateInfo, VmaAllocationCreateFlags Flags, VkImage* vkImage, const char* AllocationName) const
    {
        LUMINA_PROFILE_SCOPE();
    
        ASSERT(CreateInfo->extent.depth != 0);
    
        VmaAllocationCreateInfo Info = {};
        Info.usage = VMA_MEMORY_USAGE_AUTO;
        Info.flags = Flags;
        
        VkDeviceSize ImageSize = (uint64)CreateInfo->extent.width * CreateInfo->extent.height * CreateInfo->extent.depth * CreateInfo->arrayLayers;
        
        if (ImageSize > DEDICATED_MEMORY_THRESHOLD)
        {
            Info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            Info.priority = 0.75f;
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
        DEBUG_ASSERT(Buffer);
        
        vmaDestroyBuffer(Allocator, Buffer, Allocation);
    }
    
    void FVulkanMemoryAllocator::DestroyImage(VkImage Image, VmaAllocation Allocation) const
    {
        LUMINA_PROFILE_SCOPE();
        DEBUG_ASSERT(Image);
        
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
