#pragma once

#include "CommandList.h"

namespace Lumina
{
    // Validation decorator; only inserted when bValidation is true. Submission unwraps via GetUnwrappedCommandList.
    class RUNTIME_API FCommandListValidator final : public ICommandList
    {
    public:

        explicit FCommandListValidator(FRHICommandListRef InInner);

        ICommandList* GetUnwrappedCommandList() override { return Inner.GetReference(); }

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
        void FillBuffer(FRHIBuffer* Buffer, uint32 Value, uint32 Size, uint32 Offset) override;
        void CopyBuffer(FRHIBuffer* Source, uint64 SrcOffset, FRHIBuffer* Destination, uint64 DstOffset, uint64 CopySize) override;

        FTransientAlloc AllocateTransient(uint64 Size, uint32 Alignment = 16) override;

        void SetEnableUavBarriersForImage(FRHIImage* Image, bool bEnableBarriers) override;
        void SetEnableUavBarriersForBuffer(FRHIBuffer* Buffer, bool bEnableBarriers) override;

        void SetPermanentImageState(FRHIImage* Image, EResourceStates StateBits) override;
        void SetPermanentBufferState(FRHIBuffer* Buffer, EResourceStates StateBits) override;

        void BeginTrackingImageState(FRHIImage* Image, FTextureSubresourceSet Subresources, EResourceStates StateBits) override;
        void BeginTrackingBufferState(FRHIBuffer* Buffer, EResourceStates StateBits) override;

        void SetImageState(FRHIImage* Image, FTextureSubresourceSet Subresources, EResourceStates StateBits) override;
        void SetBufferState(FRHIBuffer* Buffer, EResourceStates StateBits) override;

        void SetResourceStatesForBindingSet(FRHIBindingSet* BindingSet) override;
        void SetResourceStateForRenderPass(const FRenderPassDesc& PassInfo) override;

        void EnableAutomaticBarriers() override;
        void DisableAutomaticBarriers() override;
        void CommitBarriers() override;

        EResourceStates GetImageSubresourceState(FRHIImage* Image, uint32 ArraySlice, uint32 MipLevel) override;
        EResourceStates GetBufferState(FRHIBuffer* Buffer) override;

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
        void SetPushConstants(const void* Data, size_t ByteSize) override;

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

        const FCommandListInfo& GetCommandListInfo() const override { return Inner->GetCommandListInfo(); }
        FPendingCommandState& GetPendingCommandState() override { return Inner->GetPendingCommandState(); }
        const FCommandListStatTracker& GetCommandListStats() const override { return Inner->GetCommandListStats(); }

    protected:

        void* GetAPIResourceImpl(EAPIResourceType InType) override { return Inner->GetAPI(InType); }

    private:

        FRHICommandListRef Inner;

        // Net open debug-label depth; an unclosed label at Close() leaks onto the queue's label
        // stack at submit (a silent FQueue creep). Reset on Open, asserted zero on Close.
        int32 MarkerDepth = 0;
    };
}
