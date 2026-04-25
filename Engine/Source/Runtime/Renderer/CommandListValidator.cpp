#include "pch.h"
#include "CommandListValidator.h"

#include "RenderResource.h"
#include "Core/Assertions/Assert.h"
#include "Log/Log.h"

namespace Lumina
{
    FCommandListValidator::FCommandListValidator(FRHICommandListRef InInner)
        : Inner(std::move(InInner))
    {
        ASSERT(Inner.IsValid());
    }

    void FCommandListValidator::Open()
    {
        Inner->Open();
    }

    void FCommandListValidator::Close()
    {
        Inner->Close();
    }

    void FCommandListValidator::Executed(FQueue* Queue, uint64 SubmissionID)
    {
        Inner->Executed(Queue, SubmissionID);
    }

    void FCommandListValidator::CopyImage(FRHIImage* Src, const FTextureSlice& SrcSlice, FRHIImage* Dst, const FTextureSlice& DstSlice)
    {
        ASSERT(Src != nullptr && Dst != nullptr);
        Inner->CopyImage(Src, SrcSlice, Dst, DstSlice);
    }

    void FCommandListValidator::CopyImage(FRHIImage* Src, const FTextureSlice& SrcSlice, FRHIStagingImage* Dst, const FTextureSlice& DstSlice)
    {
        ASSERT(Src != nullptr && Dst != nullptr);
        Inner->CopyImage(Src, SrcSlice, Dst, DstSlice);
    }

    void FCommandListValidator::CopyImage(FRHIStagingImage* Src, const FTextureSlice& SrcSlice, FRHIImage* Dst, const FTextureSlice& DstSlice)
    {
        ASSERT(Src != nullptr && Dst != nullptr);
        Inner->CopyImage(Src, SrcSlice, Dst, DstSlice);
    }

    void FCommandListValidator::WriteImage(FRHIImage* Dst, uint32 ArraySlice, uint32 MipLevel, const void* Data, uint32 RowPitch, uint32 DepthPitch)
    {
        ASSERT(Dst != nullptr && Data != nullptr);
        if (Dst->GetDescription().Extent.y > 1 && RowPitch == 0)
        {
            LOG_ERROR("WriteImage: RowPitch is 0 but dest has multiple rows");
        }
        Inner->WriteImage(Dst, ArraySlice, MipLevel, Data, RowPitch, DepthPitch);
    }

    void FCommandListValidator::ResolveImage(FRHIImage* Src, const FTextureSubresourceSet& SrcSubresources, FRHIImage* Dst, const FTextureSubresourceSet& DstSubresources)
    {
        ASSERT(Src != nullptr && Dst != nullptr);

        const FTextureSubresourceSet DestSR   = DstSubresources.Resolve(Dst->GetDescription(), false);
        const FTextureSubresourceSet SourceSR = SrcSubresources.Resolve(Src->GetDescription(), false);
        if (DestSR.NumArraySlices != SourceSR.NumArraySlices || DestSR.NumMipLevels != SourceSR.NumMipLevels)
        {
            LOG_ERROR("Mismatched subresources during image resolve!");
            return;
        }

        Inner->ResolveImage(Src, SrcSubresources, Dst, DstSubresources);
    }

    void FCommandListValidator::ClearImageFloat(FRHIImage* Image, FTextureSubresourceSet Subresource, const FColor& Color)
    {
        ASSERT(Image != nullptr);
        Inner->ClearImageFloat(Image, Subresource, Color);
    }

    void FCommandListValidator::ClearImageUInt(FRHIImage* Image, FTextureSubresourceSet Subresource, uint32 Color)
    {
        ASSERT(Image != nullptr);
        Inner->ClearImageUInt(Image, Subresource, Color);
    }

    void FCommandListValidator::WriteBuffer(FRHIBuffer* Buffer, const void* Data, size_t Size, size_t Offset)
    {
        ASSERT(Buffer != nullptr);
        if (Size == 0)
        {
            Inner->WriteBuffer(Buffer, Data, Size, Offset);
            return;
        }
        ASSERT(Data != nullptr);
        ASSERT(Size <= Buffer->GetSize());
        ASSERT(Offset + Size <= Buffer->GetSize());
        Inner->WriteBuffer(Buffer, Data, Size, Offset);
    }

    void FCommandListValidator::FillBuffer(FRHIBuffer* Buffer, uint32 Value)
    {
        ASSERT(Buffer != nullptr);
        Inner->FillBuffer(Buffer, Value);
    }

    void FCommandListValidator::CopyBuffer(FRHIBuffer* Source, uint64 SrcOffset, FRHIBuffer* Destination, uint64 DstOffset, uint64 CopySize)
    {
        ASSERT(Source != nullptr);
        ASSERT(Destination != nullptr);
        ASSERT(SrcOffset + CopySize <= Source->GetDescription().Size);
        ASSERT(DstOffset + CopySize <= Destination->GetDescription().Size);
        Inner->CopyBuffer(Source, SrcOffset, Destination, DstOffset, CopySize);
    }

    void FCommandListValidator::SetEnableUavBarriersForImage(FRHIImage* Image, bool bEnableBarriers)
    {
        ASSERT(Image != nullptr);
        Inner->SetEnableUavBarriersForImage(Image, bEnableBarriers);
    }

    void FCommandListValidator::SetEnableUavBarriersForBuffer(FRHIBuffer* Buffer, bool bEnableBarriers)
    {
        ASSERT(Buffer != nullptr);
        Inner->SetEnableUavBarriersForBuffer(Buffer, bEnableBarriers);
    }

    void FCommandListValidator::SetPermanentImageState(FRHIImage* Image, EResourceStates StateBits)
    {
        ASSERT(Image != nullptr);
        Inner->SetPermanentImageState(Image, StateBits);
    }

    void FCommandListValidator::SetPermanentBufferState(FRHIBuffer* Buffer, EResourceStates StateBits)
    {
        ASSERT(Buffer != nullptr);
        Inner->SetPermanentBufferState(Buffer, StateBits);
    }

    void FCommandListValidator::BeginTrackingImageState(FRHIImage* Image, FTextureSubresourceSet Subresources, EResourceStates StateBits)
    {
        ASSERT(Image != nullptr);
        Inner->BeginTrackingImageState(Image, Subresources, StateBits);
    }

    void FCommandListValidator::BeginTrackingBufferState(FRHIBuffer* Buffer, EResourceStates StateBits)
    {
        ASSERT(Buffer != nullptr);
        Inner->BeginTrackingBufferState(Buffer, StateBits);
    }

    void FCommandListValidator::SetImageState(FRHIImage* Image, FTextureSubresourceSet Subresources, EResourceStates StateBits)
    {
        ASSERT(Image != nullptr);
        Inner->SetImageState(Image, Subresources, StateBits);
    }

    void FCommandListValidator::SetBufferState(FRHIBuffer* Buffer, EResourceStates StateBits)
    {
        ASSERT(Buffer != nullptr);
        Inner->SetBufferState(Buffer, StateBits);
    }

    void FCommandListValidator::SetResourceStatesForBindingSet(FRHIBindingSet* BindingSet)
    {
        ASSERT(BindingSet != nullptr);
        Inner->SetResourceStatesForBindingSet(BindingSet);
    }

    void FCommandListValidator::SetResourceStateForRenderPass(const FRenderPassDesc& PassInfo)
    {
        Inner->SetResourceStateForRenderPass(PassInfo);
    }

    void FCommandListValidator::EnableAutomaticBarriers()
    {
        Inner->EnableAutomaticBarriers();
    }

    void FCommandListValidator::DisableAutomaticBarriers()
    {
        Inner->DisableAutomaticBarriers();
    }

    void FCommandListValidator::CommitBarriers()
    {
        Inner->CommitBarriers();
    }

    EResourceStates FCommandListValidator::GetImageSubresourceState(FRHIImage* Image, uint32 ArraySlice, uint32 MipLevel)
    {
        ASSERT(Image != nullptr);
        return Inner->GetImageSubresourceState(Image, ArraySlice, MipLevel);
    }

    EResourceStates FCommandListValidator::GetBufferState(FRHIBuffer* Buffer)
    {
        ASSERT(Buffer != nullptr);
        return Inner->GetBufferState(Buffer);
    }

    void FCommandListValidator::BeginTimerQuery(ITimerQuery* Query)
    {
        ASSERT(Query != nullptr);
        Inner->BeginTimerQuery(Query);
    }

    void FCommandListValidator::EndTimerQuery(ITimerQuery* Query)
    {
        ASSERT(Query != nullptr);
        Inner->EndTimerQuery(Query);
    }

    void FCommandListValidator::BeginPipelineStatsQuery(IPipelineStatsQuery* Query)
    {
        ASSERT(Query != nullptr);
        Inner->BeginPipelineStatsQuery(Query);
    }

    void FCommandListValidator::EndPipelineStatsQuery(IPipelineStatsQuery* Query)
    {
        ASSERT(Query != nullptr);
        Inner->EndPipelineStatsQuery(Query);
    }

    void FCommandListValidator::AddMarker(const char* Name, const FColor& Color)
    {
        ASSERT(Name != nullptr);
        Inner->AddMarker(Name, Color);
    }

    void FCommandListValidator::PopMarker()
    {
        Inner->PopMarker();
    }

    void FCommandListValidator::BeginRenderPass(const FRenderPassDesc& PassInfo)
    {
        Inner->BeginRenderPass(PassInfo);
    }

    void FCommandListValidator::EndRenderPass()
    {
        Inner->EndRenderPass();
    }

    void FCommandListValidator::ClearImageColor(FRHIImage* Image, const FColor& Color)
    {
        ASSERT(Image != nullptr);
        Inner->ClearImageColor(Image, Color);
    }

    void FCommandListValidator::SetPushConstants(const void* Data, size_t ByteSize)
    {
        ASSERT(Data != nullptr);
        ASSERT(ByteSize > 0);
        Inner->SetPushConstants(Data, ByteSize);
    }

    void FCommandListValidator::SetGraphicsState(const FGraphicsState& State)
    {
        ASSERT(State.Pipeline != nullptr);
        for (FRHIBindingSet* Set : State.Bindings)
        {
            ASSERT(Set != nullptr);
        }
        Inner->SetGraphicsState(State);
    }

    void FCommandListValidator::Draw(uint32 VertexCount, uint32 InstanceCount, uint32 FirstVertex, uint32 FirstInstance)
    {
        Inner->Draw(VertexCount, InstanceCount, FirstVertex, FirstInstance);
    }

    void FCommandListValidator::DrawIndexed(uint32 IndexCount, uint32 InstanceCount, uint32 FirstIndex, int32 VertexOffset, uint32 FirstInstance)
    {
        Inner->DrawIndexed(IndexCount, InstanceCount, FirstIndex, VertexOffset, FirstInstance);
    }

    void FCommandListValidator::DrawIndirect(uint32 DrawCount, uint64 Offset)
    {
        Inner->DrawIndirect(DrawCount, Offset);
    }

    void FCommandListValidator::DrawIndexedIndirect(uint32 DrawCount, uint64 Offset)
    {
        Inner->DrawIndexedIndirect(DrawCount, Offset);
    }

    void FCommandListValidator::SetComputeState(const FComputeState& State)
    {
        ASSERT(State.Pipeline != nullptr);
        for (FRHIBindingSet* Set : State.Bindings)
        {
            ASSERT(Set != nullptr);
        }
        Inner->SetComputeState(State);
    }

    void FCommandListValidator::Dispatch(uint32 GroupCountX, uint32 GroupCountY, uint32 GroupCountZ)
    {
        Inner->Dispatch(GroupCountX, GroupCountY, GroupCountZ);
    }
}
