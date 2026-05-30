#pragma once
#include "CommandList.h"
#include "RenderResource.h"
#include "RenderTypes.h"
#include "RHIFwd.h"
#include "Containers/Array.h"
#include "Containers/String.h"
#include "Types/BitFlags.h"
#include "Core/UpdateContext.h"


namespace Lumina::RHI
{
    class ICrashTracker;
}

namespace Lumina
{
    class IShaderCompiler;
    struct FCommandListInfo;
    class ICommandList;
}

namespace Lumina
{

    struct FRenderContextDesc
    {
        bool bValidation = false;
        bool bDebugUtils = true;
    };

    // API-neutral GPU device summary, surfaced to tools without exposing the backend.
    struct FGPUDeviceInfo
    {
        FString Name;       // Adapter name, e.g. "NVIDIA GeForce RTX 4080".
        FString APIName;    // Backend + version, e.g. "Vulkan 1.3.250".
        bool    bDiscrete = false;
    };

    // One GPU memory heap. Usage is OS-reported for the process; Allocated/Block come from the
    // allocator (Block >= Allocated, the gap is fragmentation/reserve).
    struct FGPUMemoryHeapStats
    {
        uint32 HeapIndex       = 0;
        bool   bDeviceLocal    = false;
        uint64 BudgetBytes     = 0;
        uint64 UsageBytes      = 0;
        uint64 AllocatedBytes  = 0;
        uint64 BlockBytes      = 0;
        uint32 BlockCount      = 0;
        uint32 AllocationCount = 0;
    };

    struct FGPUMemoryStats
    {
        uint64 TotalBudget      = 0;
        uint64 TotalUsage       = 0;
        uint64 TotalAllocated   = 0;
        uint64 TotalBlockBytes  = 0;
        uint32 TotalAllocations = 0;
        uint32 TotalBlocks      = 0;
        TFixedVector<FGPUMemoryHeapStats, 16> Heaps;   // VK_MAX_MEMORY_HEAPS == 16.
    };

    class IRenderContext
    {
    public:
        virtual ~IRenderContext() = default;

        virtual bool Initialize(const FRenderContextDesc& Desc) = 0;
        virtual void Deinitialize() = 0;

        virtual void WaitIdle() = 0;

        virtual void SetVSyncEnabled(bool bEnable) = 0;
        virtual bool IsVSyncEnabled() const = 0;

        virtual void HandleDeviceLost() = 0;

        

        virtual const FRenderContextDesc& GetRenderContextDescription() const = 0;


        virtual bool FrameStart(uint8 InCurrentFrameIndex) = 0;
        virtual bool FrameEnd(ICommandList& CmdList) = 0;
        virtual void WaitForGPU() = 0;

        // Acquire/release exclusive access to a queue from outside the RHI, e.g. ImGui's
        // multi-viewport backend submitting/presenting on the same VkQueue we drive.
        virtual void LockQueueForExternalAccess(ECommandQueue Queue) = 0;
        virtual void UnlockQueueForExternalAccess(ECommandQueue Queue) = 0;
        

        NODISCARD virtual uint64 GetAllocatedMemory() const = 0;
        NODISCARD virtual uint64 GetAvailableMemory() const = 0;

        // Full per-heap GPU memory breakdown (device truth), abstracted from the backend.
        virtual void GetGPUMemoryStats(FGPUMemoryStats& Out) const = 0;

        // Adapter name / API version for display.
        NODISCARD virtual FGPUDeviceInfo GetDeviceInfo() const = 0;


        virtual FRHICommandListRef CreateCommandList(const FCommandListInfo& Info) = 0;
        virtual uint64 ExecuteCommandLists(ICommandList* const* CommandLists, uint32 NumCommandLists, ECommandQueue QueueType) = 0;


        NODISCARD virtual FRHIEventQueryRef CreateEventQuery() = 0;
        virtual void SetEventQuery(IEventQuery* Query, ECommandQueue Queue) = 0;
        virtual void ResetEventQuery(IEventQuery* Query) = 0;
        virtual void WaitEventQuery(IEventQuery* Query) = 0;
        virtual bool PollEventQuery(IEventQuery* Query) = 0;
        
        NODISCARD virtual FRHITimerQueryRef CreateTimerQuery() = 0;
        NODISCARD virtual bool PollTimerQuery(ITimerQuery* Query) = 0;
        NODISCARD virtual float GetTimerQueryTime(ITimerQuery* Query) = 0;
        virtual void ResetTimerQuery(ITimerQuery* Query) = 0;

        NODISCARD virtual FRHIPipelineStatsQueryRef CreatePipelineStatsQuery() = 0;
        NODISCARD virtual bool PollPipelineStatsQuery(IPipelineStatsQuery* Query) = 0;
        NODISCARD virtual FPipelineStats GetPipelineStats(IPipelineStatsQuery* Query) = 0;
        virtual void ResetPipelineStatsQuery(IPipelineStatsQuery* Query) = 0;

        virtual void AddCommandQueueWait(ECommandQueue Waiting, ECommandQueue WaitOn) = 0;
        

        NODISCARD virtual void* MapBuffer(FRHIBuffer* Buffer) = 0;
        NODISCARD virtual void UnMapBuffer(FRHIBuffer* Buffer) = 0;
        NODISCARD virtual FRHIBufferRef CreateBuffer(const FRHIBufferDesc& Description) = 0;
        NODISCARD virtual FRHIBufferRef CreateBuffer(ICommandList* CommandList, const void* InitialData, const FRHIBufferDesc& Description) = 0;
        NODISCARD virtual uint64 GetAlignedSizeForBuffer(uint64 Size, TBitFlags<EBufferUsageFlags> Usage) = 0;


        

        NODISCARD virtual FRHIViewportRef CreateViewport(const FUIntVector2& Size, FString&& DebugName) = 0;

        
        
        NODISCARD virtual FRHIVertexShaderRef CreateVertexShader(const FShaderHeader& Shader) = 0;
        NODISCARD virtual FRHIPixelShaderRef CreatePixelShader(const FShaderHeader& Shader) = 0;
        NODISCARD virtual FRHIComputeShaderRef CreateComputeShader(const FShaderHeader& Shader) = 0;
        NODISCARD virtual FRHIGeometryShaderRef CreateGeometryShader(const FShaderHeader& Shader) = 0;

        NODISCARD virtual IShaderCompiler* GetShaderCompiler() const = 0;
        NODISCARD virtual FRHIShaderLibraryRef GetShaderLibrary() const = 0;
        

        virtual void ClearBindingCaches() = 0;
        NODISCARD virtual FRHIDescriptorTableRef CreateDescriptorTable(FRHIBindingLayout* InLayout) = 0;
        virtual void ResizeDescriptorTable(FRHIDescriptorTable* Table, uint32 NewSize, bool bKeepContents) = 0;
        virtual bool WriteDescriptorTable(FRHIDescriptorTable* Table, const FBindingSetItem& Binding) = 0;
        NODISCARD virtual FRHIInputLayoutRef CreateInputLayout(const FVertexAttributeDesc* AttributeDesc, uint32 Count) = 0;
        NODISCARD virtual FRHIBindingLayoutRef CreateBindingLayout(const FBindingLayoutDesc& Desc) = 0;
        NODISCARD virtual FRHIBindingLayoutRef CreateBindlessLayout(const FBindlessLayoutDesc& Desc) = 0;
        NODISCARD virtual FRHIBindingSetRef CreateBindingSet(const FBindingSetDesc& Desc, FRHIBindingLayout* InLayout) = 0;
        virtual void CreateBindingSetAndLayout(const TBitFlags<ERHIShaderType>& Visibility, uint16 Binding, const FBindingSetDesc& Desc, FRHIBindingLayoutRef& OutLayout, FRHIBindingSetRef& OutBindingSet) = 0;
        NODISCARD virtual FRHIComputePipelineRef CreateComputePipeline(const FComputePipelineDesc& Desc) = 0;
        NODISCARD virtual FRHIGraphicsPipelineRef CreateGraphicsPipeline(const FGraphicsPipelineDesc& Desc, const FRenderPassDesc& RenderPassDesc) = 0;

        
        NODISCARD virtual RHI::ICrashTracker& GetCrashTracker() const = 0;
        

        virtual void OnShaderCompiled(FRHIShader* Shader, bool bAddToLibrary, bool bReloadPipelines) = 0;

        virtual void SetObjectName(IRHIResource* Resource, const char* Name, EAPIResourceType Type = EAPIResourceType::Default) = 0;

        NODISCARD virtual FRHIStagingImageRef CreateStagingImage(const FRHIImageDesc& Desc, ERHIAccess Access) = 0;
        NODISCARD virtual void* MapStagingTexture(FRHIStagingImage* Image, const FTextureSlice& slice, ERHIAccess Access, size_t* OutRowPitch) = 0;
        virtual void UnMapStagingTexture(FRHIStagingImage* Image) = 0;
        
        NODISCARD virtual FRHIImageRef CreateImage(const FRHIImageDesc& ImageSpec) = 0;
        NODISCARD virtual FRHISamplerRef CreateSampler(const FSamplerDesc& SamplerDesc) = 0;

        uint64 ExecuteCommandList(ICommandList* CommandList, ECommandQueue ExecutionQueue = ECommandQueue::Graphics)
        {
            return ExecuteCommandLists(&CommandList, 1, ExecutionQueue);
        }


        
        virtual void FlushPendingDeletes() = 0;
    
    protected:
        
    };
    
}
