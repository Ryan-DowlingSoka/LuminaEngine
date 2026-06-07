#include "PCH.h"
#include <volk/volk.h>
#include "VulkanDevice.h"
#include "Renderer/RHI.h"
#include "Renderer/RHIGlobals.h"
#include "Renderer/API/Vulkan/VulkanRenderContext.h"

namespace Lumina::RHI
{
    static_assert(sizeof(GPUPtr) == sizeof(VkDeviceSize), "GPUPtr must be the same size as a vulkan device address");
    
    static constexpr VkPipelineStageFlags2 ToVkPipelineState(EStageFlags Flags)
    {
        VkPipelineStageFlags2 Out = 0;
        
        Out |= EnumHasAnyFlags(Flags, EStageFlags::IndirectArguments) ? VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT : 0;
        Out |= EnumHasAnyFlags(Flags, EStageFlags::Transfer) ? VK_PIPELINE_STAGE_2_TRANSFER_BIT : 0;
        Out |= EnumHasAnyFlags(Flags, EStageFlags::Compute) ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT : 0;
        Out |= EnumHasAnyFlags(Flags, EStageFlags::RasterColorOut) ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT : 0;
        Out |= EnumHasAnyFlags(Flags, EStageFlags::PixelShader) ?  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT 
                                                                        | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT 
                                                                        | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT : 0;
        
        Out |= EnumHasAnyFlags(Flags, EStageFlags::FragmentTests) ? VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT : 0;
        Out |= EnumHasAnyFlags(Flags, EStageFlags::VertexShader) ? VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT : 0;
        Out |= EnumHasAnyFlags(Flags, EStageFlags::Host) ? VK_PIPELINE_STAGE_2_HOST_BIT : 0;
        
        return Out;
    }
    
    
    constexpr VkBufferUsageFlags kDefaultBufferUsages =
          VK_BUFFER_USAGE_INDEX_BUFFER_BIT 
        | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT 
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT 
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT 
        | VK_BUFFER_USAGE_TRANSFER_SRC_BIT 
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT 
        | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    
    
    struct FBuffer
    {
        VkBuffer        Buffer;
        VmaAllocation   Allocation;
        void*           Host;
        GPUPtr          Device;
    };
    
    struct FTexture
    {
        VkImage Image;
        VkImageView DefaultImageView = VK_NULL_HANDLE;
        VmaAllocation Allocation;
        VkImageViewType Type = VK_IMAGE_VIEW_TYPE_2D;
        EFormat Format;
        bool bSwapchainImage = false;
    };
    
    struct FTextureHeap
    {
        VkDescriptorSet DescriptorSet;
        FBitVector      SamplersBitset;
        FBitVector      SampledImagesBitset;
        FBitVector      RWImagesBitset;
        
        TVector<VkSampler>   Samplers;
        TVector<VkImageView> ImageViews;
        TVector<VkImageView> RWImageViews;
    };
    
    struct FSemaphore
    {
        VkSemaphore Semaphore;
        
        operator VkSemaphore() const { return Semaphore; }
    };
    
    struct FPipeline
    {
        VkPipeline Pipeline;
        
        operator VkPipeline() const { return Pipeline; }
    };
    
    struct FCommandList
    {
        VkCommandBuffer CommandBuffer;
        VkCommandPool   Pool;
    };
    
    struct FDeviceImpl
    {
        VkDevice                Device;
        VkPhysicalDevice        PhysicsDevice;
        VmaAllocator            Allocator;
        
        TVector<FBuffer>        Buffers;
        
        TSegmentMap<FSemaphore>     Semaphores;
        TSegmentMap<FPipeline>      Pipelines;
        TSegmentMap<FTexture>       Textures;
        TSegmentMap<FCommandList>   CommandLists;
        
        VkMemoryRequirements    MemoryRequirements;
        
        operator VkDevice() const           { return Device; }
        operator VkPhysicalDevice() const   { return PhysicsDevice; }
    };
    
    static FDeviceImpl* GDevice;
    
    
    
    void CreateDevice()
    {
        //@TODO Actual implementation.
        
        GDevice = new FDeviceImpl{};
        auto* VkCtx = static_cast<FVulkanRenderContext*>(GRenderContext);
        
        GDevice->Device = VkCtx->GetDevice()->GetDevice();
        GDevice->PhysicsDevice = VkCtx->GetDevice()->GetPhysicalDevice();
        GDevice->Allocator = VkCtx->GetDevice()->GetAllocator().GetVMA();
        
        GDevice->Semaphores.SetDtor([](FSemaphore* Semaphore)
        {
            vkDestroySemaphore(*GDevice, *Semaphore, nullptr);
        });
        
        GDevice->Pipelines.SetDtor([](FPipeline* Pipeline)
        {
           vkDestroyPipeline(*GDevice, *Pipeline, nullptr); 
        });
    }
    
    void FreeDevice()
    {
        //@TODO Actual cleanup.
        GDevice->Semaphores.Clear();
        GDevice->Pipelines.Clear();
        delete GDevice;
    }

    void WaitDeviceIdle()
    {
        vkDeviceWaitIdle(*GDevice);
    }

    void WaitSemaphore(FSemaphoreH Semaphore, uint64 Value)
    {
        VkSemaphore VulkanSemaphore = GDevice->Semaphores[Semaphore].Semaphore;
        
        VkSemaphoreWaitInfo WaitInfo
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .pNext = nullptr,
            .flags = 0,
            .semaphoreCount = 1,
            
            .pSemaphores = &VulkanSemaphore,
            .pValues = &Value
        };
        
        VK_CHECK(vkWaitSemaphores(*GDevice, &WaitInfo, UINT64_MAX));
    }

    GPUPtr Malloc(uint64 Size, uint64 Alignment, EMemoryType Type)
    {
        VmaAllocationCreateInfo Info = {};
        Info.usage = VMA_MEMORY_USAGE_AUTO;
        
        switch (Type)
        {
        case EMemoryType::CPUWrite:
            Info.flags  = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        case EMemoryType::CPURead:
            Info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        case EMemoryType::GPUOnly:
            Info.flags = 0;
            break;
        }
        
        // @TODO Query about memory alignment requirements.
        Size = (Size + Alignment - 1) & ~(Alignment - 1);
        
        VkBufferCreateInfo SampleInfo = {};
        SampleInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        SampleInfo.size   = Size;
        SampleInfo.usage  = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                          | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                          | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                          | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                          | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                          | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                          | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        SampleInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        VmaAllocation Allocation = nullptr;
        VmaAllocationInfo AllocationInfo;

        VkBuffer VulkanBuffer;
        VK_CHECK(vmaCreateBuffer(GDevice->Allocator, &SampleInfo, &Info, &VulkanBuffer, &Allocation, &AllocationInfo));
        
        VkBufferDeviceAddressInfo AddressInfo
        {
            .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .pNext  = nullptr,
            .buffer = VulkanBuffer,
        };

        GPUPtr Gpu = vkGetBufferDeviceAddress(*GDevice, &AddressInfo);
        
        auto It = eastl::lower_bound(GDevice->Buffers.begin(), GDevice->Buffers.end(), Gpu, [](const FBuffer& LHS, GPUPtr Value)
        {
            return LHS.Device < Value;
        });
        
        FBuffer Buffer
        {
            .Buffer     = VulkanBuffer,
            .Allocation = Allocation,
            .Host       = AllocationInfo.pMappedData,
            .Device     = Gpu
        };
        
        GDevice->Buffers.insert(It, Buffer);
        
        return Buffer.Device;
    }

    GPUPtr Malloc(uint64 Size, EMemoryType Type)
    {
        return Malloc(Size, 16, Type);
    }

    void* ToHost(GPUPtr GPU)
    {
        auto It = eastl::lower_bound(GDevice->Buffers.begin(), GDevice->Buffers.end(), GPU, [](const FBuffer& b, GPUPtr value)
        {
            return b.Device < value;
        });
        
        if (It != GDevice->Buffers.end() && It->Device == GPU)
        {
            return It->Host;
        }
        
        return nullptr;
    }

    void Free(GPUPtr GPU)
    {
        auto It = eastl::lower_bound(GDevice->Buffers.begin(), GDevice->Buffers.end(), GPU, [](const FBuffer& b, GPUPtr value)
        {
            return b.Device < value;
        });

        if (It != GDevice->Buffers.end() && It->Device == GPU)
        {
            vmaDestroyBuffer(GDevice->Allocator, It->Buffer, It->Allocation);
            GDevice->Buffers.erase(It);
        }
    }

    void Free(FSemaphoreH Handle)
    {
        GDevice->Semaphores.Erase(Handle);
    }

    FPipelineH CreateGraphicsPipeline(const FShaderSource& Vertex, const FShaderSource& Fragment, const FRasterDesc& Desc)
    {
        constexpr VkDynamicState DynamicStates[] = 
        {
            VK_DYNAMIC_STATE_LINE_WIDTH, // prob temp.
            VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
            VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
            VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
            VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE,
            VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
            VK_DYNAMIC_STATE_STENCIL_OP,
            VK_DYNAMIC_STATE_DEPTH_BOUNDS,
            VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
            VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
            VK_DYNAMIC_STATE_CULL_MODE,
            VK_DYNAMIC_STATE_FRONT_FACE,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
        };

        
        return {};
    }

    FPipelineH CreateComputePipeline(const FShaderSource& Compute)
    {
        return {};
    }

    FSemaphoreH CreateSemaphore(uint64 Value)
    {
        VkSemaphoreTypeCreateInfo TypeInfo
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .pNext = nullptr,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = Value
        };
        
        VkSemaphoreCreateInfo Info
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &TypeInfo,
            .flags = 0
        };
        
        VkSemaphore Semaphore;
        vkCreateSemaphore(*GDevice, &Info, nullptr, &Semaphore);
        
        return GDevice->Semaphores.Emplace(Semaphore);
    }

    FTextureH CreateTexture(const FTextureDesc& Desc, GPUPtr Location)
    {
        return {};
    }

    FTextureHeapH CreateTextureHeap(uint32 TextureCount, uint32 RWTextureCount, uint32 SamplerCount)
    {
        return {};
    }

    FCmdListH OpenCommandList(EQueueType Type)
    { 
        VkCommandPoolCreateInfo Info
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = 0
        };
        
        VkCommandPool Pool = VK_NULL_HANDLE;
        VK_CHECK(vkCreateCommandPool(*GDevice, &Info, nullptr, &Pool));
        
        VkCommandBufferAllocateInfo BufferInfo = {};
        BufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        BufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        BufferInfo.commandBufferCount = 1;
        BufferInfo.commandPool = Pool;
        
        VkCommandBuffer Buffer = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateCommandBuffers(*GDevice, &BufferInfo, &Buffer));
        
        FCommandList CmdList
        {
            .CommandBuffer = Buffer,
            .Pool = Pool
        };
        
        return GDevice->CommandLists.Emplace(CmdList);
    }

    void CloseCommandList(FCmdListH CL)
    {
    }

    void Submit(TSpan<FCmdListH> CommandLists)
    {
    }

    void CmdMemcpy(FCmdListH CL, GPUPtr Dest, GPUPtr Source)
    {
    }

    void CmdBarrier(FCmdListH CL, EStageFlags Before, EStageFlags After)
    {
        const VkPipelineStageFlags2 SrcStage = ToVkPipelineState(Before);
        const VkPipelineStageFlags2 DstStage = ToVkPipelineState(After);
        
        constexpr auto Access = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
        
        VkMemoryBarrier2 BarrierInfo
        {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = SrcStage,
            .srcAccessMask = Access,
            .dstStageMask = DstStage,
            .dstAccessMask = Access
        };
        
        VkDependencyInfo DepInfo
        {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &BarrierInfo,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = 0,
            .pImageMemoryBarriers = nullptr
        };
        
        vkCmdPipelineBarrier2(CL->CommandBuffer, &DepInfo);
    }

    void CmdBeginRenderPass(FCmdListH CL, const FRenderPassDesc& Desc)
    {
    }

    void CmdEndRenderPass(FCmdListH CL)
    {
    }

    void CmdSetFrontFace(FCmdListH CL, EFrontFace Front)
    {
    }

    void CmdSetCullMode(FCmdListH CL, ECullMode Mode)
    {
    }

    void CmdSetPipeline(FCmdListH CL, FPipelineH Pipeline)
    {
    }

    void CmdSetScissor(FCmdListH CL, const FRect& Rect)
    {
    }

    void CmdSetViewport(FCmdListH CL, const FRect& Rect)
    {
    }

    void CmdDispatch(FCmdListH CL, GPUPtr DrawArgs, uint32 GroupX, uint32 GroupY, uint32 GroupZ)
    {
    }

    void CmdDraw(FCmdListH CL, GPUPtr DrawArgs, uint32 VertexCount, uint32 InstanceCount, uint32 FirstVertex, uint32 FirstInstance)
    {
    }

    void CmdDrawIndexed(FCmdListH CL, GPUPtr DrawArgs, uint32 IndexCount, uint32 InstanceCount, uint32 FirstIndex, uint32 VertexOffset, uint32 FirstInstance)
    {
    }

    void CmdDrawIndirect(FCmdListH CL, GPUPtr DrawArgs, uint32 Offset, uint32 DrawCount, uint32 Stride)
    {
    }

    void CmdDrawIndexedIndirect(FCmdListH CL, GPUPtr DrawArgs, uint32 Offset, uint32 DrawCount, uint32 Stride)
    {
    }
}
