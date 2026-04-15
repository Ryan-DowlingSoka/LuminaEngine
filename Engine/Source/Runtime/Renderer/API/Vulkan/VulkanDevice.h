#pragma once

#include <volk/volk.h>
#include "vk_mem_alloc.h"
#include "Core/LuminaMacros.h"
#include "Core/Threading/Thread.h"
#include "Memory/Memory.h"

namespace Lumina
{
    class ICommandList;
    class FVulkanBuffer;
    class FRHIBuffer;
    class IDeviceChild;
    class FVulkanRenderContext;
}

namespace Lumina
{

    class FVulkanMemoryAllocator
    {
    public:

        FVulkanMemoryAllocator(FVulkanRenderContext* InCxt, VkInstance Instance, VkPhysicalDevice PhysicalDevice, VkDevice Device);
        ~FVulkanMemoryAllocator() = default;
        LE_NO_COPYMOVE(FVulkanMemoryAllocator);
        
        void Shutdown();
        void GetMemoryBudget(VmaBudget* OutBudgets);
        void LogMemoryStats();

        VmaAllocation AllocateBuffer(const VkBufferCreateInfo* CreateInfo, VmaAllocationCreateFlags Flags, VkBuffer* vkBuffer, const char* AllocationName) const;
        VmaAllocation AllocateImage(const VkImageCreateInfo* CreateInfo, VmaAllocationCreateFlags Flags, VkImage* vkImage, const char* AllocationName) const;

        // Fast-path allocation for transient/upload buffers. Uses a dedicated linear-algorithm pool
        // so allocations are O(1) bump-pointer and never fragment. Falls back to a dedicated allocation
        // if the buffer is larger than the pool block size.
        VmaAllocation AllocateUploadBuffer(const VkBufferCreateInfo* CreateInfo, VkBuffer* vkBuffer, const char* AllocationName) const;

        VmaAllocator GetVMA() const { return Allocator; }

        void DestroyBuffer(VkBuffer Buffer, VmaAllocation Allocation) const;
        void DestroyImage(VkImage Image, VmaAllocation Allocation) const;

        /** All buffers created with VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT will have their memory persistently mapped *
         * https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html
         * */
        void* GetMappedMemory(const FVulkanBuffer* Buffer) const;

    
    private:

        void InitUploadPool();

        VmaAllocator           Allocator       = nullptr;
        VmaPool                UploadPool      = VK_NULL_HANDLE;
        VkDeviceSize           UploadBlockSize = 0;
        FVulkanRenderContext*  RenderContext   = nullptr;
    };
    
    class FVulkanDevice
    {
    public:
        FVulkanDevice(FVulkanRenderContext* RenderContext, VkInstance Instance, VkPhysicalDevice InPhysicalDevice, VkDevice InDevice);
        virtual ~FVulkanDevice();
        LE_NO_COPYMOVE(FVulkanDevice);

        NODISCARD FVulkanMemoryAllocator& GetAllocator() { return Allocator; }
        NODISCARD VkPhysicalDevice GetPhysicalDevice() const { return PhysicalDevice; }
        NODISCARD VkDevice GetDevice() const { return Device; }
        
        NODISCARD const VkPhysicalDeviceFeatures& GetFeatures10() const { return Features10; }
        NODISCARD const VkPhysicalDeviceVulkan11Features& GetFeatures11() const { return Features11; }
        NODISCARD const VkPhysicalDeviceVulkan12Features& GetFeatures12() const { return Features12; }
        NODISCARD const VkPhysicalDeviceVulkan13Features& GetFeatures13() const { return Features13; }
        
        NODISCARD VkPhysicalDeviceProperties GetPhysicalDeviceProperties() const { return PhysicalDeviceProperties; }
        NODISCARD VkPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties() const { return PhysicalDeviceMemoryProperties; }
    
    private:

        FMutex                                  ChildMutex;
        FVulkanMemoryAllocator                  Allocator;
        VkPhysicalDevice                        PhysicalDevice;
        VkDevice                                Device;

        VkPhysicalDeviceProperties              PhysicalDeviceProperties;
        VkPhysicalDeviceMemoryProperties        PhysicalDeviceMemoryProperties;
        
        VkPhysicalDeviceFeatures                Features10{};
        VkPhysicalDeviceVulkan11Features        Features11{};
        VkPhysicalDeviceVulkan12Features        Features12{};
        VkPhysicalDeviceVulkan13Features        Features13{};
    };

    class IDeviceChild
    {
    public:

        IDeviceChild(FVulkanDevice* InDevice)
            :Device(InDevice)
        {}
        
        FVulkanDevice* Device = nullptr;
    };
    
}
