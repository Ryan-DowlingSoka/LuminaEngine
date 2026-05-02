#pragma once

#include "PendingState.h"
#include "RenderResource.h"
#include "Core/Math/Color.h"
#include "Platform/GenericPlatform.h"

namespace Lumina
{
    struct FTextureStateExtension;
    class FQueue;
    class IRenderContext;
}

namespace Lumina
{

    enum class ECommandQueue : uint8;

    struct RUNTIME_API FCommandListInfo
    {
        size_t UploadChunkSize = 64 * 1024;
        size_t ScratchChunkSize = 64 * 1024;
        size_t ScratchMaxMemory = 1024 * 1024 * 1024;
        ECommandQueue CommandQueue = ECommandQueue::Graphics;


        static FCommandListInfo As(ECommandQueue Queue)
        {
            FCommandListInfo Ret;
            Ret.CommandQueue = Queue;
            return Ret;
        }

        static FCommandListInfo Transfer()
        {
            FCommandListInfo Ret;
            Ret.CommandQueue = ECommandQueue::Transfer;
            return Ret;
        }

        static FCommandListInfo Graphics()
        {
            FCommandListInfo Ret;
            Ret.CommandQueue = ECommandQueue::Graphics;
            return Ret;
        }

        static FCommandListInfo Compute()
        {
            FCommandListInfo Ret;
            Ret.CommandQueue = ECommandQueue::Compute;
            return Ret;
        }

    };

    struct RUNTIME_API FCommandListStatTracker
    {
        uint32 NumDrawCalls = 0;
        uint32 NumDispatchCalls = 0;
        uint32 NumBlitCommands = 0;
        uint32 NumClearCommands = 0;
        uint32 NumBufferWrites = 0;
        uint32 NumCopies = 0;
        uint32 NumBarriers = 0;
        uint32 NumPipelineSwitches = 0;
        uint32 NumRenderPasses = 0;
        uint32 NumBindings = 0;
        uint32 NumPushConstants = 0;
    };

    class RUNTIME_API ICommandList : public IRHIResource
    {
    public:

        RENDER_RESOURCE(RRT_CommandList)

        ICommandList() = default;
        virtual ~ICommandList() override = default;

        // Validation wrappers return inner list; concrete backends return self.
        virtual ICommandList* GetUnwrappedCommandList() { return this; }

        virtual void Open() = 0;
        virtual void Close() = 0;
        virtual void Executed(FQueue* Queue, uint64 SubmissionID) = 0;

        virtual void CopyImage(FRHIImage* Src, const FTextureSlice& SrcSlice, FRHIImage* Dst, const FTextureSlice& DstSlice) = 0;
        virtual void CopyImage(FRHIImage* Src, const FTextureSlice& SrcSlice, FRHIStagingImage* Dst, const FTextureSlice& DstSlice) = 0;
        virtual void CopyImage(FRHIStagingImage* Src, const FTextureSlice& SrcSlice, FRHIImage* Dst, const FTextureSlice& DstSlice) = 0;

        virtual void WriteImage(FRHIImage* Dst, uint32 ArraySlice, uint32 MipLevel, const void* Data, uint32 RowPitch, uint32 DepthPitch) = 0;

        virtual void ResolveImage(FRHIImage* Src, const FTextureSubresourceSet& SrcSubresources, FRHIImage* Dst, const FTextureSubresourceSet& DstSubresources) = 0;

        virtual void ClearImageFloat(FRHIImage* Image, FTextureSubresourceSet Subresource, const FColor& Color) = 0;
        virtual void ClearImageUInt(FRHIImage* Image, FTextureSubresourceSet Subresource, uint32 Color) = 0;

        virtual void WriteBuffer(FRHIBuffer* Buffer, const void* Data, size_t Size, size_t Offset = 0) = 0;
        virtual void FillBuffer(FRHIBuffer* Buffer, uint32 Value) = 0;
        virtual void CopyBuffer(FRHIBuffer* Source, uint64 SrcOffset, FRHIBuffer* Destination, uint64 DstOffset, uint64 CopySize) = 0;


        virtual void SetEnableUavBarriersForImage(FRHIImage* Image, bool bEnableBarriers) = 0;
        virtual void SetEnableUavBarriersForBuffer(FRHIBuffer* Buffer, bool bEnableBarriers) = 0;

        // Permanent state opts the resource out of automatic state tracking.
        virtual void SetPermanentImageState(FRHIImage* Image, EResourceStates StateBits) = 0;
        virtual void SetPermanentBufferState(FRHIBuffer* Buffer, EResourceStates StateBits) = 0;

        virtual void BeginTrackingImageState(FRHIImage* Image, FTextureSubresourceSet Subresources, EResourceStates StateBits) = 0;
        virtual void BeginTrackingBufferState(FRHIBuffer* Buffer, EResourceStates StateBits) = 0;

        virtual void SetImageState(FRHIImage* Image, FTextureSubresourceSet Subresources, EResourceStates StateBits) = 0;
        virtual void SetBufferState(FRHIBuffer* Buffer, EResourceStates StateBits) = 0;

        virtual void SetResourceStatesForBindingSet(FRHIBindingSet* BindingSet) = 0;
        virtual void SetResourceStateForRenderPass(const FRenderPassDesc& PassInfo) = 0;

        virtual void EnableAutomaticBarriers() = 0;
        virtual void DisableAutomaticBarriers() = 0;
        virtual void CommitBarriers() = 0;

        NODISCARD virtual EResourceStates GetImageSubresourceState(FRHIImage* Image, uint32 ArraySlice, uint32 MipLevel) = 0;
        NODISCARD virtual EResourceStates GetBufferState(FRHIBuffer* Buffer) = 0;


        virtual void BeginTimerQuery(ITimerQuery* Query) = 0;
        virtual void EndTimerQuery(ITimerQuery* Query) = 0;

        virtual void BeginPipelineStatsQuery(IPipelineStatsQuery* Query) = 0;
        virtual void EndPipelineStatsQuery(IPipelineStatsQuery* Query) = 0;

        virtual void AddMarker(const char* Name, const FColor& Color = FColor::Red) = 0;
        virtual void PopMarker() = 0;

        virtual void BeginRenderPass(const FRenderPassDesc& PassInfo) = 0;
        virtual void EndRenderPass() = 0;

        virtual void ClearImageColor(FRHIImage* Image, const FColor& Color) = 0;

        virtual void SetPushConstants(const void* Data, size_t ByteSize) = 0;

        virtual void SetGraphicsState(const FGraphicsState& State) = 0;

        virtual void Draw(uint32 VertexCount, uint32 InstanceCount, uint32 FirstVertex, uint32 FirstInstance) = 0;
        virtual void DrawIndexed(uint32 IndexCount, uint32 InstanceCount = 1, uint32 FirstIndex = 1, int32 VertexOffset = 0, uint32 FirstInstance = 0) = 0;
        virtual void DrawIndirect(uint32 DrawCount, uint64 Offset) = 0;
        virtual void DrawIndexedIndirect(uint32 DrawCount, uint64 Offset) = 0;

        virtual void SetComputeState(const FComputeState& State) = 0;
        virtual void Dispatch(uint32 GroupCountX, uint32 GroupCountY, uint32 GroupCountZ) = 0;

        NODISCARD virtual const FCommandListInfo& GetCommandListInfo() const = 0;
        NODISCARD virtual FPendingCommandState& GetPendingCommandState() = 0;
        NODISCARD virtual const FCommandListStatTracker& GetCommandListStats() const = 0;
    };
}