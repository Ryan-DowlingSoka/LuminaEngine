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

        void CopyImage(FRHIImage* Src, const FTextureSlice& SrcSlice, FRHIImage* Dst, const FTextureSlice& DstSlice) override;
        void CopyImage(FRHIImage* Src, const FTextureSlice& SrcSlice, FRHIStagingImage* Dst, const FTextureSlice& DstSlice) override;
        void CopyImage(FRHIStagingImage* Src, const FTextureSlice& SrcSlice, FRHIImage* Dst, const FTextureSlice& DstSlice) override;
        void WriteImage(FRHIImage* Dst, uint32 ArraySlice, uint32 MipLevel, const void* Data, uint32 RowPitch, uint32 DepthPitch) override;
        void ResolveImage(FRHIImage* Src, const FTextureSubresourceSet& SrcSubresources, FRHIImage* Dst, const FTextureSubresourceSet& DstSubresources) override;
        void ClearImageFloat(FRHIImage* Image, FTextureSubresourceSet Subresource, const FColor& Color) override;
        void ClearImageUInt(FRHIImage* Image, FTextureSubresourceSet Subresource, uint32 Color) override;

        void WriteBuffer(FRHIBuffer* Buffer, const void* Data, size_t Size, size_t Offset = 0) override;
        void FillBuffer(FRHIBuffer* Buffer, uint32 Value) override;
        void CopyBuffer(FRHIBuffer* Source, uint64 SrcOffset, FRHIBuffer* Destination, uint64 DstOffset, uint64 CopySize) override;

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

        void BeginRenderPass(const FRenderPassDesc& PassInfo) override;
        void EndRenderPass() override;

        void ClearImageColor(FRHIImage* Image, const FColor& Color) override;
        void SetPushConstants(const void* Data, size_t ByteSize) override;

        void SetGraphicsState(const FGraphicsState& State) override;
        void Draw(uint32 VertexCount, uint32 InstanceCount, uint32 FirstVertex, uint32 FirstInstance) override;
        void DrawIndexed(uint32 IndexCount, uint32 InstanceCount, uint32 FirstIndex, int32 VertexOffset, uint32 FirstInstance) override;
        void DrawIndirect(uint32 DrawCount, uint64 Offset) override;
        void DrawIndexedIndirect(uint32 DrawCount, uint64 Offset) override;

        void SetComputeState(const FComputeState& State) override;
        void Dispatch(uint32 GroupCountX, uint32 GroupCountY, uint32 GroupCountZ) override;

        const FCommandListInfo& GetCommandListInfo() const override { return Inner->GetCommandListInfo(); }
        FPendingCommandState& GetPendingCommandState() override { return Inner->GetPendingCommandState(); }
        const FCommandListStatTracker& GetCommandListStats() const override { return Inner->GetCommandListStats(); }

    protected:

        void* GetAPIResourceImpl(EAPIResourceType InType) override { return Inner->GetAPI(InType); }

    private:

        FRHICommandListRef Inner;
    };
}
