#include "pch.h"
#include "VulkanRenderContext.h"
#include <filesystem>
#define VOLK_IMPLEMENTATION
#include <volk/volk.h>
#include "Convert.h"
#include "VulkanCommandList.h"
#include "VulkanDevice.h"
#include "VulkanMacros.h"
#include "VulkanResources.h"
#include "VulkanSwapchain.h"
#include "Core/Engine/Engine.h"
#include "Core/Math/Alignment.h"
#include "Core/Profiler/Profile.h"
#include "Core/Windows/Window.h"
#include "FileSystem/FileSystem.h"
#include "Paths/Paths.h"
#include "Renderer/CommandList.h"
#include "Renderer/CommandListValidator.h"
#include "Renderer/RHIStaticStates.h"
#include "Renderer/ShaderCompiler.h"
#include "TaskSystem/TaskSystem.h"
#include "Renderer/ErrorHandling/Vulkan/VulkanCrashTracker.h"
#include "Tools/Dialogs/Dialogs.h"

namespace Lumina
{
    VkAllocationCallbacks GVulkanAllocationCallbacks;
    
    static void* VulkanAlloc(void* pUserData, size_t size, size_t alignment, VkSystemAllocationScope allocationScope)
    {
        return Memory::Malloc(size, alignment);
    }

    static void VulkanFree(void* pUserData, void* pMemory)
    {
        Memory::Free(pMemory);
    }
    
    static void* VulkanRealloc(void* pUserData, void* pMemory, size_t size, size_t alignment, VkSystemAllocationScope allocationScope)
    {
        return Memory::Realloc(pMemory, size, alignment);
    }
    
    VkBool32 VKAPI_PTR VkDebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageTypes,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData)
    {
        if (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
        {
            return VK_FALSE;
        }
        
        auto GetMessageTypeString = [](VkDebugUtilsMessageTypeFlagsEXT types) -> FFixedString
        {
            FFixedString Result;
            if (types & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
            {
                Result += "[General] ";
            }
            if (types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
            {
                Result += "[Validation] ";
            }
            if (types & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
            {
                Result += "[Performance] ";
            }
            return Result.empty() ? "[Unknown] " : Result;
        };

        FStringView StringView = GetMessageTypeString(messageTypes);

        switch (messageSeverity)
        {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            LOG_TRACE("Vulkan {}{}", StringView, pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            LOG_DEBUG("Vulkan {}{}", StringView, pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            LOG_WARN("Vulkan {}{}", StringView, pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            LOG_ERROR("Vulkan {}{}", StringView, pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT:
            UNREACHABLE();
        }

        return VK_FALSE;
    }

        constexpr VkObjectType ToVkObjectType(EAPIResourceType type)
    {
        switch (type)
        {
        case EAPIResourceType::Buffer: return VK_OBJECT_TYPE_BUFFER;
        case EAPIResourceType::Image: return VK_OBJECT_TYPE_IMAGE;
        case EAPIResourceType::ImageView: return VK_OBJECT_TYPE_IMAGE_VIEW;
        case EAPIResourceType::Sampler: return VK_OBJECT_TYPE_SAMPLER;
        case EAPIResourceType::ShaderModule: return VK_OBJECT_TYPE_SHADER_MODULE;
        case EAPIResourceType::Pipeline: return VK_OBJECT_TYPE_PIPELINE;
        case EAPIResourceType::PipelineLayout: return VK_OBJECT_TYPE_PIPELINE_LAYOUT;
        case EAPIResourceType::RenderPass: return VK_OBJECT_TYPE_RENDER_PASS;
        case EAPIResourceType::Framebuffer: return VK_OBJECT_TYPE_FRAMEBUFFER;
        case EAPIResourceType::DescriptorSet: return VK_OBJECT_TYPE_DESCRIPTOR_SET;
        case EAPIResourceType::DescriptorSetLayout: return VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
        case EAPIResourceType::DescriptorPool: return VK_OBJECT_TYPE_DESCRIPTOR_POOL;
        case EAPIResourceType::CommandPool: return VK_OBJECT_TYPE_COMMAND_POOL;
        case EAPIResourceType::CommandBuffer: return VK_OBJECT_TYPE_COMMAND_BUFFER;
        case EAPIResourceType::Semaphore: return VK_OBJECT_TYPE_SEMAPHORE;
        case EAPIResourceType::Fence: return VK_OBJECT_TYPE_FENCE;
        case EAPIResourceType::Event: return VK_OBJECT_TYPE_EVENT;
        case EAPIResourceType::QueryPool: return VK_OBJECT_TYPE_QUERY_POOL;
        case EAPIResourceType::DeviceMemory: return VK_OBJECT_TYPE_DEVICE_MEMORY;
        case EAPIResourceType::Swapchain: return VK_OBJECT_TYPE_SWAPCHAIN_KHR;
        case EAPIResourceType::Surface: return VK_OBJECT_TYPE_SURFACE_KHR;
        case EAPIResourceType::Device: return VK_OBJECT_TYPE_DEVICE;
        case EAPIResourceType::Instance: return VK_OBJECT_TYPE_INSTANCE;
        case EAPIResourceType::Queue: return VK_OBJECT_TYPE_QUEUE;
        default: return VK_OBJECT_TYPE_UNKNOWN;
        }
    }
    
    //------------------------------------------------------------------------------------


    VkImageAspectFlags GuessImageAspectFlags(VkFormat Format)
    {
        switch (Format)
        {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT:
            return VK_IMAGE_ASPECT_DEPTH_BIT;

        case VK_FORMAT_S8_UINT:
            return VK_IMAGE_ASPECT_STENCIL_BIT;

        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
        }
    }

    FQueue::FQueue(FVulkanRenderContext* InRenderContext, VkQueue InQueue, uint32 InQueueFamilyIndex, ECommandQueue InType)
        : IDeviceChild(InRenderContext->GetDevice())
        , RenderContext(InRenderContext)
        , Queue(InQueue)
        , Type(InType)
        , QueueFamilyIndex(InQueueFamilyIndex)
    {
        
        VkSemaphoreTypeCreateInfo TimelineInfo = {};
        TimelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        TimelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        TimelineInfo.initialValue = 0;

        VkSemaphoreCreateInfo CreateInfo = {};
        CreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        CreateInfo.pNext = &TimelineInfo;

        VK_CHECK(vkCreateSemaphore(Device->GetDevice(), &CreateInfo, VK_ALLOC_CALLBACK, &TimelineSemaphore));
        RenderContext->SetVulkanObjectName("Timeline Semaphore", VK_OBJECT_TYPE_SEMAPHORE, (uintptr_t)TimelineSemaphore);
    }
    
    FQueue::~FQueue()
    {
        vkDestroySemaphore(Device->GetDevice(), TimelineSemaphore, VK_ALLOC_CALLBACK);
        TimelineSemaphore = nullptr;
    }

    TRefCountPtr<FTrackedCommandBuffer> FQueue::GetOrCreateCommandBuffer()
    {
        LUMINA_PROFILE_SCOPE();

        LockMark(Mutex);
        uint64 RecodingID = LastRecordingID.fetch_add(1, std::memory_order_relaxed) + 1;

        TRefCountPtr<FTrackedCommandBuffer> TrackedBuffer;
        if (!CommandBufferPool.try_dequeue(TrackedBuffer))
        {
            LUMINA_PROFILE_SECTION_COLORED("vkCreateCommandPool", tracy::Color::DarkRed);

            VkCommandPoolCreateFlags Flags = 0;
            Flags |= VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

            VkCommandPoolCreateInfo PoolInfo = {};
            PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            PoolInfo.queueFamilyIndex = QueueFamilyIndex;
            PoolInfo.flags = Flags;
            
            VkCommandPool CommandPool = VK_NULL_HANDLE;
            VK_CHECK(vkCreateCommandPool(Device->GetDevice(), &PoolInfo, VK_ALLOC_CALLBACK, &CommandPool));
            DEBUG_ASSERT(CommandPool);

            VkCommandBufferAllocateInfo BufferInfo = {};
            BufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            BufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            BufferInfo.commandBufferCount = 1;
            BufferInfo.commandPool = CommandPool;

            VkCommandBuffer Buffer;
            VK_CHECK(vkAllocateCommandBuffers(Device->GetDevice(), &BufferInfo, &Buffer));
            TrackedBuffer = MakeRefCount<FTrackedCommandBuffer>(Device, Buffer, CommandPool, this);
            TrackedBuffer->RecordingID = RecodingID;

            if (RenderContext->GetRenderContextDescription().bValidation)
            {
                const char* QueueName = "\0";
                switch (Type)
                {
                case ECommandQueue::Graphics:
                    QueueName = "Graphics";
                    break;
                case ECommandQueue::Compute:
                    QueueName = "Compute";
                    break;
                case ECommandQueue::Transfer:
                    QueueName = "Transfer";
                    break;
                default:
                    break;
                }
            
                FFixedString String;
                String.sprintf("CommandBuffer: %s", QueueName);
                RenderContext->SetVulkanObjectName(String.data(), VK_OBJECT_TYPE_COMMAND_BUFFER, (uintptr_t)Buffer);
            }
        }

        return TrackedBuffer;
    }

    void FQueue::RetireCommandBuffers()
    {
        LUMINA_PROFILE_SCOPE();

        uint64 LastFinish = UpdateLastFinishID();

        TFixedVector<TRefCountPtr<FTrackedCommandBuffer>, 16> ToEnqueue;

        // Two-pointer compaction: finished submissions go to ToEnqueue, unfinished stay packed.
        uint32 WriteIdx = 0;
        for (uint32 ReadIdx = 0, Count = (uint32)CommandBuffersInFlight.size(); ReadIdx < Count; ++ReadIdx)
        {
            TRefCountPtr<FTrackedCommandBuffer>& Slot = CommandBuffersInFlight[ReadIdx];
            if (Slot->SubmissionID <= LastFinish)
            {
                Slot->ClearReferencedResources();
                Slot->SubmissionID = 0;
                ToEnqueue.emplace_back(Move(Slot));
            }
            else
            {
                if (WriteIdx != ReadIdx)
                {
                    CommandBuffersInFlight[WriteIdx] = Move(Slot);
                }
                ++WriteIdx;
            }
        }

        CommandBuffersInFlight.resize(WriteIdx);

        if (!ToEnqueue.empty())
        {
            CommandBufferPool.enqueue_bulk(ToEnqueue.begin(), ToEnqueue.size());
        }
    }

    uint64 FQueue::GetCompletedInstance() const
    {
        uint64 CompletedInstance;
        VK_CHECK(vkGetSemaphoreCounterValue(Device->GetDevice(), TimelineSemaphore, &CompletedInstance));
        
        return CompletedInstance;
    }

    uint64 FQueue::Submit(ICommandList* const* CommandLists, uint32 NumCommandLists)
    {
        LUMINA_PROFILE_SCOPE();
        
        std::scoped_lock Lock(Mutex);
        LockMark(Mutex);
        LastSubmittedID++;

#if TRACY_ENABLE
        {
            char TagBuf[96];
            auto Result = std::format_to_n(TagBuf, sizeof(TagBuf) - 1, "Submit ID: {} - CLs: {}", LastSubmittedID, NumCommandLists);
            *Result.out = '\0';
            ZoneText(TagBuf, (size_t)(Result.out - TagBuf));
        }
#endif

        TFixedVector<VkCommandBuffer, 4> CommandBuffers(NumCommandLists);
        
        for (uint32 i = 0; i < NumCommandLists; ++i)
        {
            FVulkanCommandList* VulkanCommandList = static_cast<FVulkanCommandList*>(CommandLists[i]->GetUnwrappedCommandList());
            ECommandQueue CommandListType = VulkanCommandList->GetCommandListInfo().CommandQueue;
            if (CommandListType != Type)
            {
                LOG_CRITICAL("Attempted to submit a command buffer to queue type {0} but was a {1} command buffer!", (uint32)Type, (uint32)CommandListType);
                continue;
            }
            
            TRefCountPtr<FTrackedCommandBuffer>& TrackedBuffer = VulkanCommandList->CurrentCommandBuffer;
            DEBUG_ASSERT(TrackedBuffer->Queue == this);


            CommandBuffers[i] = TrackedBuffer->CommandBuffer;
            CommandBuffersInFlight.push_back(TrackedBuffer);

            for (const TRefCountPtr<FRHIBuffer>& Buffer : TrackedBuffer->ReferencedStagingResources)
            {
                FVulkanBuffer* VkBuf = Buffer.As<FVulkanBuffer>();
                VkBuf->LastUseQueue = Type;
                VkBuf->LastUseCommandListID = LastSubmittedID;
            }
            
        }

        AddSignalSemaphore(TimelineSemaphore, LastSubmittedID);

        VkTimelineSemaphoreSubmitInfo TimelineSubmitInfo    = {};
        TimelineSubmitInfo.sType                            = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        TimelineSubmitInfo.signalSemaphoreValueCount        = (uint32)SignalSemaphoreValues.size();
        TimelineSubmitInfo.pSignalSemaphoreValues           = SignalSemaphoreValues.data();
        TimelineSubmitInfo.waitSemaphoreValueCount          = (uint32)WaitSemaphoreValues.size();
        TimelineSubmitInfo.pWaitSemaphoreValues             = WaitSemaphoreValues.data();
        
        VkSubmitInfo SubmitInfo         = {};
        SubmitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        SubmitInfo.pNext                = &TimelineSubmitInfo;
        SubmitInfo.pCommandBuffers      = CommandBuffers.data();
        SubmitInfo.commandBufferCount   = NumCommandLists;
        SubmitInfo.pWaitSemaphores      = WaitSemaphores.data();
        SubmitInfo.waitSemaphoreCount   = (uint32)WaitSemaphores.size();
        SubmitInfo.pWaitDstStageMask    = WaitStageFlags.data();
        SubmitInfo.pSignalSemaphores    = SignalSemaphores.data();
        SubmitInfo.signalSemaphoreCount = (uint32)SignalSemaphores.size();
        
        VK_CHECK(vkQueueSubmit(Queue, 1, &SubmitInfo, nullptr));

        WaitSemaphores.clear();
        WaitSemaphoreValues.clear();
        WaitStageFlags.clear();
        SignalSemaphores.clear();
        SignalSemaphoreValues.clear();

        return LastSubmittedID;
    }

    void FQueue::SignalSemaphore(VkSemaphore SemaphoreToSignal) const
    {
        LUMINA_PROFILE_SCOPE();

        VkSubmitInfo SubmitInfo         = {};
        SubmitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        SubmitInfo.pSignalSemaphores    = &SemaphoreToSignal;
        SubmitInfo.signalSemaphoreCount = 1;
        
        VK_CHECK(vkQueueSubmit(Queue, 1, &SubmitInfo, nullptr));
    }

    void FQueue::WaitIdle()
    {
        LUMINA_PROFILE_SCOPE();
        VK_CHECK(vkQueueWaitIdle(Queue));
    }

    uint64 FQueue::UpdateLastFinishID()
    {
        VK_CHECK(vkGetSemaphoreCounterValue(Device->GetDevice(), TimelineSemaphore, &LastFinishedID));
        return LastFinishedID;
    }

    bool FQueue::PollCommandList(uint64 CommandListID)
    {
        LUMINA_PROFILE_SCOPE_COLORED(tracy::Color::Green);
        if (CommandListID > LastSubmittedID || CommandListID == 0)
        {
            return false;
        }

        bool bCompleted = LastFinishedID >= CommandListID;
        if (bCompleted)
        {
            return true;
        }
        
        bCompleted = UpdateLastFinishID() >= CommandListID;
        return bCompleted;
    }

    bool FQueue::WaitCommandList(uint64 CommandListID, uint64 Timeout)
    {
        LUMINA_PROFILE_SCOPE_COLORED(tracy::Color::Green);

        if (CommandListID > LastSubmittedID || CommandListID == 0)
        {
            return false;
        }

        if (PollCommandList(CommandListID))
        {
            return true;
        }

        VkSemaphoreWaitInfo WaitInfo    = {};
        WaitInfo.sType                  = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        WaitInfo.semaphoreCount         = 1;
        WaitInfo.pSemaphores            = &TimelineSemaphore;
        WaitInfo.pValues                = &CommandListID;

#if TRACY_ENABLE
        {
            char TagBuf[96];
            auto Result = std::format_to_n(TagBuf, sizeof(TagBuf) - 1, "Wait CL ID: {} | Last Submitted: {}", CommandListID, LastSubmittedID);
            *Result.out = '\0';
            ZoneText(TagBuf, (size_t)(Result.out - TagBuf));
        }
#endif

        {
            LUMINA_PROFILE_SECTION("vkWaitSemaphores");
            VkResult Result = vkWaitSemaphores(Device->GetDevice(), &WaitInfo, Timeout);
            VK_CHECK(Result);
            return (Result == VK_SUCCESS);
        }
    }

    void FQueue::AddSignalSemaphore(VkSemaphore Semaphore, uint64 Value)
    {
        ASSUME(Semaphore);
        SignalSemaphores.push_back(Semaphore);
        SignalSemaphoreValues.push_back(Value);
    }

    void FQueue::AddWaitSemaphore(VkSemaphore Semaphore, uint64 Value, VkPipelineStageFlags Stage)
    {
        ASSUME(Semaphore);
        WaitSemaphores.push_back(Semaphore);
        WaitSemaphoreValues.push_back(Value);
        WaitStageFlags.push_back(Stage);
    }

    
    FRHICommandListRef FCommandListManager::GetOrCreateCommandList(FVulkanRenderContext* RenderContext, const FCommandListInfo& CommandListInfo)
    {
        TConcurrentQueue<FRHICommandListRef>& CommandListPool = CommandLists[(uint32)CommandListInfo.CommandQueue];

        FRHICommandListRef CommandList;
        if (!CommandListPool.try_dequeue(CommandList))
        {
            CommandList = MakeRefCount<FVulkanCommandList>(RenderContext, CommandListInfo);
        }

        return CommandList;
    }

    void FCommandListManager::Enqueue(ICommandList* RetiredCommandList)
    {
        CommandLists[(uint32)RetiredCommandList->GetCommandListInfo().CommandQueue].enqueue(RetiredCommandList);
    }

    void FCommandListManager::BulkEnqueue(ICommandList* const* RetiredCommandLists, uint32 Num, ECommandQueue QueueType)
    {
        CommandLists[(uint32)QueueType].enqueue_bulk(RetiredCommandLists, Num);
    }

    void FCommandListManager::Cleanup()
    {
        for (uint32 i = 0; i < (uint32)ECommandQueue::Num; ++i)
        {
            FRHICommandListRef Item;
            while (CommandLists[i].try_dequeue(Item)) { }
        }
    }
    
    FVulkanRenderContext::FVulkanRenderContext()
        : TimerQueryAllocator(1024)
        , PipelineStatsAllocator(1024)
        , CurrentFrameIndex(0)
    {
    }

    static void ShowVulkanInitFailure(const FString& Title, const FString& Message)
    {
        LOG_CRITICAL("{}: {}", Title, Message);
        const FString Body = Message + "\n\nThis is usually caused by an outdated GPU driver or a GPU that does not support the required Vulkan features. "
            "Please update your graphics drivers from your GPU vendor (NVIDIA, AMD, or Intel) and restart the application.";
        Dialogs::ShowInternal(Dialogs::ESeverity::FatalError, Dialogs::EType::Ok, Title, Body);
    }

    bool FVulkanRenderContext::Initialize(const FRenderContextDesc& Desc)
    {
        LUMINA_PROFILE_SCOPE();

        CrashTracker = MakeUnique<RHI::FVulkanCrashTracker>();

        if (!glfwVulkanSupported())
        {
            ShowVulkanInitFailure("Vulkan Not Supported",
                "GLFW reports that this system does not support Vulkan. The Vulkan runtime (vulkan-1.dll) was not found, "
                "or no installed GPU driver provides a Vulkan ICD.");
            return false;
        }

        VkResult VolkInitResult = volkInitialize();
        if (VolkInitResult != VK_SUCCESS)
        {
            ShowVulkanInitFailure("Vulkan Loader Failure",
                "Failed to initialize the Vulkan loader (volkInitialize). The Vulkan runtime appears to be missing or corrupted.");
            return false;
        }

        Description = Desc;
        GVulkanAllocationCallbacks.pfnAllocation    = VulkanAlloc;
        GVulkanAllocationCallbacks.pfnFree          = VulkanFree;
        GVulkanAllocationCallbacks.pfnReallocation  = VulkanRealloc;

        vkb::InstanceBuilder Builder; Builder
        .set_app_name("Lumina Engine")
        .require_api_version(1, 4, 0)
        .set_allocation_callbacks(VK_ALLOC_CALLBACK);
        if (Description.bValidation)
        {
            Builder.add_debug_messenger_severity(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
            Builder.add_debug_messenger_severity(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT);
            Builder.add_debug_messenger_type(VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT);
            Builder.add_debug_messenger_type(VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT);
            Builder.add_debug_messenger_type(VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT);
            Builder.add_validation_feature_enable(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
            Builder.request_validation_layers();
            Builder.use_default_debug_messenger();
            Builder.set_debug_callback(VkDebugCallback);
        }
        
        if (Description.bDebugUtils)
        {
            Builder.enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        vkb::Result InstBuilder = Builder.build();

        if (!InstBuilder.has_value())
        {
            FString InstanceMessage = "Failed to create a Vulkan instance: ";
            InstanceMessage += InstBuilder.error().message().c_str();
            ShowVulkanInitFailure("Vulkan Instance Creation Failed", InstanceMessage);
            return false;
        }

        VulkanInstance = InstBuilder.value();

        volkLoadInstance(VulkanInstance);

        if (Description.bValidation)
        {
            DebugUtils.DebugMessenger = InstBuilder->debug_messenger;
        }

        if (!CreateDevice(InstBuilder.value()))
        {
            return false;
        }

        volkLoadDevice(VulkanDevice->GetDevice());
        
        uint32 APIVer = GetDevice()->GetPhysicalDeviceProperties().apiVersion;
        uint32 Major = VK_API_VERSION_MAJOR(APIVer);
        uint32 Minor = VK_API_VERSION_MINOR(APIVer);
        uint32 Patch = VK_API_VERSION_PATCH(APIVer);
        
        LOG_TRACE("Vulkan Render Context - {} - API: {}.{}.{} - Validation: {}", GetDevice()->GetPhysicalDeviceProperties().deviceName, Major, Minor, Patch, Description.bValidation);

        DebugUtils.DebugUtilsObjectNameEXT      = (PFN_vkSetDebugUtilsObjectNameEXT)(vkGetInstanceProcAddr(VulkanInstance, "vkSetDebugUtilsObjectNameEXT"));
        
        Swapchain = Memory::New<FVulkanSwapchain>();
        Swapchain->CreateSwapchain(VulkanInstance, this, Windowing::GetPrimaryWindowHandle(), Windowing::GetPrimaryWindowHandle()->GetExtent());
        
        ShaderLibrary = MakeRefCount<FShaderLibrary>();
        ShaderCompiler = Memory::New<FSpirVShaderCompiler>();
        ShaderCompiler->Initialize();
        
        WaitIdle();
        FlushPendingDeletes();
        
        return true;
    }

    void FVulkanRenderContext::Deinitialize()
    {
        LUMINA_PROFILE_SCOPE();

        WaitIdle();

        ShaderCompiler->Shutdown();
        Memory::Delete(ShaderCompiler);
        
        ShaderLibrary.SafeRelease();
        PipelineCache.ReleasePipelines();
        
        Memory::Delete(Swapchain);

        // Drop command-list refs first. Each FRHICommandList holds an
        // FTrackedCommandBuffer ref alongside FQueue::CommandBufferPool;
        // releasing the manager first means the queue's pool drop is the
        // refcount-zero edge that triggers vkDestroyCommandPool, which must
        // happen before vkDestroyDevice at the end of this function.
        CommandListManager.Cleanup();

        for (TUniquePtr<FQueue>& Queue : Queues)
        {
            Queue.reset();
        }

        SamplerMap.clear();
        InputLayoutMap.clear();
        FlushPendingDeletes();
        IRHIResource::ReleaseAllRHIResources();

        CrashTracker->Shutdown();

        if (TimerQueryPool != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(VulkanDevice->GetDevice(), TimerQueryPool, VK_ALLOC_CALLBACK);
            TimerQueryPool = VK_NULL_HANDLE;
        }

        if (PipelineStatsQueryPool != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(VulkanDevice->GetDevice(), PipelineStatsQueryPool, VK_ALLOC_CALLBACK);
            PipelineStatsQueryPool = VK_NULL_HANDLE;
        }

        Memory::Delete(VulkanDevice);
        VulkanDevice = nullptr;

        if (Description.bValidation)
        {
            auto FuncPtr = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(VulkanInstance, "vkDestroyDebugUtilsMessengerEXT");
            FuncPtr(VulkanInstance, DebugUtils.DebugMessenger, VK_ALLOC_CALLBACK);
        }
        
        vkDestroyInstance(VulkanInstance, VK_ALLOC_CALLBACK);
        VulkanInstance = VK_NULL_HANDLE;
    }
    
    void FVulkanRenderContext::SetVSyncEnabled(bool bEnable)
    {
        Swapchain->SetPresentMode(bEnable ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR);
    }

    bool FVulkanRenderContext::IsVSyncEnabled() const
    {
        return Swapchain->GetPresentMode() == VK_PRESENT_MODE_FIFO_KHR;
    }

    void FVulkanRenderContext::HandleDeviceLost()
    {
        if (CrashTracker)
        {
            CrashTracker->OnDeviceLost();
        }
    }

    void FVulkanRenderContext::WaitIdle()
    {
        VK_CHECK(vkDeviceWaitIdle(VulkanDevice->GetDevice()));
    }

    bool FVulkanRenderContext::FrameStart(const FUpdateContext& UpdateContext, uint8 InCurrentFrameIndex) 
    {
        LUMINA_PROFILE_SCOPE();

        CurrentFrameIndex = InCurrentFrameIndex;

        bool bSuccess = Swapchain->AcquireNextImage();
        
        return bSuccess;
    }

    bool FVulkanRenderContext::FrameEnd(const FUpdateContext& UpdateContext, ICommandList& CmdList)
    {
        LUMINA_PROFILE_SCOPE();

        CmdList.CopyImage(FEngine::GetEngineViewport()->GetRenderTarget(), FTextureSlice(), Swapchain->GetCurrentImage(), FTextureSlice());

        CmdList.Close();
        ICommandList* CL = &CmdList;
        ExecuteCommandLists(&CL, 1, ECommandQueue::Graphics);

        bool bSuccess = Swapchain->Present();

        return bSuccess;
    }

    uint64 FVulkanRenderContext::GetAllocatedMemory() const
    {
        VmaBudget Budgets[VK_MAX_MEMORY_HEAPS];
        vmaGetHeapBudgets(GetDevice()->GetAllocator().GetVMA(), Budgets);

        uint64 Used = 0;

        for (uint32 i = 0; i < VK_MAX_MEMORY_HEAPS; i++)
        {
            Used += Budgets[i].usage;
        }

        return Used;
    }

    uint64 FVulkanRenderContext::GetAvailableMemory() const
    {
        VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
        vmaGetHeapBudgets(GetDevice()->GetAllocator().GetVMA(), budgets);

        uint64 Budget = 0;
        uint64 Usage  = 0;

        for (uint32 i = 0; i < VK_MAX_MEMORY_HEAPS; i++)
        {
            Budget += budgets[i].budget;
            Usage  += budgets[i].usage;
        }

        return (Budget > Usage) ? (Budget - Usage) : 0;
    }

    void FVulkanRenderContext::ClearCommandListCache()
    {
        CommandListManager.Cleanup();
    }

    FRHICommandListRef FVulkanRenderContext::CreateCommandList(const FCommandListInfo& Info)
    {
        auto* Inner = new FVulkanCommandList(this, Info);
        if (Description.bValidation)
        {
            return MakeRefCount<FCommandListValidator>(Inner);
        }
        return Inner;
    }
    
    uint64 FVulkanRenderContext::ExecuteCommandLists(ICommandList* const* CommandLists, uint32 NumCommandLists, ECommandQueue QueueType)
    {
        LUMINA_PROFILE_SCOPE();
        
        TUniquePtr<FQueue>& Queue = Queues[(uint32)QueueType];

        uint64 SubmissionID = Queue->Submit(CommandLists, NumCommandLists);

        if (NumCommandLists > 30)
        {
            Task::ParallelFor(NumCommandLists, [&](uint32 Index)
            {
                FVulkanCommandList* CommandList = static_cast<FVulkanCommandList*>(CommandLists[Index]->GetUnwrappedCommandList());
                CommandList->Executed(Queue.get(), SubmissionID);
            });
        }
        else
        {
            for (uint32 i = 0; i < NumCommandLists; ++i)
            {
                FVulkanCommandList* CommandList = static_cast<FVulkanCommandList*>(CommandLists[i]->GetUnwrappedCommandList());
                CommandList->Executed(Queue.get(), SubmissionID);
            }
        }
        
        return SubmissionID;
    }
    
    bool FVulkanRenderContext::CreateDevice(vkb::Instance Instance)
    {
        VkPhysicalDeviceFeatures DeviceFeatures             = {};
        DeviceFeatures.fragmentStoresAndAtomics             = VK_TRUE;
        DeviceFeatures.samplerAnisotropy                    = VK_TRUE;
        DeviceFeatures.sampleRateShading                    = VK_TRUE;
        DeviceFeatures.fillModeNonSolid                     = VK_TRUE;
        DeviceFeatures.wideLines                            = VK_TRUE; // @TODO Don't keep this.
        DeviceFeatures.imageCubeArray                       = VK_TRUE;
        DeviceFeatures.multiViewport                        = VK_TRUE;
        DeviceFeatures.multiDrawIndirect                    = VK_TRUE;
        DeviceFeatures.shaderStorageImageWriteWithoutFormat = VK_TRUE;
        DeviceFeatures.shaderStorageImageReadWithoutFormat  = VK_TRUE;
        DeviceFeatures.shaderStorageImageExtendedFormats    = VK_TRUE;
        DeviceFeatures.drawIndirectFirstInstance            = VK_TRUE;
        DeviceFeatures.vertexPipelineStoresAndAtomics       = VK_TRUE; // @TODO See if we need this.
        DeviceFeatures.shaderInt16                          = VK_TRUE;
        DeviceFeatures.independentBlend                     = VK_TRUE;
        DeviceFeatures.pipelineStatisticsQuery              = VK_TRUE;

        VkPhysicalDeviceVulkan11Features Features11 = {};
        Features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        Features11.shaderDrawParameters             = VK_TRUE;
        Features11.multiview                        = VK_TRUE;
        
        VkPhysicalDeviceVulkan12Features Features12 = {};
        Features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        Features12.timelineSemaphore                = VK_TRUE;
        Features12.bufferDeviceAddress              = VK_TRUE;
        Features12.descriptorIndexing                                 = VK_TRUE;
        Features12.descriptorBindingPartiallyBound                    = VK_TRUE;
        // UpdateAfterBind lets FTextureManager register/unregister bindless
        // images mid-frame without invalidating in-flight command buffers.
        Features12.descriptorBindingSampledImageUpdateAfterBind       = VK_TRUE;
        Features12.descriptorBindingStorageImageUpdateAfterBind       = VK_TRUE;
        Features12.descriptorBindingUniformBufferUpdateAfterBind      = VK_TRUE;
        Features12.descriptorBindingStorageBufferUpdateAfterBind      = VK_TRUE;
        Features12.descriptorBindingUpdateUnusedWhilePending          = VK_TRUE;
        Features12.shaderOutputViewportIndex        = VK_TRUE; // Should not stay.
        Features12.shaderOutputLayer                = VK_TRUE; // Should not stay.
        Features12.samplerFilterMinmax              = VK_TRUE;
        Features12.bufferDeviceAddress              = VK_TRUE;
        Features12.runtimeDescriptorArray           = VK_TRUE;
        Features12.shaderInt8                       = VK_TRUE;
        Features12.shaderFloat16                    = VK_TRUE;

        VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR DerivativesFeature{};
        DerivativesFeature.sType                        = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR;
        DerivativesFeature.computeDerivativeGroupQuads  = VK_TRUE;

        // VK_EXT_mutable_descriptor_type lets the bindless table host both
        // sampled and storage images at one binding slot, with the per-write
        // descriptorType picking which interpretation to use.
        VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT MutableDescriptorFeature{};
        MutableDescriptorFeature.sType                  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT;
        MutableDescriptorFeature.mutableDescriptorType  = VK_TRUE;
        MutableDescriptorFeature.pNext                  = &DerivativesFeature;

        VkPhysicalDeviceVulkan13Features Features13 = {};
        Features13.sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        Features13.dynamicRendering                 = VK_TRUE;
        Features13.synchronization2                 = VK_TRUE;
        Features13.pNext                            = &MutableDescriptorFeature;


        vkb::PhysicalDeviceSelector selector(Instance);
        auto PhysicalDeviceResult = selector
            .set_minimum_version(1, 4)
            .set_required_features(DeviceFeatures)
            .set_required_features_11(Features11)
            .set_required_features_12(Features12)
            .set_required_features_13(Features13)
            .add_required_extension(VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME)
            .require_separate_transfer_queue()
            .require_separate_compute_queue()
            .defer_surface_initialization()
            .select();

        if (!PhysicalDeviceResult.has_value())
        {
            uint32 InstanceVersion = VK_API_VERSION_1_0;
            if (vkEnumerateInstanceVersion != nullptr)
            {
                vkEnumerateInstanceVersion(&InstanceVersion);
            }

            FString Message = "No suitable GPU was found that supports the required Vulkan 1.4 features.\n\nReason: ";
            Message += PhysicalDeviceResult.error().message().c_str();
            Message += "\n\nDetected Vulkan instance API version: ";
            Message += eastl::to_string(VK_API_VERSION_MAJOR(InstanceVersion)).c_str();
            Message += ".";
            Message += eastl::to_string(VK_API_VERSION_MINOR(InstanceVersion)).c_str();
            Message += ".";
            Message += eastl::to_string(VK_API_VERSION_PATCH(InstanceVersion)).c_str();
            Message += "\n\nLumina requires Vulkan 1.4 with dynamic rendering, synchronization2, descriptor indexing, "
                "buffer device address, timeline semaphores, and compute shader derivatives.";

            ShowVulkanInitFailure("Vulkan Device Selection Failed", Message);
            return false;
        }

        vkb::PhysicalDevice PhysicalDevice = PhysicalDeviceResult.value();

        PhysicalDevice.enable_extension_if_present(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        PhysicalDevice.enable_extension_if_present(VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME);
        PhysicalDevice.enable_extension_if_present(VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);
        PhysicalDevice.enable_extension_if_present(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);

        if (PhysicalDevice.enable_extension_if_present(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME))
        {
            EnabledExtensions.SetFlag(EVulkanExtensions::PushDescriptors);
        }
        
        if (PhysicalDevice.enable_extension_if_present(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME))
        {
            EnabledExtensions.SetFlag(EVulkanExtensions::ConservativeRasterization);
        }

        if (PhysicalDevice.enable_extension_if_present(VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME))
        {
            EnabledExtensions.SetFlag(EVulkanExtensions::ViewportIndexLayer);
        }

        vkb::DeviceBuilder DeviceBuilder(PhysicalDevice);
        CrashTracker->EnableDeviceFeatures(DeviceBuilder);

        auto DeviceResult = DeviceBuilder.build();
        if (!DeviceResult.has_value())
        {
            FString Message = "Failed to create the Vulkan logical device on '";
            Message += PhysicalDevice.name.c_str();
            Message += "'.\n\nReason: ";
            Message += DeviceResult.error().message().c_str();
            ShowVulkanInitFailure("Vulkan Device Creation Failed", Message);
            return false;
        }

        vkb::Device vkbDevice = DeviceResult.value();
        VkDevice Device = vkbDevice.device;
        volkLoadDevice(Device);

        CrashTracker->Initialize(Device, PhysicalDevice.physical_device);

        VkPhysicalDevice VulkanPhysicalDevice = PhysicalDevice.physical_device;
        VulkanDevice = Memory::New<FVulkanDevice>(this, VulkanInstance, VulkanPhysicalDevice, Device);

        if (vkbDevice.get_queue(vkb::QueueType::graphics).has_value())
        {
            VkQueue Queue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
            uint32 Index = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
            Queues[uint32(ECommandQueue::Graphics)] = MakeUnique<FQueue>(this, Queue, Index, ECommandQueue::Graphics);
            SetVulkanObjectName("Graphics Queue", VK_OBJECT_TYPE_QUEUE, (uintptr_t)Queue);
        }

        if (vkbDevice.get_queue(vkb::QueueType::compute).has_value())
        {
            VkQueue Queue = vkbDevice.get_queue(vkb::QueueType::compute).value();
            uint32 Index = vkbDevice.get_queue_index(vkb::QueueType::compute).value();
            Queues[uint32(ECommandQueue::Compute)] = MakeUnique<FQueue>(this, Queue, Index, ECommandQueue::Compute);
            SetVulkanObjectName("Compute Queue", VK_OBJECT_TYPE_QUEUE, (uintptr_t)Queue);
        }

        if (vkbDevice.get_queue(vkb::QueueType::transfer).has_value())
        {
            VkQueue Queue = vkbDevice.get_queue(vkb::QueueType::transfer).value();
            uint32 Index = vkbDevice.get_queue_index(vkb::QueueType::transfer).value();
            Queues[uint32(ECommandQueue::Transfer)] = MakeUnique<FQueue>(this, Queue, Index, ECommandQueue::Transfer);
            SetVulkanObjectName("Transfer Queue", VK_OBJECT_TYPE_QUEUE, (uintptr_t)Queue);
        }

        return true;
    }

    uint64 FVulkanRenderContext::GetAlignedSizeForBuffer(uint64 Size, TBitFlags<EBufferUsageFlags> Usage)
    {
        uint64 MinAlignment = 1;

        if(Usage.AreAnyFlagsSet(EBufferUsageFlags::Dynamic))
        {
            MinAlignment = VulkanDevice->GetPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment;
        }

        return Math::GetAligned(Size, MinAlignment);
    }
    
    FRHIViewportRef FVulkanRenderContext::CreateViewport(const glm::uvec2& Size, FString&& DebugName)
    {
        return MakeRefCount<FVulkanViewport>(Size, this, Move(DebugName));
    }

    FRHIStagingImageRef FVulkanRenderContext::CreateStagingImage(const FRHIImageDesc& Desc, ERHIAccess Access)
    {
        auto Image = MakeRefCount<FVulkanStagingImage>(VulkanDevice);
        Image->Desc = Desc;
        Image->PopulateSliceRegions();

        FRHIBufferDesc BufDesc;
        BufDesc.Size                = Image->GetBufferSize();
        BufDesc.DebugName           = Desc.DebugName;
        BufDesc.InitialState        = EResourceStates::CopyDest;
        BufDesc.bKeepInitialState   = true;
        if (Access == ERHIAccess::HostRead)
        {
            BufDesc.Usage.SetFlag(EBufferUsageFlags::CPUReadable);
        }
        else if (Access == ERHIAccess::HostWrite)
        {
            BufDesc.Usage.SetFlag(EBufferUsageFlags::CPUWritable);
        }

        auto Buffer = MakeRefCount<FVulkanBuffer>(VulkanDevice, BufDesc);
        Image->Buffer = Buffer;

        if (!Image->Buffer)
        {
            return nullptr;
        }

        return Image;
    }

    void* FVulkanRenderContext::MapStagingTexture(FRHIStagingImage* RESTRICT Image, const FTextureSlice& slice, ERHIAccess Access, size_t* RESTRICT OutRowPitch)
    {
        FVulkanStagingImage* VulkanStagingImage = static_cast<FVulkanStagingImage*>(Image);

        auto ResolvedSlice = slice.Resolve(Image->GetDesc());

        auto Region = VulkanStagingImage->GetSliceRegion(ResolvedSlice.MipLevel, ResolvedSlice.ArraySlice, ResolvedSlice.Z);
        
        const FFormatInfo& formatInfo = RHI::Format::Info(VulkanStagingImage->Desc.Format);

        auto wInBlocks = ResolvedSlice.X / formatInfo.BlockSize;

        *OutRowPitch = wInBlocks * formatInfo.BytesPerBlock;

        uint8* MappedPtr = static_cast<uint8*>(VulkanDevice->GetAllocator().GetMappedMemory(VulkanStagingImage->Buffer));
        MappedPtr += Region.Offset;
        return MappedPtr;
    }

    void FVulkanRenderContext::UnMapStagingTexture(FRHIStagingImage* Image)
    {
        // Persistently mapped; no-op.
    }

    FRHIImageRef FVulkanRenderContext::CreateImage(const FRHIImageDesc& ImageSpec)
    {
        return MakeRefCount<FVulkanImage>(VulkanDevice, ImageSpec);
    }

    FRHISamplerRef FVulkanRenderContext::CreateSampler(const FSamplerDesc& SamplerDesc)
    {
        uint64 Hash = Hash::GetHash(SamplerDesc);

        FScopeLock Lock(SamplerMutex);
        auto [It, bInserted] = SamplerMap.try_emplace(Hash);
        if (bInserted)
        {
            It->second = MakeRefCount<FVulkanSampler>(VulkanDevice, SamplerDesc);
        }
        return It->second;
    }

    FRHIVertexShaderRef FVulkanRenderContext::CreateVertexShader(const FShaderHeader& Shader)
    {
        return MakeRefCount<FVulkanVertexShader>(VulkanDevice, Shader);
    }

    FRHIPixelShaderRef FVulkanRenderContext::CreatePixelShader(const FShaderHeader& Shader)
    {
        return MakeRefCount<FVulkanPixelShader>(VulkanDevice, Shader);
    }

    FRHIComputeShaderRef FVulkanRenderContext::CreateComputeShader(const FShaderHeader& Shader)
    {
        return MakeRefCount<FVulkanComputeShader>(VulkanDevice, Shader);
    }

    FRHIGeometryShaderRef FVulkanRenderContext::CreateGeometryShader(const FShaderHeader& Shader)
    {
        return MakeRefCount<FVulkanGeometryShader>(VulkanDevice, Shader);
    }

    IShaderCompiler* FVulkanRenderContext::GetShaderCompiler() const
    {
        return ShaderCompiler;
    }

    FRHIShaderLibraryRef FVulkanRenderContext::GetShaderLibrary() const
    {
        return ShaderLibrary;
    }

    FRHIDescriptorTableRef FVulkanRenderContext::CreateDescriptorTable(FRHIBindingLayout* InLayout)
    {
        return MakeRefCount<FVulkanDescriptorTable>(this, (FVulkanBindingLayout*)InLayout);
    }

    void FVulkanRenderContext::ResizeDescriptorTable(FRHIDescriptorTable* Table, uint32 NewSize, bool bKeepContents)
    {
        (void)Table;
        (void)NewSize;
        (void)bKeepContents;
    }

    bool FVulkanRenderContext::WriteDescriptorTable(FRHIDescriptorTable* Table, const FBindingSetItem& Binding)
    {
        LUMINA_PROFILE_SCOPE();

        FVulkanDescriptorTable* DescriptorTable = static_cast<FVulkanDescriptorTable*>(Table);
        FVulkanBindingLayout* BindingLayout = static_cast<FVulkanBindingLayout*>(DescriptorTable->GetLayout());

        TFixedVector<VkWriteDescriptorSet, 4> Writes;
        TFixedVector<VkDescriptorImageInfo, 4> ImageWriteInfos;
        TFixedVector<VkDescriptorBufferInfo, 4> BufferWriteInfos;
        
        if (Binding.Slot >= DescriptorTable->GetCapacity())
        {
            return false;
        }

        auto WriteDescriptorForBinding = [&] (const VkDescriptorSetLayoutBinding& LayoutBinding)
        {
            VkWriteDescriptorSet Write  = {};
            Write.sType                 = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            Write.descriptorCount       = 1;
            Write.dstArrayElement       = Binding.Slot;
            Write.dstBinding            = LayoutBinding.binding;
            Write.dstSet                = DescriptorTable->DescriptorSet;
            
            switch (Binding.Type)
            {
            case ERHIBindingResourceType::Texture_SRV:
                {
                    FVulkanImage* Image = static_cast<FVulkanImage*>(Binding.ResourceHandle);

                    // SRVs span every mip the caller asked for. SampleLevel(UV, lod) on
                    // multi-mip images (HZB taps especially) requires the view to expose
                    // all mips, otherwise reads outside the view's mip range are
                    // implementation-defined.
                    const FTextureSubresourceSet Subresource = Binding.GetTextureResource().Subresources.Resolve(Image->GetDescription(), false);
                    FVulkanImage::ESubresourceViewType ViewType = Vk::GetTextureViewType(Binding.Format, Image->GetDescription().Format);
                    VkImageView View = Image->GetSubresourceView(Subresource, Binding.GetTextureResource().Dimension, Binding.Format, VK_IMAGE_USAGE_SAMPLED_BIT, ViewType).View;

                    VkDescriptorImageInfo& ImageInfo = ImageWriteInfos.emplace_back();
                    ImageInfo.imageView = View;
                    ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                    // SRV-into-mutable writes a plain SAMPLED_IMAGE (sampler comes
                    // from the bindless sampler array). Otherwise fall back to the
                    // legacy COMBINED_IMAGE_SAMPLER path with the static default
                    // sampler baked in.
                    const bool bMutable = (LayoutBinding.descriptorType == VK_DESCRIPTOR_TYPE_MUTABLE_EXT);
                    if (bMutable)
                    {
                        ImageInfo.sampler    = VK_NULL_HANDLE;
                        Write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                    }
                    else
                    {
                        ImageInfo.sampler    = TStaticRHISampler<>::GetRHI()->GetAPI<VkSampler>();
                        Write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    }
                    Write.pImageInfo = &ImageInfo;
                }
                break;
            case ERHIBindingResourceType::Texture_UAV:
                {
                    FVulkanImage* Image = static_cast<FVulkanImage*>(Binding.ResourceHandle);

                    const FTextureSubresourceSet Subresource = Binding.GetTextureResource().Subresources.Resolve(Image->GetDescription(), true);
                    FVulkanImage::ESubresourceViewType ViewType = Vk::GetTextureViewType(Binding.Format, Image->GetDescription().Format);
                    VkImageView View = Image->GetSubresourceView(Subresource, Binding.GetTextureResource().Dimension, Binding.Format, VK_IMAGE_USAGE_STORAGE_BIT, ViewType).View;

                    VkDescriptorImageInfo& ImageInfo = ImageWriteInfos.emplace_back();
                    ImageInfo.imageView = View;
                    ImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                    Write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    Write.pImageInfo = &ImageInfo;
                }
                break;
            case ERHIBindingResourceType::Sampler:
                {
                    FVulkanSampler* Sampler = static_cast<FVulkanSampler*>(Binding.ResourceHandle);

                    VkDescriptorImageInfo& ImageInfo = ImageWriteInfos.emplace_back();
                    ImageInfo.sampler     = Sampler->GetAPI<VkSampler>();
                    ImageInfo.imageView   = VK_NULL_HANDLE;
                    ImageInfo.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

                    Write.pImageInfo     = &ImageInfo;
                    Write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                }
                break;
            case ERHIBindingResourceType::Buffer_CBV:
                {
                    FVulkanBuffer* Buffer = static_cast<FVulkanBuffer*>(Binding.ResourceHandle);
                    VkDescriptorBufferInfo& BufferInfo = BufferWriteInfos.emplace_back();
                    BufferInfo.buffer = Buffer->GetBuffer();
                    BufferInfo.offset = 0;
                    BufferInfo.range = Binding.GetBufferRange().Resolve(Buffer->GetDescription()).ByteSize;
                        
                    Write.pBufferInfo = &BufferInfo;
                    Write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                }
                break;
            case ERHIBindingResourceType::Buffer_SRV:
                {
                    FVulkanBuffer* Buffer = static_cast<FVulkanBuffer*>(Binding.ResourceHandle);
                    VkDescriptorBufferInfo& BufferInfo = BufferWriteInfos.emplace_back();
                    BufferInfo.buffer = Buffer->GetBuffer();
                    BufferInfo.offset = 0;
                    BufferInfo.range = Binding.GetBufferRange().Resolve(Buffer->GetDescription()).ByteSize;
                        
                    Write.pBufferInfo = &BufferInfo;
                    Write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                }
                break;
            case ERHIBindingResourceType::Buffer_UAV:
                {
                    FVulkanBuffer* Buffer = static_cast<FVulkanBuffer*>(Binding.ResourceHandle);
                    VkDescriptorBufferInfo& BufferInfo = BufferWriteInfos.emplace_back();
                    BufferInfo.buffer = Buffer->GetBuffer();
                    BufferInfo.offset = 0;
                    BufferInfo.range = Binding.GetBufferRange().Resolve(Buffer->GetDescription()).ByteSize;
                        
                    Write.pBufferInfo = &BufferInfo;
                    Write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                }
                break;
            }

            Writes.push_back(Write);
        };

        auto BindingAcceptsWrite = [](ERHIBindingResourceType LayoutType, ERHIBindingResourceType WriteType)
        {
            if (LayoutType == WriteType)
            {
                return true;
            }
            // Image_Mutable bindings hold either kind of image.
            if (LayoutType == ERHIBindingResourceType::Image_Mutable)
            {
                return WriteType == ERHIBindingResourceType::Texture_SRV
                    || WriteType == ERHIBindingResourceType::Texture_UAV;
            }
            return false;
        };

        for (uint32 BindingLocation = 0; BindingLocation < uint32(BindingLayout->Bindings.size()); BindingLocation++)
        {
            const FBindingLayoutItem& Item = BindingLayout->BindlessDesc.Bindings[BindingLocation];
            if (BindingAcceptsWrite(Item.Type, Binding.Type))
            {
                const VkDescriptorSetLayoutBinding& LayoutBinding = BindingLayout->Bindings[BindingLocation];

                WriteDescriptorForBinding(LayoutBinding);
            }
        }
        
        vkUpdateDescriptorSets(VulkanDevice->GetDevice(), (uint32)Writes.size(), Writes.data(), 0, nullptr);

        return true;
    }
    
    FRHIBindingLayoutRef FVulkanRenderContext::CreateBindingLayout(const FBindingLayoutDesc& Desc)
    {
        LUMINA_PROFILE_SCOPE();
        auto Layout =  MakeRefCount<FVulkanBindingLayout>(VulkanDevice, Desc);

        Layout->Bake();

        return Layout;
    }

    FRHIBindingLayoutRef FVulkanRenderContext::CreateBindlessLayout(const FBindlessLayoutDesc& Desc)
    {
        LUMINA_PROFILE_SCOPE();

        auto Layout = MakeRefCount<FVulkanBindingLayout>(VulkanDevice, Desc);
        
        Layout->Bake();

        return Layout;
    }

    FRHIBindingSetRef FVulkanRenderContext::CreateBindingSet(const FBindingSetDesc& Desc, FRHIBindingLayout* InLayout)
    {
        LUMINA_PROFILE_SCOPE();
        return MakeRefCount<FVulkanBindingSet>(this, Desc, (FVulkanBindingLayout*)InLayout);
    }

    void FVulkanRenderContext::CreateBindingSetAndLayout(const TBitFlags<ERHIShaderType>& Visibility, uint16 Binding, const FBindingSetDesc& Desc, FRHIBindingLayoutRef& OutLayout, FRHIBindingSetRef& OutBindingSet)
    {
        FBindingLayoutDesc LayoutDesc;
        LayoutDesc.StageFlags = Visibility;
        LayoutDesc.SetBindingIndex(Binding);
        
        for (const FBindingSetItem& BindingItem : Desc.Bindings)
        {
            FBindingLayoutItem Item;
            Item.Slot = BindingItem.Slot;
            Item.Type = BindingItem.Type;
            Item.Size = 1;
            LayoutDesc.Bindings.push_back(Item);
        }

        OutLayout       = CreateBindingLayout(LayoutDesc);
        OutBindingSet   = CreateBindingSet(Desc, OutLayout);
    }

    FRHIComputePipelineRef FVulkanRenderContext::CreateComputePipeline(const FComputePipelineDesc& Desc)
    {
        return PipelineCache.GetOrCreateComputePipeline(VulkanDevice, Desc);
    }

    FRHIGraphicsPipelineRef FVulkanRenderContext::CreateGraphicsPipeline(const FGraphicsPipelineDesc& Desc, const FRenderPassDesc& RenderPassDesc)
    {
        return PipelineCache.GetOrCreateGraphicsPipeline(VulkanDevice, Desc, RenderPassDesc);
    }

    RHI::ICrashTracker& FVulkanRenderContext::GetCrashTracker() const
    {
        return *CrashTracker;
    }

    void FVulkanRenderContext::SetObjectName(IRHIResource* Resource, const char* Name, EAPIResourceType Type)
    {
        if (GetDebugUtils().DebugUtilsObjectNameEXT)
        {
            VkDebugUtilsObjectNameInfoEXT NameInfo = {};
            NameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            NameInfo.pObjectName = Name;
            NameInfo.objectType = ToVkObjectType(Type);
            NameInfo.objectHandle = reinterpret_cast<uint64>(Resource->GetAPI(Type));

            GetDebugUtils().DebugUtilsObjectNameEXT(GetDevice()->GetDevice(), &NameInfo);
        }
    }

    FRHIEventQueryRef FVulkanRenderContext::CreateEventQuery()
    {
        return MakeRefCount<FVulkanEventQuery>();
    }

    void FVulkanRenderContext::SetEventQuery(IEventQuery* Query, ECommandQueue Queue)
    {
        FVulkanEventQuery* VkQuery = static_cast<FVulkanEventQuery*>(Query);
        DEBUG_ASSERT(VkQuery->CommandListID == 0);

        VkQuery->Queue = Queue;
        VkQuery->CommandListID = GetQueue(Queue)->LastSubmittedID;
    }

    void FVulkanRenderContext::ResetEventQuery(IEventQuery* Query)
    {
        FVulkanEventQuery* VkQuery = static_cast<FVulkanEventQuery*>(Query);
        VkQuery->CommandListID = 0;
    }

    bool FVulkanRenderContext::PollEventQuery(IEventQuery* Query)
    {
        FVulkanEventQuery* VkQuery = static_cast<FVulkanEventQuery*>(Query);
        FQueue* Queue = GetQueue(VkQuery->Queue);
        return Queue->PollCommandList(VkQuery->CommandListID);
    }

    void FVulkanRenderContext::WaitEventQuery(IEventQuery* Query)
    {
        LUMINA_PROFILE_SCOPE_COLORED(tracy::Color::Green3);
        FVulkanEventQuery* VkQuery = static_cast<FVulkanEventQuery*>(Query);
        if (VkQuery->CommandListID == 0)
        {
            return;
        }

        FQueue* Queue = GetQueue(VkQuery->Queue);
        bool bSuccess = Queue->WaitCommandList(VkQuery->CommandListID, UINT64_MAX);
        ASSERT(bSuccess);
        
        (void)bSuccess;
    }

    FRHITimerQueryRef FVulkanRenderContext::CreateTimerQuery()
    {
        if (TimerQueryPool == VK_NULL_HANDLE)
        {
            VkQueryPoolCreateInfo QueryPoolInfo = {};
            QueryPoolInfo.sType         = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            QueryPoolInfo.queryCount    = TimerQueryAllocator.GetCapacity() * 2;
            QueryPoolInfo.queryType     = VK_QUERY_TYPE_TIMESTAMP;
            VK_CHECK(vkCreateQueryPool(GetDevice()->GetDevice(), &QueryPoolInfo, VK_ALLOC_CALLBACK, &TimerQueryPool));
        }
        
        int32 QueryIndex = TimerQueryAllocator.Allocate();
        
        if (QueryIndex < 0)
        {
            LOG_ERROR("Insufficient query pool space, increase numer of timer queries");
            return nullptr;
        }
        
        auto TimerQuery = MakeRefCount<FVulkanTimerQuery>(TimerQueryAllocator);
        TimerQuery->BeginQueryIndex = QueryIndex * 2;
        TimerQuery->EndQueryIndex = QueryIndex * 2 + 1;
        
        return TimerQuery;
    }

    bool FVulkanRenderContext::PollTimerQuery(ITimerQuery* Query)
    {
        FVulkanTimerQuery* VulkanTimerQuery = static_cast<FVulkanTimerQuery*>(Query);
        
        if (!VulkanTimerQuery->bStarted)
        {
            return false;
        }
        
        if (VulkanTimerQuery->bResolved)
        {
            return true;
        }
        
        uint32 Timestamps[2] = {0, 0};
        
        VkResult Result = vkGetQueryPoolResults(GetDevice()->GetDevice(), TimerQueryPool, VulkanTimerQuery->BeginQueryIndex, 2, sizeof(Timestamps), Timestamps, sizeof(Timestamps[0]), 0);
        if (Result == VK_NOT_READY)
        {
            return false;
        }
        
        const auto TimestampPeriod = GetDevice()->GetPhysicalDeviceProperties().limits.timestampPeriod;
        const float Scale = 1e-9f * TimestampPeriod;
        
        VulkanTimerQuery->Time = (float)(Timestamps[1] - Timestamps[0]) * Scale;
        VulkanTimerQuery->bResolved = true;
        return true;
    }

    float FVulkanRenderContext::GetTimerQueryTime(ITimerQuery* Query)
    {
        FVulkanTimerQuery* VulkanTimerQuery = static_cast<FVulkanTimerQuery*>(Query);

        if (!VulkanTimerQuery->bStarted)
        {
            return 0.0f;
        }
        
        if (!VulkanTimerQuery->bResolved)
        {
            while (!PollTimerQuery(Query))
            {
            }
        }
        
        VulkanTimerQuery->bStarted = false;
        DEBUG_ASSERT(VulkanTimerQuery->bResolved);
        return VulkanTimerQuery->Time;
    }

    void FVulkanRenderContext::ResetTimerQuery(ITimerQuery* Query)
    {
        FVulkanTimerQuery* VulkanTimerQuery = static_cast<FVulkanTimerQuery*>(Query);
        VulkanTimerQuery->bStarted = false;
        VulkanTimerQuery->bResolved = false;
        VulkanTimerQuery->Time = 0.0f;
    }

    // Bit order MUST match the read-back struct below.
    static constexpr VkQueryPipelineStatisticFlags GPipelineStatsFlags =
        VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT       |
        VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT     |
        VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT     |
        VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT          |
        VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT           |
        VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT   |
        VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;

    static constexpr uint32 GPipelineStatsCounterCount = 7;

    FRHIPipelineStatsQueryRef FVulkanRenderContext::CreatePipelineStatsQuery()
    {
        if (PipelineStatsQueryPool == VK_NULL_HANDLE)
        {
            VkQueryPoolCreateInfo QueryPoolInfo = {};
            QueryPoolInfo.sType                 = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            QueryPoolInfo.queryCount            = PipelineStatsAllocator.GetCapacity();
            QueryPoolInfo.queryType             = VK_QUERY_TYPE_PIPELINE_STATISTICS;
            QueryPoolInfo.pipelineStatistics    = GPipelineStatsFlags;
            VK_CHECK(vkCreateQueryPool(GetDevice()->GetDevice(), &QueryPoolInfo, VK_ALLOC_CALLBACK, &PipelineStatsQueryPool));
        }

        int32 Slot = PipelineStatsAllocator.Allocate();
        if (Slot < 0)
        {
            LOG_ERROR("Insufficient pipeline-stats query pool space");
            return nullptr;
        }

        auto Query = MakeRefCount<FVulkanPipelineStatsQuery>(PipelineStatsAllocator);
        Query->QueryIndex = Slot;
        return Query;
    }

    bool FVulkanRenderContext::PollPipelineStatsQuery(IPipelineStatsQuery* Query)
    {
        FVulkanPipelineStatsQuery* VulkanQuery = static_cast<FVulkanPipelineStatsQuery*>(Query);

        if (!VulkanQuery->bStarted)
        {
            return false;
        }

        if (VulkanQuery->bResolved)
        {
            return true;
        }

        uint64 Results[GPipelineStatsCounterCount] = {};
        VkResult Result = vkGetQueryPoolResults(
            GetDevice()->GetDevice(),
            PipelineStatsQueryPool,
            VulkanQuery->QueryIndex, 1,
            sizeof(Results), Results, sizeof(Results),
            VK_QUERY_RESULT_64_BIT);

        if (Result == VK_NOT_READY)
        {
            return false;
        }

        // Bit order must match GPipelineStatsFlags.
        VulkanQuery->Stats.InputAssemblyVertices     = Results[0];
        VulkanQuery->Stats.InputAssemblyPrimitives   = Results[1];
        VulkanQuery->Stats.VertexShaderInvocations   = Results[2];
        VulkanQuery->Stats.ClippingInvocations       = Results[3];
        VulkanQuery->Stats.ClippingPrimitives        = Results[4];
        VulkanQuery->Stats.FragmentShaderInvocations = Results[5];
        VulkanQuery->Stats.ComputeShaderInvocations  = Results[6];
        VulkanQuery->bResolved = true;
        return true;
    }

    FPipelineStats FVulkanRenderContext::GetPipelineStats(IPipelineStatsQuery* Query)
    {
        FVulkanPipelineStatsQuery* VulkanQuery = static_cast<FVulkanPipelineStatsQuery*>(Query);

        if (!VulkanQuery->bStarted)
        {
            return {};
        }

        if (!VulkanQuery->bResolved)
        {
            while (!PollPipelineStatsQuery(Query))
            {
                // Spin-wait; latency-sensitive callers should Poll first.
            }
        }

        VulkanQuery->bStarted = false;
        return VulkanQuery->Stats;
    }

    void FVulkanRenderContext::ResetPipelineStatsQuery(IPipelineStatsQuery* Query)
    {
        FVulkanPipelineStatsQuery* VulkanQuery = static_cast<FVulkanPipelineStatsQuery*>(Query);
        VulkanQuery->bStarted = false;
        VulkanQuery->bResolved = false;
        VulkanQuery->Stats = {};
    }

    void FVulkanRenderContext::AddCommandQueueWait(ECommandQueue Waiting, ECommandQueue WaitOn)
    {
        FQueue* WaitingQueue = Queues[(uint32)Waiting].get();
        FQueue* WaitOnQueue = Queues[(uint32)WaitOn].get();

        VkPipelineStageFlags WaitStage = 0;
        
        // @TODO proper wait queue.
        WaitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        
        WaitingQueue->AddWaitSemaphore(WaitOnQueue->TimelineSemaphore, WaitOnQueue->LastSubmittedID, WaitStage);
    }

    void* FVulkanRenderContext::MapBuffer(FRHIBuffer* Buffer)
    {
        FVulkanBuffer* VulkanBuffer = static_cast<FVulkanBuffer*>(Buffer);
        return VulkanDevice->GetAllocator().GetMappedMemory(VulkanBuffer);
    }

    void FVulkanRenderContext::UnMapBuffer(FRHIBuffer* Buffer)
    {
        // Persistently mapped; no-op.
    }

    FRHIBufferRef FVulkanRenderContext::CreateBuffer(const FRHIBufferDesc& Desc)
    {
        return MakeRefCount<FVulkanBuffer>(VulkanDevice, Desc);
    }

    FRHIBufferRef FVulkanRenderContext::CreateBuffer(ICommandList* RESTRICT CommandList, const void* RESTRICT InitialData, const FRHIBufferDesc& Desc)
    {
        auto Buffer = MakeRefCount<FVulkanBuffer>(VulkanDevice, Desc);
        CommandList->BeginTrackingBufferState(Buffer, EResourceStates::CopyDest);
        CommandList->WriteBuffer(Buffer, InitialData, Desc.Size);
        return Buffer;
    }

    void FVulkanRenderContext::FlushPendingDeletes()
    {
        LUMINA_PROFILE_SCOPE();
        
        for (auto& Queue : Queues)
        {
            if (Queue != nullptr)
            {
                Queue->RetireCommandBuffers();
            }
        }
    }

    void FVulkanRenderContext::OnShaderCompiled(FRHIShader* Shader, bool bAddToLibrary, bool bReloadPipelines)
    {
        if (bReloadPipelines)
        {
            PipelineCache.PostShaderRecompiled(Shader);
        }
        
        if (bAddToLibrary && Shader != nullptr)
        {
            ShaderLibrary->AddShader(Shader->GetShaderHeader().DebugName, Shader);
        }
    }

    void FVulkanRenderContext::ClearBindingCaches()
    {
        InputLayoutMap.clear();
        PipelineCache.ReleasePipelines();
    }

    FRHIInputLayoutRef FVulkanRenderContext::CreateInputLayout(const FVertexAttributeDesc* AttributeDesc, uint32 Count)
    {
        uint64 Hash = 0;
        for (uint32 i = 0; i < Count; ++i)
        {
            Hash::HashCombine(Hash, Hash::GetHash(AttributeDesc[i]));
        }

        FScopeLock Lock(LayoutMutex);
        auto it = InputLayoutMap.find(Hash);
        if (it != InputLayoutMap.end())
        {
            return it->second;
        }

        TRefCountPtr<FVulkanInputLayout> Layout = MakeRefCount<FVulkanInputLayout>(AttributeDesc, Count);
        InputLayoutMap.emplace(Hash, Layout);
        return Layout;
    }

    void FVulkanRenderContext::SetVulkanObjectName(FName Name, VkObjectType ObjectType, uint64 Handle)
    {
        #if !defined(LUMINA_SHIPPING)
        if (DebugUtils.DebugUtilsObjectNameEXT && Name != NAME_None)
        {
            VkDebugUtilsObjectNameInfoEXT NameInfo = {};
            NameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            NameInfo.objectType = ObjectType;
            NameInfo.objectHandle = Handle;
            NameInfo.pObjectName = Name.c_str();

            DebugUtils.DebugUtilsObjectNameEXT(VulkanDevice->GetDevice(), &NameInfo);
        }
        #endif
    }

    FVulkanRenderContextFunctions& FVulkanRenderContext::GetDebugUtils()
    {
        return DebugUtils;
    }
}
