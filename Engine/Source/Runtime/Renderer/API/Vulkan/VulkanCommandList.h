#pragma once


#include "TrackedCommandBuffer.h"
#include "Memory/SmartPtr.h"
#include "Renderer/CommandList.h"
#include "Renderer/StateTracking.h"
#include "Renderer/GPUProfiler/GPUProfiler.h"


namespace Lumina
{
    class FUploadManager;

    VkImageLayout ConvertRHIAccessToVkImageLayout(ERHIAccess Access);

    struct FResourceStateMapping
    {
        EResourceStates         State;
        VkPipelineStageFlags    StageFlags;
        VkAccessFlags           AccessMask;
        VkImageLayout           ImageLayout;
        
        FResourceStateMapping(EResourceStates InState, VkPipelineStageFlags InStageFlags, VkAccessFlags InAccessMask, VkImageLayout InImageLayout)
            : State(InState), StageFlags(InStageFlags), AccessMask(InAccessMask), ImageLayout(InImageLayout)
        {}
        
    };

    struct FResourceStateMapping2 // KHR_synchronization2
    {
         EResourceStates State;
         VkImageLayout ImageLayout;
         VkPipelineStageFlags2 StageFlags;
         VkAccessFlags2 AccessMask;
        
         FResourceStateMapping2(EResourceStates InState, VkImageLayout ImageLayout, VkPipelineStageFlags2 InStageFlags, VkAccessFlags2 InAccessMask)
            : State(InState), ImageLayout(ImageLayout), StageFlags(InStageFlags), AccessMask(InAccessMask)
        {}
    };
    
    class FVulkanCommandList : public ICommandList
    {
    public:

        friend class FQueue;
        
        FVulkanCommandList(FVulkanRenderContext* InContext, const FCommandListInfo& InInfo);

        void Open() override;
        void Close() override;
        void Executed(FQueue* Queue, uint64 SubmissionID) override;
        void KeepAlive(IRHIResource* Resource) override;

        void CopyImage(FRHIImage* Src, const FTextureSlice& SrcSlice, FRHIImage* Dst, const FTextureSlice& DstSlice) override;
        void CopyImage(FRHIImage* Src, const FTextureSlice& SrcSlice, FRHIStagingImage* Dst, const FTextureSlice& DstSlice) override;
        void CopyImage(FRHIStagingImage* Src, const FTextureSlice& SrcSlice, FRHIImage* Dst, const FTextureSlice& DstSlice) override;
        void WriteImage(FRHIImage* Dst, uint32 ArraySlice, uint32 MipLevel, const void* Data, uint32 RowPitch, uint32 DepthPitch) override;
        void WriteImageRegion(FRHIImage* Dst, uint32 ArraySlice, uint32 MipLevel, uint32 OffsetX, uint32 OffsetY, uint32 Width, uint32 Height, const void* Data, uint32 RowPitch) override;
        void ResolveImage(FRHIImage* Src, const FTextureSubresourceSet& SrcSubresources, FRHIImage* Dst, const FTextureSubresourceSet& DstSubresources) override;
        void ClearImageFloat(FRHIImage* Image, FTextureSubresourceSet Subresource, const FColor& Color) override;
        void ClearImageUInt(FRHIImage* Image, FTextureSubresourceSet Subresource, uint32 Color) override;

        void WriteBuffer(FRHIBuffer* Buffer, const void* Data, size_t Size, size_t Offset = 0) override;
        void FillBuffer(FRHIBuffer* Buffer, uint32 Value) override;
        void CopyBuffer(FRHIBuffer* Source, uint64 SrcOffset, FRHIBuffer* Destination, uint64 DstOffset, uint64 CopySize) override;

        FTransientAlloc AllocateTransient(uint64 Size, uint32 Alignment = 16) override;

        void WriteDynamicBuffer(FRHIBuffer* Buffer, const void* Data, SIZE_T Size);
        void FlushDynamicBufferWrites();
        void SubmitDynamicBuffers(uint64 RecordingID, uint64 SubmittedID);

        void UpdateComputeDynamicBuffers();
        void UpdateGraphicsDynamicBuffers();

        void SetEnableUavBarriersForImage(FRHIImage* Image, bool bEnableBarriers) override;
        void SetEnableUavBarriersForBuffer(FRHIBuffer* Buffer, bool bEnableBarriers) override;
        
        void SetPermanentImageState(FRHIImage* Image,EResourceStates StateBits) override;
        void SetPermanentBufferState(FRHIBuffer* Buffer, EResourceStates StateBits) override;

        void BeginTrackingImageState(FRHIImage* Image, FTextureSubresourceSet Subresources, EResourceStates StateBits) override;
        void BeginTrackingBufferState(FRHIBuffer* Buffer, EResourceStates StateBits) override;

        void SetImageState(FRHIImage* Image, FTextureSubresourceSet Subresources, EResourceStates StateBits) override;
        void SetBufferState(FRHIBuffer* Buffer, EResourceStates StateBits) override;
        
        EResourceStates GetImageSubresourceState(FRHIImage* Image, uint32 ArraySlice, uint32 MipLevel) override;
        EResourceStates GetBufferState(FRHIBuffer* Buffer) override;

        void EnableAutomaticBarriers() override;
        void DisableAutomaticBarriers() override;
        
        void CommitBarriers() override;
        void SetResourceStatesForBindingSet(FRHIBindingSet* BindingSet) override;
        void SetResourceStateForRenderPass(const FRenderPassDesc& PassInfo) override;

        void BeginTimerQuery(ITimerQuery* Query) override;
        void EndTimerQuery(ITimerQuery* Query) override;
        void BeginPipelineStatsQuery(IPipelineStatsQuery* Query) override;
        void EndPipelineStatsQuery(IPipelineStatsQuery* Query) override;
        void AddMarker(const char* Name, const FColor& Color = FColor::Red) override;
        void PopMarker() override;
        void BeginProfilerZone(const char* Name, const FColor& Color = FColor::White) override;
        void EndProfilerZone() override;

        void BeginRenderPass(const FRenderPassDesc& PassInfo) override;
        void EndRenderPass() override;
        
        void ClearImageColor(FRHIImage* Image, const FColor& Color) override;

        void BindBindingSets(VkPipelineBindPoint BindPoint, VkPipelineLayout PipelineLayout, const TFixedVector<FRHIBindingSet*, 4>& BindingSets);

        void SetPushConstants(const void* Data, SIZE_T ByteSize) override;

        static VkViewport ToVkViewport(float MinX, float MinY, float MinZ, float MaxX, float MaxY, float MaxZ);
        static VkRect2D ToVkScissorRect(uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY);
        
        void SetGraphicsState(const FGraphicsState& State) override;
        void SetScissor(const FRect& Rect) override;
        void SetLineWidth(float Width) override;
        void Draw(uint32 VertexCount, uint32 InstanceCount, uint32 FirstVertex, uint32 FirstInstance) override;
        void DrawIndexed(uint32 IndexCount, uint32 InstanceCount, uint32 FirstIndex, int32 VertexOffset, uint32 FirstInstance) override;
        void DrawIndirect(uint32 DrawCount, uint64 Offset) override;
        void DrawIndexedIndirect(uint32 DrawCount, uint64 Offset) override;
        void DrawIndirect(FRHIBuffer* ArgsBuffer, uint64 Offset, uint32 DrawCount, uint32 Stride) override;
        void DrawIndexedIndirect(FRHIBuffer* ArgsBuffer, uint64 Offset, uint32 DrawCount, uint32 Stride) override;
        using ICommandList::DrawIndirect;
        using ICommandList::DrawIndexedIndirect;

        void SetComputeState(const FComputeState& State) override;
        void Dispatch(uint32 GroupCountX, uint32 GroupCountY, uint32 GroupCountZ) override;


        void CommitBarriersInternal();

        void TrackResourcesAndBarriers(const FGraphicsState& State);

        void RequireTextureState(FRHIImage* Texture, FTextureSubresourceSet Subresources, EResourceStates StateBits);

        void RequireBufferState(FRHIBuffer* Buffer, EResourceStates StateBits);
        
        
        const FCommandListInfo& GetCommandListInfo() const override { return Info; }

        FPendingCommandState& GetPendingCommandState() override { return PendingState; }
        
        const FCommandListStatTracker& GetCommandListStats() const override { return CommandListStatLastFrame; }
                

    protected:
        
        void* GetAPIResourceImpl(EAPIResourceType) override;
        
    private:
        
        // Tracks the distinct dynamic buffers written per frame (Scene/Light/Instance/Bone/...);
        // inline 2 spilled to the heap every frame. Bounded by distinct buffers, not draws.
        TFixedHashMap<FRHIBufferRef, FDynamicBufferWrite, 16>   DynamicBufferWrites;

        FGraphicsState                                          CurrentGraphicsState;
        FComputeState                                           CurrentComputeState;

        FVulkanRenderContext*                                   RenderContext = nullptr;
        TUniquePtr<FUploadManager>                              UploadManager;
        TUniquePtr<FUploadManager>                              ScratchManager;
        
        // Last transient-ring chunk referenced this recording.
        FRHIBuffer*                                             LastTransientChunk = nullptr;


        FCommandListStatTracker                                 CommandListStats;
        FCommandListStatTracker                                 CommandListStatLastFrame;
        TRefCountPtr<FTrackedCommandBuffer>                     CurrentCommandBuffer;
                                                                
        FCommandListResourceStateTracker                        StateTracker;
        // Reason tag stamped onto barriers captured by the GPU profiler (debug-only).
        EGPUBarrierPhase                                        CurrentBarrierPhase = EGPUBarrierPhase::Pass;
        FPendingCommandState                                    PendingState;
        FCommandListInfo                                        Info;
        VkShaderStageFlags                                      PushConstantVisibility;
        VkPipelineLayout                                        CurrentPipelineLayout;

        #if defined(TRACY_ENABLE)
        static constexpr uint32                                 MaxTracyZoneDepth = 32;
        alignas(tracy::VkCtxScope) uint8                        TracyZoneStorage[MaxTracyZoneDepth][sizeof(tracy::VkCtxScope)] = {};
        uint32                                                  TracyZoneDepth = 0;
        #endif
    };
    
}
