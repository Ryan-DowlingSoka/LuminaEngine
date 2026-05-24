#pragma once

#include <Containers/Array.h>
#include <volk/volk.h>
#include <tracy/TracyVulkan.hpp>
#include "TrackedCommandBuffer.h"
#include "VulkanPipelineCache.h"
#include "VulkanResources.h"
#include "concurrentqueue/concurrentqueue.h"
#include "Core/Threading/Thread.h"
#include "Memory/SmartPtr.h"
#include "Renderer/RenderContext.h"
#include "Renderer/ErrorHandling/Vulkan/VulkanCrashTracker.h"
#include "VkBootstrap.h"
#include "Containers/BitSetAllocator.h"
#include "Types/BitFlags.h"

namespace Lumina
{
    class FSpirVShaderCompiler;
    class FVulkanCommandList;
    class FVulkanSwapchain;
    class FVulkanDevice;
    enum class ECommandQueue : uint8;
}

namespace Lumina
{
    extern VkAllocationCallbacks GVulkanAllocationCallbacks;
    #define VK_ALLOC_CALLBACK &GVulkanAllocationCallbacks

    
    struct FVulkanRenderContextFunctions
    {
        VkDebugUtilsMessengerEXT DebugMessenger = nullptr;
        PFN_vkSetDebugUtilsObjectNameEXT DebugUtilsObjectNameEXT = nullptr;
    };

    enum class EVulkanExtensions : uint8
    {
        None,
        PushDescriptors,
        ConservativeRasterization,
        ViewportIndexLayer,
        UnifiedImageLayouts,
        ExtendedDynamicState3,
        MemoryPriority,
        PageableDeviceLocalMemory,
        FragmentShadingRate,
        HostImageCopy,
    };

    // Pipeline states moved to dynamic so descs differing only in these no longer mint a
    // separate PSO. Cull/front-face/depth are core in Vulkan 1.4 (always on); the EDS3
    // states are gated per-feature on VK_EXT_extended_dynamic_state3 and fall back to
    // baked-in-the-PSO when unsupported.
    struct FDynamicPipelineStates
    {
        bool bCullMode           = false;
        bool bFrontFace          = false;
        bool bDepthTestEnable    = false;
        bool bDepthWriteEnable   = false;
        bool bDepthCompareOp     = false;
        bool bPolygonMode        = false;
        bool bColorBlendEnable   = false;
        bool bColorBlendEquation = false;
        bool bColorWriteMask     = false;
    };

    VkImageAspectFlags GuessImageAspectFlags(VkFormat Format);
    
    class FQueue : public IDeviceChild
    {
    public:
        
        FQueue(FVulkanRenderContext* InRenderContext, VkQueue InQueue, uint32 InQueueFamilyIndex, ECommandQueue InType);
        ~FQueue();
        
        FQueue(const FQueue&) = delete;
        FQueue& operator = (const FQueue&) = delete;
        FQueue(FQueue&&) = delete;
        FQueue& operator = (FQueue&&) = delete;
        

        // Free-threaded; called from ICommandList::Open.
        TRefCountPtr<FTrackedCommandBuffer> GetOrCreateCommandBuffer();

        void RetireCommandBuffers();
        
        uint64 GetCompletedInstance() const;
        
        // Submit from one thread at a time. ExtraWait/ExtraSignal are binary semaphores bound to
        // this exact submit (merged under the lock) so a concurrent submit can't steal them.
        uint64 Submit(ICommandList* const* CommandLists, uint32 NumCommandLists,
                      VkSemaphore ExtraWaitSemaphore = VK_NULL_HANDLE,
                      VkPipelineStageFlags ExtraWaitStage = 0,
                      VkSemaphore ExtraSignalSemaphore = VK_NULL_HANDLE);
        void SignalSemaphore(VkSemaphore SemaphoreToSignal);

        // External sync for the VkQueue; vkQueuePresentKHR must serialize
        // against vkQueueSubmit on the same queue.
        VkResult Present(const VkPresentInfoKHR& PresentInfo);

        void WaitIdle();
        uint64 UpdateLastFinishID();
        bool PollCommandList(uint64 CommandListID);
        bool WaitCommandList(uint64 CommandListID, uint64 Timeout);
        
        void AddSignalSemaphore(VkSemaphore Semaphore, uint64 Value);
        void AddWaitSemaphore(VkSemaphore Semaphore, uint64 Value, VkPipelineStageFlags Stage);

        FVulkanRenderContext*               RenderContext = nullptr;
        VkQueue                             Queue = VK_NULL_HANDLE;
        VkSemaphore                         TimelineSemaphore = VK_NULL_HANDLE;
    
        TAtomic<uint64>                     LastRecordingID{0};
    
        uint64                              LastSubmittedID = 0;
        uint64                              LastFinishedID = 0;
    

        TFixedVector<TRefCountPtr<FTrackedCommandBuffer>, 32> CommandBuffersInFlight;
        TConcurrentQueue<TRefCountPtr<FTrackedCommandBuffer>> CommandBufferPool;
    
        TFixedVector<VkSemaphore, 4>            WaitSemaphores;
        TFixedVector<uint64, 4>                 WaitSemaphoreValues;
        TFixedVector<VkPipelineStageFlags, 4>   WaitStageFlags;
        TFixedVector<VkSemaphore, 4>            SignalSemaphores;
        TFixedVector<uint64, 4>                 SignalSemaphoreValues;

        TracyLockable(FMutex,       Mutex);
    
        ECommandQueue               Type = ECommandQueue::Num;
        uint32                      QueueFamilyIndex = 0;
    };
    
    
    class FVulkanRenderContext : public IRenderContext
    {
    public:

        using FQueueArray = TArray<TUniquePtr<FQueue>, (uint32)ECommandQueue::Num>;
        
        FVulkanRenderContext();
        
        FVulkanRenderContext(const FVulkanRenderContext&) = delete;
        FVulkanRenderContext& operator = (const FVulkanRenderContext&) = delete;
        FVulkanRenderContext(FVulkanRenderContext&&) = delete;
        FVulkanRenderContext& operator = (FVulkanRenderContext&&) = delete;
        
        bool Initialize(const FRenderContextDesc& Desc) override;
        void Deinitialize() override;
        
        void SetVSyncEnabled(bool bEnable) override;
        bool IsVSyncEnabled() const override;

        void HandleDeviceLost() override;

        void WaitIdle() override;
        bool CreateDevice(vkb::Instance Instance);
        
        bool FrameStart(uint8 InCurrentFrameIndex) override;
        bool FrameEnd(ICommandList& CmdList) override;
        void WaitForGPU() override;
        void LockQueueForExternalAccess(ECommandQueue Queue) override;
        void UnlockQueueForExternalAccess(ECommandQueue Queue) override;

        uint64 GetAllocatedMemory() const override;
        uint64 GetAvailableMemory() const override;
        void GetGPUMemoryStats(FGPUMemoryStats& Out) const override;
        FGPUDeviceInfo GetDeviceInfo() const override;


        FORCEINLINE NODISCARD FQueue* GetQueue(ECommandQueue Type) const { return Queues[(uint32)Type].get(); }

        NODISCARD FRHICommandListRef CreateCommandList(const FCommandListInfo& Info) override;
        uint64 ExecuteCommandLists(ICommandList* const* CommandLists, uint32 NumCommandLists, ECommandQueue QueueType) override;
        uint64 SubmitWithSemaphores(ICommandList* const* CommandLists, uint32 NumCommandLists, ECommandQueue QueueType, VkSemaphore ExtraWaitSemaphore, VkPipelineStageFlags ExtraWaitStage, VkSemaphore ExtraSignalSemaphore);
        
        NODISCARD VkInstance GetVulkanInstance() const { return VulkanInstance; }
        NODISCARD FVulkanDevice* GetDevice() const { return VulkanDevice; }
        NODISCARD FVulkanSwapchain* GetSwapchain() const { return Swapchain; }
        
        //-------------------------------------------------------------------------------------

        NODISCARD FRHIEventQueryRef CreateEventQuery() override;
        void SetEventQuery(IEventQuery* Query, ECommandQueue Queue) override;
        void ResetEventQuery(IEventQuery* Query) override;
        bool PollEventQuery(IEventQuery* Query) override;
        void WaitEventQuery(IEventQuery* Query) override;
        
        FRHITimerQueryRef CreateTimerQuery() override;
        bool PollTimerQuery(ITimerQuery* Query) override;
        float GetTimerQueryTime(ITimerQuery* Query) override;
        void ResetTimerQuery(ITimerQuery* Query) override;

        FRHIPipelineStatsQueryRef CreatePipelineStatsQuery() override;
        bool PollPipelineStatsQuery(IPipelineStatsQuery* Query) override;
        FPipelineStats GetPipelineStats(IPipelineStatsQuery* Query) override;
        void ResetPipelineStatsQuery(IPipelineStatsQuery* Query) override;

        void AddCommandQueueWait(ECommandQueue Waiting, ECommandQueue WaitOn) override;
        
        //-------------------------------------------------------------------------------------

        NODISCARD void* MapBuffer(FRHIBuffer* Buffer) override;
        NODISCARD void UnMapBuffer(FRHIBuffer* Buffer) override;
        NODISCARD FRHIBufferRef CreateBuffer(const FRHIBufferDesc& Description) override;
        NODISCARD FRHIBufferRef CreateBuffer(ICommandList* CommandList, const void* InitialData, const FRHIBufferDesc& Description) override;
        NODISCARD uint64 GetAlignedSizeForBuffer(uint64 Size, TBitFlags<EBufferUsageFlags> Usage) override;

        
        //-------------------------------------------------------------------------------------

        NODISCARD FRHIViewportRef CreateViewport(const glm::uvec2& Size, FString&& DebugName) override;
        
        NODISCARD FRHIStagingImageRef CreateStagingImage(const FRHIImageDesc& Desc, ERHIAccess Access) override;
        void* MapStagingTexture(FRHIStagingImage* Image, const FTextureSlice& slice, ERHIAccess Access, size_t* OutRowPitch) override;
        void UnMapStagingTexture(FRHIStagingImage* Image) override;
        
        NODISCARD FRHIImageRef CreateImage(const FRHIImageDesc& ImageSpec) override;
        NODISCARD FRHISamplerRef CreateSampler(const FSamplerDesc& SamplerDesc) override;
        
        
        //-------------------------------------------------------------------------------------

        NODISCARD FRHIVertexShaderRef CreateVertexShader(const FShaderHeader& Shader) override;
        NODISCARD FRHIPixelShaderRef CreatePixelShader(const FShaderHeader& Shader) override;
        NODISCARD FRHIComputeShaderRef CreateComputeShader(const FShaderHeader& Shader) override;
        NODISCARD FRHIGeometryShaderRef CreateGeometryShader(const FShaderHeader& Shader) override;

        NODISCARD IShaderCompiler* GetShaderCompiler() const override;
        NODISCARD FRHIShaderLibraryRef GetShaderLibrary() const override;
        void OnShaderCompiled(FRHIShader* Shader, bool bAddToLibrary, bool bReloadPipelines) override;

        
        //-------------------------------------------------------------------------------------

        void ClearBindingCaches() override;
        NODISCARD FRHIDescriptorTableRef CreateDescriptorTable(FRHIBindingLayout* InLayout) override;
        void ResizeDescriptorTable(FRHIDescriptorTable* Table, uint32 NewSize, bool bKeepContents) override;
        bool WriteDescriptorTable(FRHIDescriptorTable* Table, const FBindingSetItem& Binding) override;
        NODISCARD FRHIInputLayoutRef CreateInputLayout(const FVertexAttributeDesc* AttributeDesc, uint32 Count) override;
        NODISCARD FRHIBindingLayoutRef CreateBindingLayout(const FBindingLayoutDesc& Desc) override;
        NODISCARD FRHIBindingLayoutRef CreateBindlessLayout(const FBindlessLayoutDesc& Desc) override;
        NODISCARD FRHIBindingSetRef CreateBindingSet(const FBindingSetDesc& Desc, FRHIBindingLayout* InLayout) override;
        void CreateBindingSetAndLayout(const TBitFlags<ERHIShaderType>& Visibility, uint16 Binding, const FBindingSetDesc& Desc, FRHIBindingLayoutRef& OutLayout, FRHIBindingSetRef& OutBindingSet) override;
        NODISCARD FRHIComputePipelineRef CreateComputePipeline(const FComputePipelineDesc& Desc) override;
        NODISCARD FRHIGraphicsPipelineRef CreateGraphicsPipeline(const FGraphicsPipelineDesc& Desc, const FRenderPassDesc& RenderPassDesc) override;

        NODISCARD const FRenderContextDesc& GetRenderContextDescription() const override { return Description; }


        RHI::ICrashTracker& GetCrashTracker() const override;
        
        NODISCARD VkQueryPool GetTimerQueryPool() const { return TimerQueryPool; }
        NODISCARD VkQueryPool GetPipelineStatsQueryPool() const { return PipelineStatsQueryPool; }

        //-------------------------------------------------------------------------------------

        void SetObjectName(IRHIResource* Resource, const char* Name, EAPIResourceType Type) override;
        
        void FlushPendingDeletes() override;
        
        void SetVulkanObjectName(FName Name, VkObjectType ObjectType, uint64 Handle);
        FVulkanRenderContextFunctions& GetDebugUtils();

        // VK_KHR_unified_image_layouts: when enabled, GENERAL is universally optimal,
        // so every image stays in GENERAL for its whole life and no layout transitions
        // are issued (the swapchain still needs PRESENT_SRC to present).
        FORCEINLINE bool SupportsUnifiedImageLayouts() const { return EnabledExtensions.IsFlagSet(EVulkanExtensions::UnifiedImageLayouts); }

        // VK_EXT_memory_priority (not core in 1.4): gates VMA's MEMORY_PRIORITY allocator bit;
        // prerequisite for pageable device-local memory.
        FORCEINLINE bool SupportsMemoryPriority() const { return EnabledExtensions.IsFlagSet(EVulkanExtensions::MemoryPriority); }

        // VK_EXT_pageable_device_local_memory (not core): driver pages VRAM in/out by priority.
        FORCEINLINE bool SupportsPageableDeviceLocalMemory() const { return EnabledExtensions.IsFlagSet(EVulkanExtensions::PageableDeviceLocalMemory); }

        // VK_KHR_fragment_shading_rate (not core): per-draw / attachment variable-rate shading.
        FORCEINLINE bool SupportsFragmentShadingRate() const { return EnabledExtensions.IsFlagSet(EVulkanExtensions::FragmentShadingRate); }

        // hostImageCopy (core in 1.4): host<->image copies without a staging buffer.
        FORCEINLINE bool SupportsHostImageCopy() const { return EnabledExtensions.IsFlagSet(EVulkanExtensions::HostImageCopy); }

        // Max fragment shading rate the device supports (maxFragmentSize); {1,1} if FSR is off.
        // Pipeline creation clamps requested rates to this so an unsupported pick can't fault.
        FORCEINLINE VkExtent2D GetMaxShadingRate() const { return ShadingRateMax; }

        // Collapses any optimal layout to GENERAL when unified layouts are active.
        // PRESENT_SRC and UNDEFINED pass through (present is the one real transition,
        // UNDEFINED is the initial discard).
        FORCEINLINE VkImageLayout GetEffectiveImageLayout(VkImageLayout Layout) const
        {
            if (SupportsUnifiedImageLayouts() && Layout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && Layout != VK_IMAGE_LAYOUT_UNDEFINED)
            {
                return VK_IMAGE_LAYOUT_GENERAL;
            }
            return Layout;
        }

        // Which pipeline states are dynamic on this device (see FDynamicPipelineStates).
        // Read by both pipeline creation (which states to declare dynamic + canonicalize
        // out of the cache key) and SetGraphicsState (which vkCmdSet* to issue).
        FORCEINLINE const FDynamicPipelineStates& GetDynamicPipelineStates() const { return DynamicPipelineStates; }

    private:

        FBitSetAllocator                                    TimerQueryAllocator;
        VkQueryPool                                         TimerQueryPool = VK_NULL_HANDLE;

        FBitSetAllocator                                    PipelineStatsAllocator;
        VkQueryPool                                         PipelineStatsQueryPool = VK_NULL_HANDLE;

        VkExtent2D                                          ShadingRateMax = { 1, 1 };

        FVulkanSwapchain*                                   Swapchain = nullptr;
        FVulkanDevice*                                      VulkanDevice = nullptr;
        FSpirVShaderCompiler*                               ShaderCompiler = nullptr;
        VkInstance                                          VulkanInstance = nullptr;

        TUniquePtr<RHI::FVulkanCrashTracker>                CrashTracker;
        FRHIShaderLibraryRef                                ShaderLibrary;
        
        THashMap<uint64, FRHIInputLayoutRef>                InputLayoutMap;
        THashMap<uint64, FRHISamplerRef>                    SamplerMap;
        FVulkanRenderContextFunctions                       DebugUtils;
        
        FVulkanPipelineCache                                PipelineCache;
        FQueueArray                                         Queues;
        
        FMutex                                              LayoutMutex;
        FMutex                                              SamplerMutex;

        TBitFlags<EVulkanExtensions>                        EnabledExtensions;
        FDynamicPipelineStates                              DynamicPipelineStates;

        uint8                                               CurrentFrameIndex;
        FRenderContextDesc                                  Description;

    };
}
