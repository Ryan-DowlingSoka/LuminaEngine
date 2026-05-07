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
        
        /** Submission must happen from a single thread at a time */
        uint64 Submit(ICommandList* const* CommandLists, uint32 NumCommandLists);
        void SignalSemaphore(VkSemaphore SemaphoreToSignal) const;
        
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

    class FCommandListManager : INonCopyable
    {
    private:
        
        TArray<TConcurrentQueue<FRHICommandListRef>, (uint32)ECommandQueue::Num> CommandLists;

    public:
        FRHICommandListRef GetOrCreateCommandList(FVulkanRenderContext* RenderContext, const FCommandListInfo& CommandListInfo);

        void Enqueue(ICommandList* RetiredCommandList);
        void BulkEnqueue(ICommandList* const* RetiredCommandLists, uint32 Num, ECommandQueue QueueType);
        void Cleanup();
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
        
        bool FrameStart(const FUpdateContext& UpdateContext, uint8 InCurrentFrameIndex) override;
        bool FrameEnd(const FUpdateContext& UpdateContext, ICommandList& CmdList) override;

        uint64 GetAllocatedMemory() const override;
        uint64 GetAvailableMemory() const override;


        FORCEINLINE NODISCARD FQueue* GetQueue(ECommandQueue Type) const { return Queues[(uint32)Type].get(); }

        void ClearCommandListCache() override;
        NODISCARD FRHICommandListRef CreateCommandList(const FCommandListInfo& Info) override;
        uint64 ExecuteCommandLists(ICommandList* const* CommandLists, uint32 NumCommandLists, ECommandQueue QueueType) override;
        
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
    
    private:

        FBitSetAllocator                                    TimerQueryAllocator;
        VkQueryPool                                         TimerQueryPool = VK_NULL_HANDLE;

        FBitSetAllocator                                    PipelineStatsAllocator;
        VkQueryPool                                         PipelineStatsQueryPool = VK_NULL_HANDLE;

        FVulkanSwapchain*                                   Swapchain = nullptr;
        FVulkanDevice*                                      VulkanDevice = nullptr;
        FSpirVShaderCompiler*                               ShaderCompiler = nullptr;
        VkInstance                                          VulkanInstance = nullptr;

        TUniquePtr<RHI::FVulkanCrashTracker>                CrashTracker;
        FRHIShaderLibraryRef                                ShaderLibrary;
        
        THashMap<uint64, FRHIInputLayoutRef>                InputLayoutMap;
        THashMap<uint64, FRHISamplerRef>                    SamplerMap;
        FVulkanRenderContextFunctions                       DebugUtils;

        FCommandListManager                                 CommandListManager;
        FVulkanPipelineCache                                PipelineCache;
        FQueueArray                                         Queues;
        
        FMutex                                              LayoutMutex;
        FMutex                                              SamplerMutex;

        TBitFlags<EVulkanExtensions>                        EnabledExtensions;

        uint8                                               CurrentFrameIndex;
        FRenderContextDesc                                  Description;

    };
}
