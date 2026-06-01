#include "pch.h"
#include "VulkanCommandList.h"

#include "Convert.h"
#include "VulkanMacros.h"
#include "VulkanRenderContext.h"
#include "VulkanResources.h"
#include "Core/Profiler/Profile.h"
#include "Memory/Memcpy.h"
#include "Renderer/GPUProfiler/GPUProfiler.h"
#include "Renderer/RHIGlobals.h"
#include "TaskSystem/TaskSystem.h"

namespace Lumina
{
    
    namespace Limits
    {
        constexpr size_t vkCmdUpdateBufferLimit = 65536;
    }
    
    static VkResolveModeFlagBits PickColorResolveMode(EFormat Format)
    {
        const FFormatInfo& Info = RHI::Format::Info(Format);
        return Info.Kind == EFormatKind::Integer ? VK_RESOLVE_MODE_SAMPLE_ZERO_BIT : VK_RESOLVE_MODE_AVERAGE_BIT;
    }

    static EImageDimension GetDimensionForFramebuffer(EImageDimension dimension, bool isArray)
    {
        // Cubes/3D can't be render targets directly; treat as 2D arrays.
        if (dimension == EImageDimension::TextureCube || dimension == EImageDimension::TextureCubeArray || dimension == EImageDimension::Texture3D)
        {
            dimension = EImageDimension::Texture2DArray;
        }

        if (!isArray)
        {
            switch(dimension)  // NOLINT(clang-diagnostic-switch-enum)
            {
            case EImageDimension::Texture2DArray:
                dimension = EImageDimension::Texture2D;
                break;
            default:
                break;
            }
        }

        return dimension;
    }

    FVulkanCommandList::FVulkanCommandList(FVulkanRenderContext* RESTRICT InContext, const FCommandListInfo& RESTRICT InInfo)
        : RenderContext(InContext)
        , UploadManager(MakeUnique<FUploadManager>(InContext, InInfo.UploadChunkSize, 0, false))
        , ScratchManager(nullptr /* Unused for now */)
        , Info(InInfo)
        , PushConstantVisibility(0)
        , CurrentPipelineLayout(nullptr)
    {
        PendingState.AddPendingState(EPendingCommandState::AutomaticBarriers);
    }

    void FVulkanCommandList::KeepAlive(IRHIResource* Resource)
    {
        if (Resource != nullptr)
        {
            CurrentCommandBuffer->AddReferencedResource(Resource);
        }
    }

    void FVulkanCommandList::Open()
    {
        LUMINA_PROFILE_SCOPE();

        CurrentCommandBuffer = RenderContext->GetQueue(Info.CommandQueue)->GetOrCreateCommandBuffer();

        static constexpr VkCommandBufferBeginInfo BeginInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
        };

        VK_CHECK(vkBeginCommandBuffer(CurrentCommandBuffer->CommandBuffer, &BeginInfo));
        CurrentCommandBuffer->ReferencedResources.push_back(this);

        PendingState.AddPendingState(EPendingCommandState::Recording);
    }

    void FVulkanCommandList::Close()
    {
        LUMINA_PROFILE_SCOPE();
        
        EndRenderPass();
        
#if defined(TRACY_ENABLE)
        if (FQueue* CmdQueue = CurrentCommandBuffer->Queue; CmdQueue && CmdQueue->TracyContext != nullptr)
        {
            // The queue's context is shared across all command buffers, so serialize the collect
            // (it reads/resets the shared query pool) against concurrent recorders on this queue.
            std::scoped_lock Lock(CmdQueue->Mutex);
            LockMark(CmdQueue->Mutex);
            TracyVkCollect(CmdQueue->TracyContext, CurrentCommandBuffer->CommandBuffer)
        }
#endif

        StateTracker.KeepBufferInitialStates();
        StateTracker.KeepTextureInitialStates();
        CommitBarriers();
        
        VK_CHECK(vkEndCommandBuffer(CurrentCommandBuffer->CommandBuffer));
        
        PendingState.ClearPendingState(EPendingCommandState::Recording);
        PendingState.ClearPendingState(EPendingCommandState::DynamicBufferWrites);

        PushConstantVisibility      = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
        CurrentPipelineLayout       = VK_NULL_HANDLE;
        CurrentComputeState         = {};
        CurrentGraphicsState        = {};
        CommandListStatLastFrame    = CommandListStats;
        CommandListStats            = {};
        
        FlushDynamicBufferWrites();
    }

    void FVulkanCommandList::Executed(FQueue* Queue, uint64 SubmissionID)
    {
        LUMINA_PROFILE_SCOPE();

        CurrentCommandBuffer->SubmissionID = SubmissionID;
        uint64 RecordingID = CurrentCommandBuffer->RecordingID;
        
        CurrentCommandBuffer = nullptr;

        SubmitDynamicBuffers(RecordingID, SubmissionID);
        
        StateTracker.CommandListSubmitted();

        UploadManager->SubmitChunks(MakeVersion(RecordingID, Queue->Type, false), MakeVersion(SubmissionID, Queue->Type, true));
        //ScratchManager->SubmitChunks(MakeVersion(RecordingID, Queue->Type, false), MakeVersion(SubmissionID, Queue->Type, true));

        DynamicBufferWrites.clear();
    }

    void FVulkanCommandList::CopyImage(FRHIImage* RESTRICT Src, const FTextureSlice& RESTRICT SrcSlice, FRHIImage* RESTRICT Dst, const FTextureSlice& RESTRICT DstSlice)
    {
        LUMINA_PROFILE_SCOPE();

        CurrentCommandBuffer->AddReferencedResource(Src);
        CurrentCommandBuffer->AddReferencedResource(Dst);

        FTextureSlice ResolvedDstSlice = DstSlice.Resolve(Dst->GetDescription());
        FTextureSlice ResolvedSrcSlice = SrcSlice.Resolve(Src->GetDescription());

        if (PendingState.IsInState(EPendingCommandState::AutomaticBarriers))
        {
            RequireTextureState(Src, FTextureSubresourceSet(ResolvedSrcSlice.MipLevel, 1, ResolvedSrcSlice.ArraySlice, 1), EResourceStates::CopySource);
            RequireTextureState(Dst, FTextureSubresourceSet(ResolvedDstSlice.MipLevel, 1, ResolvedDstSlice.ArraySlice, 1), EResourceStates::CopyDest);
        }
        CommitBarriers();
        
        FVulkanImage* VulkanImageSrc = (FVulkanImage*)Src;
        FVulkanImage* VulkanImageDst = (FVulkanImage*)Dst;
        
        VkBlitImageInfo2 BlitInfo                   = {};
        BlitInfo.sType                              = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
        BlitInfo.srcImage                           = Src->GetAPI<VkImage>();
        BlitInfo.srcImageLayout                     = RenderContext->GetEffectiveImageLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        BlitInfo.dstImage                           = Dst->GetAPI<VkImage>();
        BlitInfo.dstImageLayout                     = RenderContext->GetEffectiveImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        BlitInfo.filter                             = VK_FILTER_LINEAR;

        VkImageBlit2 BlitRegion                     = {};
        BlitRegion.sType                            = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
    
        BlitRegion.srcSubresource.aspectMask        = VulkanImageSrc->GetFullAspectMask();
        BlitRegion.srcSubresource.mipLevel          = SrcSlice.MipLevel;
        BlitRegion.srcSubresource.baseArrayLayer    = SrcSlice.ArraySlice;
        BlitRegion.srcSubresource.layerCount        = 1;
        BlitRegion.srcOffsets[0]                    = { 0, 0, 0 };
        BlitRegion.srcOffsets[1]                    = { (int32)Src->GetSizeX(), (int32)Src->GetSizeY(), 1 };

        BlitRegion.dstSubresource.aspectMask        = VulkanImageDst->GetFullAspectMask();
        BlitRegion.dstSubresource.mipLevel          = DstSlice.MipLevel;
        BlitRegion.dstSubresource.baseArrayLayer    = DstSlice.ArraySlice;
        BlitRegion.dstSubresource.layerCount        = 1;
        BlitRegion.dstOffsets[0]                    = { 0, 0, 0 };
        BlitRegion.dstOffsets[1]                    = { (int32)Dst->GetSizeX(), (int32)Dst->GetSizeY(), 1 };

        BlitInfo.regionCount                        = 1;
        BlitInfo.pRegions                           = &BlitRegion;

        CommandListStats.NumBlitCommands++;
        vkCmdBlitImage2(CurrentCommandBuffer->CommandBuffer, &BlitInfo);
    }

    void FVulkanCommandList::CopyImage(FRHIImage* RESTRICT Src, const FTextureSlice& RESTRICT SrcSlice, FRHIStagingImage* RESTRICT Dst, const FTextureSlice& RESTRICT DstSlice)
    {
        FVulkanImage* Source = static_cast<FVulkanImage*>(Src);
        FVulkanStagingImage* Destination = static_cast<FVulkanStagingImage*>(Dst);

        const FRHIImageDesc& SrcDesc = Source->GetDescription();
        const FRHIImageDesc& DstDesc = Destination->GetDesc();

        const uint32 SrcMip   = SrcSlice.MipLevel;
        const uint32 SrcMipW  = (SrcDesc.Extent.x >> SrcMip) > 0 ? (SrcDesc.Extent.x >> SrcMip) : 1u;
        const uint32 SrcMipH  = (SrcDesc.Extent.y >> SrcMip) > 0 ? (SrcDesc.Extent.y >> SrcMip) : 1u;

        const uint32 ExtentW  = (SrcSlice.Width  == uint32(-1)) ? (SrcMipW - SrcSlice.X) : SrcSlice.Width;
        const uint32 ExtentH  = (SrcSlice.Height == uint32(-1)) ? (SrcMipH - SrcSlice.Y) : SrcSlice.Height;
        const uint32 ExtentD  = (SrcSlice.Depth  == uint32(-1)) ? 1u : SrcSlice.Depth;

        // Destination buffer rows are packed at the staging image's own width, so a
        // region-sized staging image yields a tightly-packed region-sized buffer.
        const uint32 DstMip   = DstSlice.MipLevel;
        const uint32 DstRowTexels = (DstDesc.Extent.x >> DstMip) > 0 ? (DstDesc.Extent.x >> DstMip) : 1u;
        const uint32 DstImageRows = (DstDesc.Extent.y >> DstMip) > 0 ? (DstDesc.Extent.y >> DstMip) : 1u;

        auto DstRegion = Destination->GetSliceRegion(DstMip, DstSlice.ArraySlice, DstSlice.Z);

        FTextureSubresourceSet SrcSubresource = FTextureSubresourceSet(SrcMip, 1, SrcSlice.ArraySlice, 1);

        VkBufferImageCopy ImageCopy     = {};
        ImageCopy.bufferOffset          = DstRegion.Offset;
        ImageCopy.bufferRowLength       = DstRowTexels;
        ImageCopy.bufferImageHeight     = DstImageRows;

        ImageCopy.imageSubresource.aspectMask       = GuessImageAspectFlags(ConvertFormat(SrcDesc.Format));
        ImageCopy.imageSubresource.mipLevel         = SrcMip;
        ImageCopy.imageSubresource.baseArrayLayer   = SrcSlice.ArraySlice;
        ImageCopy.imageSubresource.layerCount       = 1;

        ImageCopy.imageOffset.x = (int32)SrcSlice.X;
        ImageCopy.imageOffset.y = (int32)SrcSlice.Y;
        ImageCopy.imageOffset.z = (int32)SrcSlice.Z;

        ImageCopy.imageExtent.width  = ExtentW;
        ImageCopy.imageExtent.height = ExtentH;
        ImageCopy.imageExtent.depth  = ExtentD;

        if (PendingState.IsInState(EPendingCommandState::AutomaticBarriers))
        {
            RequireBufferState(Destination->Buffer, EResourceStates::CopyDest);
            RequireTextureState(Source, SrcSubresource, EResourceStates::CopySource);
        }
        CommitBarriers();
        
        CurrentCommandBuffer->AddReferencedResource(Source);
        CurrentCommandBuffer->AddReferencedResource(Destination);
        CurrentCommandBuffer->AddStagingResource(Destination->Buffer);

        vkCmdCopyImageToBuffer(CurrentCommandBuffer->CommandBuffer, Source->GetImage(), RenderContext->GetEffectiveImageLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL), Destination->Buffer->Buffer, 1, &ImageCopy);
        CommandListStats.NumCopies++;
    }

    void FVulkanCommandList::CopyImage(FRHIStagingImage* RESTRICT Src, const FTextureSlice& RESTRICT SrcSlice, FRHIImage* RESTRICT Dst, const FTextureSlice& RESTRICT DstSlice)
    {
        FVulkanStagingImage* Source = static_cast<FVulkanStagingImage*>(Src);
        FVulkanImage* Destination = static_cast<FVulkanImage*>(Dst);

        FTextureSlice ResolvedDstSlice = DstSlice.Resolve(Destination->GetDescription());
        FTextureSlice ResolvedSrcSlice = SrcSlice.Resolve(Source->GetDesc());

        auto SrcRegion = Source->GetSliceRegion(ResolvedSrcSlice.MipLevel, ResolvedSrcSlice.ArraySlice, ResolvedSrcSlice.Z);

        FTextureSubresourceSet DstSubresource = FTextureSubresourceSet(ResolvedDstSlice.MipLevel, 1, ResolvedDstSlice.ArraySlice, 1);


        VkBufferImageCopy ImageCopy     = {};
        ImageCopy.bufferOffset          = SrcRegion.Offset;
        ImageCopy.bufferRowLength       = ResolvedSrcSlice.X;
        ImageCopy.bufferImageHeight     = ResolvedSrcSlice.Y;

        ImageCopy.imageSubresource.aspectMask       = GuessImageAspectFlags(ConvertFormat(Destination->GetDescription().Format));
        ImageCopy.imageSubresource.mipLevel         = ResolvedDstSlice.MipLevel;
        ImageCopy.imageSubresource.baseArrayLayer   = ResolvedDstSlice.ArraySlice;
        ImageCopy.imageSubresource.layerCount       = 1;

        // @TODO 0 for now, will need to comeback and revisit to add offset ability.
        ImageCopy.imageOffset.x = 0;//(int32)ResolvedDstSlice.X;
        ImageCopy.imageOffset.y = 0;//(int32)ResolvedDstSlice.Y;
        ImageCopy.imageOffset.z = 0;//(int32)ResolvedDstSlice.Z;

        ImageCopy.imageExtent.width  = ResolvedDstSlice.X;
        ImageCopy.imageExtent.height = ResolvedDstSlice.Y;
        ImageCopy.imageExtent.depth  = ResolvedDstSlice.Z;
        
        if (PendingState.IsInState(EPendingCommandState::AutomaticBarriers))
        {
            RequireBufferState(Source->Buffer, EResourceStates::CopySource);
            RequireTextureState(Destination, DstSubresource, EResourceStates::CopyDest);
        }
        CommitBarriers();
        
        CurrentCommandBuffer->AddReferencedResource(Source);
        CurrentCommandBuffer->AddReferencedResource(Destination);
        CurrentCommandBuffer->AddStagingResource(Source->Buffer);

        vkCmdCopyBufferToImage(CurrentCommandBuffer->CommandBuffer, Source->Buffer->Buffer, Destination->GetImage(), RenderContext->GetEffectiveImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL), 1, &ImageCopy);
        CommandListStats.NumCopies++;
    }

    static void ComputeMipLevelInformation(const FRHIImageDesc& RESTRICT Desc, uint32 MipLevel, uint32* RESTRICT WidthOut, uint32* RESTRICT HeightOut, uint32* RESTRICT DepthOut)
    {
        uint32 Width    = std::max((uint32)Desc.Extent.x >> MipLevel, uint32(1));
        uint32 Height   = std::max((uint32)Desc.Extent.y >> MipLevel, uint32(1));
        uint32 Depth    = std::max((uint32)Desc.Depth >> MipLevel, uint32(1));

        if (WidthOut)
        {
            *WidthOut = Width;
        }
        if (HeightOut)
        {
            *HeightOut = Height;
        }
        if (DepthOut)
        {
            *DepthOut = Depth;
        }
    }

    void FVulkanCommandList::WriteImage(FRHIImage* RESTRICT Dst, uint32 ArraySlice, uint32 MipLevel, const void* RESTRICT Data, uint32 RowPitch, uint32 DepthPitch)
    {
        LUMINA_PROFILE_SCOPE();

        uint32 MipWidth, MipHeight, MipDepth;
        ComputeMipLevelInformation(Dst->GetDescription(), MipLevel, &MipWidth, &MipHeight, &MipDepth);

        const FFormatInfo& FormatInfo = RHI::Format::Info(Dst->GetDescription().Format);
        uint32 DeviceNumCols = (MipWidth + FormatInfo.BlockSize - 1) / FormatInfo.BlockSize;
        uint32 DeviceNumRows = (MipHeight + FormatInfo.BlockSize - 1) / FormatInfo.BlockSize;
        uint32 DeviceRowPitch = DeviceNumCols * FormatInfo.BytesPerBlock;
        uint64 DeviceMemSize = uint64(DeviceRowPitch) * uint64(DeviceNumRows) * MipDepth;

        FRHIBuffer* UploadBuffer;
        uint64 UploadOffset;
        void* UploadCPUVA;
        if (!UploadManager->SuballocateBuffer(DeviceMemSize, UploadBuffer, UploadOffset, UploadCPUVA, MakeVersion(CurrentCommandBuffer->RecordingID, Info.CommandQueue, false)))
        {
            LOG_ERROR("Failed to suballocate buffer for size: %llu", DeviceMemSize);
            return;
        }

        uint32 MinRowPitch      = std::min(DeviceRowPitch, RowPitch);
        uint8* MappedPtrBase    = static_cast<uint8*>(UploadCPUVA);
        const uint8* SourceBase = static_cast<const uint8*>(Data);

        for (uint32 Slice = 0; Slice < MipDepth; ++Slice)
        {
            const uint8* SourcePtr  = SourceBase + static_cast<size_t>(Slice) * static_cast<size_t>(DepthPitch);
            uint8* MappedPtr        = MappedPtrBase + Slice * static_cast<uint64>(DeviceNumRows) * DeviceRowPitch;
            for (uint32 row = 0; row < DeviceNumRows; ++row)
            {
                Memory::Memcpy(MappedPtr, SourcePtr, MinRowPitch);
                MappedPtr += DeviceRowPitch;
                SourcePtr += RowPitch;
            }
        }
        
        FVulkanImage* VulkanImage = (FVulkanImage*)Dst;
        
        VkBufferImageCopy CopyRegion = {};
        CopyRegion.bufferOffset                     = UploadOffset;
        CopyRegion.bufferRowLength                  = DeviceNumCols * FormatInfo.BlockSize;
        CopyRegion.bufferImageHeight                = DeviceNumRows * FormatInfo.BlockSize;
        CopyRegion.imageSubresource.aspectMask      = VulkanImage->GetFullAspectMask();
        CopyRegion.imageSubresource.mipLevel        = MipLevel;
        CopyRegion.imageSubresource.baseArrayLayer  = ArraySlice;
        CopyRegion.imageSubresource.layerCount      = 1;
        CopyRegion.imageOffset                      = { 0, 0, 0 };
        CopyRegion.imageExtent                      = { MipWidth, MipHeight, MipDepth };

        if (PendingState.IsInState(EPendingCommandState::AutomaticBarriers))
        {
            SetImageState(Dst, FTextureSubresourceSet(MipLevel, 1, ArraySlice, 1), EResourceStates::CopyDest);
        }
        CommitBarriers();

        CurrentCommandBuffer->AddReferencedResource(Dst);
        CurrentCommandBuffer->AddStagingResource(UploadBuffer);

        CommandListStats.NumCopies++;
        vkCmdCopyBufferToImage(
            CurrentCommandBuffer->CommandBuffer,
            UploadBuffer->GetAPI<VkBuffer>(),
            Dst->GetAPI<VkImage, EAPIResourceType::Image>(),
            RenderContext->GetEffectiveImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
            1, &CopyRegion
        );

    }

    void FVulkanCommandList::WriteImageRegion(FRHIImage* RESTRICT Dst, uint32 ArraySlice, uint32 MipLevel, uint32 OffsetX, uint32 OffsetY, uint32 Width, uint32 Height, const void* RESTRICT Data, uint32 RowPitch)
    {
        LUMINA_PROFILE_SCOPE();

        if (Width == 0 || Height == 0)
        {
            return;
        }

        // Block-compressed formats would need block-aligned offsets; the terrain maps
        // that use this path are all single-texel-block (R32F / R8), so assert linearity.
        const FFormatInfo& FormatInfo = RHI::Format::Info(Dst->GetDescription().Format);
        ASSERT(FormatInfo.BlockSize == 1);

        const uint32 RegionRowPitch = Width * FormatInfo.BytesPerBlock;
        const uint64 DeviceMemSize  = uint64(RegionRowPitch) * uint64(Height);

        FRHIBuffer* UploadBuffer;
        uint64 UploadOffset;
        void* UploadCPUVA;
        if (!UploadManager->SuballocateBuffer(DeviceMemSize, UploadBuffer, UploadOffset, UploadCPUVA, MakeVersion(CurrentCommandBuffer->RecordingID, Info.CommandQueue, false)))
        {
            LOG_ERROR("Failed to suballocate buffer for size: %llu", DeviceMemSize);
            return;
        }

        // Pack the region tightly into the staging buffer, pulling each row out of the
        // (possibly wider) source via RowPitch.
        const uint32 CopyRowBytes = std::min(RegionRowPitch, RowPitch ? RowPitch : RegionRowPitch);
        uint8*       MappedPtr    = static_cast<uint8*>(UploadCPUVA);
        const uint8* SourcePtr    = static_cast<const uint8*>(Data);
        for (uint32 Row = 0; Row < Height; ++Row)
        {
            Memory::Memcpy(MappedPtr, SourcePtr, CopyRowBytes);
            MappedPtr += RegionRowPitch;
            SourcePtr += RowPitch;
        }

        FVulkanImage* VulkanImage = (FVulkanImage*)Dst;

        VkBufferImageCopy CopyRegion = {};
        CopyRegion.bufferOffset                     = UploadOffset;
        CopyRegion.bufferRowLength                  = Width;
        CopyRegion.bufferImageHeight                = Height;
        CopyRegion.imageSubresource.aspectMask      = VulkanImage->GetFullAspectMask();
        CopyRegion.imageSubresource.mipLevel        = MipLevel;
        CopyRegion.imageSubresource.baseArrayLayer  = ArraySlice;
        CopyRegion.imageSubresource.layerCount      = 1;
        CopyRegion.imageOffset                      = { (int32)OffsetX, (int32)OffsetY, 0 };
        CopyRegion.imageExtent                      = { Width, Height, 1 };

        if (PendingState.IsInState(EPendingCommandState::AutomaticBarriers))
        {
            SetImageState(Dst, FTextureSubresourceSet(MipLevel, 1, ArraySlice, 1), EResourceStates::CopyDest);
        }
        CommitBarriers();

        CurrentCommandBuffer->AddReferencedResource(Dst);
        CurrentCommandBuffer->AddStagingResource(UploadBuffer);

        CommandListStats.NumCopies++;
        vkCmdCopyBufferToImage(
            CurrentCommandBuffer->CommandBuffer,
            UploadBuffer->GetAPI<VkBuffer>(),
            Dst->GetAPI<VkImage, EAPIResourceType::Image>(),
            RenderContext->GetEffectiveImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
            1, &CopyRegion
        );
    }

    void FVulkanCommandList::ResolveImage(FRHIImage* RESTRICT Src, const FTextureSubresourceSet& RESTRICT SrcSubresources, FRHIImage* RESTRICT Dst, const FTextureSubresourceSet& RESTRICT DstSubresources)
    {
        LUMINA_PROFILE_SCOPE();
        
        EndRenderPass();
        
        FVulkanImage* Source = static_cast<FVulkanImage*>(Src);
        FVulkanImage* Destination = static_cast<FVulkanImage*>(Dst);

        FTextureSubresourceSet DestSR = DstSubresources.Resolve(Destination->GetDescription(), false);
        FTextureSubresourceSet SourceSR = SrcSubresources.Resolve(Source->GetDescription(), false);

        TFixedVector<VkImageResolve, 4> Regions;

        for (uint16 Mip = 0; Mip < static_cast<uint16>(DestSR.NumMipLevels); ++Mip)
        {
            VkImageSubresourceLayers DestLayers = {};
            DestLayers.aspectMask       = VK_IMAGE_ASPECT_COLOR_BIT;
            DestLayers.mipLevel         = Mip + DestSR.BaseMipLevel;
            DestLayers.baseArrayLayer   = DestSR.BaseArraySlice;
            DestLayers.layerCount       = DestSR.NumArraySlices;

            VkImageSubresourceLayers SourceLayers = {};
            SourceLayers.aspectMask       = VK_IMAGE_ASPECT_COLOR_BIT;
            SourceLayers.mipLevel         = Mip + SourceSR.BaseMipLevel;
            SourceLayers.baseArrayLayer   = SourceSR.BaseArraySlice;
            SourceLayers.layerCount       = SourceSR.NumArraySlices;

            VkImageResolve Resolve = {};
            Resolve.srcSubresource = SourceLayers;
            Resolve.dstSubresource = DestLayers;
            Resolve.extent.width = std::max(Destination->GetExtent().x >> DestLayers.mipLevel, 1u);
            Resolve.extent.height = std::max(Destination->GetExtent().y >> DestLayers.mipLevel, 1u);
            Resolve.extent.depth = std::max<uint32>(Destination->GetDescription().Depth >> DestLayers.mipLevel, 1u);

            Regions.push_back(Resolve);
        }
            
        if (PendingState.IsInState(EPendingCommandState::AutomaticBarriers))
        {
            RequireTextureState(Src, SourceSR, EResourceStates::ResolveSource);
            RequireTextureState(Dst, DestSR, EResourceStates::ResolveDest);
        }

        CommitBarriers();

        vkCmdResolveImage(CurrentCommandBuffer->CommandBuffer, Source->GetImage(), RenderContext->GetEffectiveImageLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL), Destination->GetImage(), RenderContext->GetEffectiveImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL), Regions.size(), Regions.data());
    }

    void FVulkanCommandList::ClearImageFloat(FRHIImage* RESTRICT Image, FTextureSubresourceSet Subresource, const FColor& RESTRICT Color)
    {
        EndRenderPass();
        
        FVulkanImage* VulkanImage = static_cast<FVulkanImage*>(Image);

        Subresource = Subresource.Resolve(VulkanImage->GetDescription(), false);

        if (PendingState.IsInState(EPendingCommandState::AutomaticBarriers))
        {
            RequireTextureState(Image, Subresource, EResourceStates::CopyDest);
        }
        CommitBarriers();
        
        
        VkImageSubresourceRange SubresourceRange    = {};
        SubresourceRange.aspectMask                 = VK_IMAGE_ASPECT_COLOR_BIT;
        SubresourceRange.baseArrayLayer             = Subresource.BaseArraySlice;
        SubresourceRange.layerCount                 = Subresource.NumArraySlices;
        SubresourceRange.baseMipLevel               = Subresource.BaseMipLevel;
        SubresourceRange.levelCount                 = Subresource.NumMipLevels;

        VkClearColorValue Value = {};
        Value.float32[0] = Color.R;
        Value.float32[1] = Color.G;
        Value.float32[2] = Color.B;
        Value.float32[3] = Color.A;

        CurrentCommandBuffer->AddReferencedResource(Image);

        CommandListStats.NumClearCommands++;
        vkCmdClearColorImage(CurrentCommandBuffer->CommandBuffer, VulkanImage->GetImage(), RenderContext->GetEffectiveImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL), &Value, 1, &SubresourceRange);

    }

    void FVulkanCommandList::ClearImageUInt(FRHIImage* RESTRICT Image, FTextureSubresourceSet Subresource, uint32 Color)
    {
        EndRenderPass();

        FVulkanImage* VulkanImage = static_cast<FVulkanImage*>(Image);
        CurrentCommandBuffer->AddReferencedResource(Image);
        
        Subresource = Subresource.Resolve(VulkanImage->GetDescription(), false);

        if (PendingState.IsInState(EPendingCommandState::AutomaticBarriers))
        {
            RequireTextureState(Image, Subresource, EResourceStates::CopyDest);
        }
        CommitBarriers();
        
        VkImageSubresourceRange SubresourceRange    = {};
        SubresourceRange.aspectMask                 = VK_IMAGE_ASPECT_COLOR_BIT;
        SubresourceRange.baseArrayLayer             = Subresource.BaseArraySlice;
        SubresourceRange.layerCount                 = Subresource.NumArraySlices;
        SubresourceRange.baseMipLevel               = Subresource.BaseMipLevel;
        SubresourceRange.levelCount                 = Subresource.NumMipLevels;

        if (Image->GetFlags().IsFlagSet(EImageCreateFlags::DepthStencil) || Image->GetFlags().IsFlagSet(EImageCreateFlags::DepthAttachment))
        {
            SubresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

            VkClearDepthStencilValue DepthValue = {};
            DepthValue.depth    = (float)Color;
            DepthValue.stencil  = 0;

            vkCmdClearDepthStencilImage(CurrentCommandBuffer->CommandBuffer, VulkanImage->GetImage(), RenderContext->GetEffectiveImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL), &DepthValue, 1, &SubresourceRange);

            return;
        }
        

        VkClearColorValue Value = {};
        Value.uint32[0] = Color;
        Value.uint32[1] = Color;
        Value.uint32[2] = Color;
        Value.uint32[3] = Color;
        Value.int32[0] = (int32)Color;
        Value.int32[1] = (int32)Color;
        Value.int32[2] = (int32)Color;
        Value.int32[3] = (int32)Color;
        
        CommandListStats.NumClearCommands++;
        vkCmdClearColorImage(CurrentCommandBuffer->CommandBuffer, VulkanImage->GetImage(), RenderContext->GetEffectiveImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL), &Value, 1, &SubresourceRange);
    }

    void FVulkanCommandList::CopyBuffer(FRHIBuffer* RESTRICT Source, uint64 SrcOffset, FRHIBuffer* RESTRICT Destination, uint64 DstOffset, uint64 CopySize)
    {
        LUMINA_PROFILE_SCOPE();

        bool bStagingDestination    = Destination->IsStagingBuffer();
        bool bStagingSource         = Source->IsStagingBuffer();

        if (bStagingDestination)
        {
            CurrentCommandBuffer->AddStagingResource(Destination);
        }
        else
        {
            CurrentCommandBuffer->AddReferencedResource(Destination);
        }

        if (bStagingSource)
        {
            CurrentCommandBuffer->AddStagingResource(Source);
        }
        else
        {
            CurrentCommandBuffer->AddReferencedResource(Source);
        }

        if (PendingState.IsInState(EPendingCommandState::AutomaticBarriers))
        {
            RequireBufferState(Source, EResourceStates::CopySource);
            RequireBufferState(Destination, EResourceStates::CopyDest);
        }
        CommitBarriers();
        
        VkBufferCopy CopyRegion = {};
        CopyRegion.size         = CopySize;
        CopyRegion.srcOffset    = SrcOffset;
        CopyRegion.dstOffset    = DstOffset;

        FVulkanBuffer* VkSource = static_cast<FVulkanBuffer*>(Source);
        FVulkanBuffer* VkDestination = static_cast<FVulkanBuffer*>(Destination);

        CommandListStats.NumCopies++;
        vkCmdCopyBuffer(CurrentCommandBuffer->CommandBuffer, VkSource->GetBuffer(), VkDestination->GetBuffer(), 1, &CopyRegion);
    }

    void FVulkanCommandList::WriteDynamicBuffer(FRHIBuffer* RESTRICT Buffer, const void* RESTRICT Data, size_t Size)
    {
        LUMINA_PROFILE_SCOPE();

        FVulkanBuffer* VulkanBuffer = static_cast<FVulkanBuffer*>(Buffer);

        auto GetQueueFinishID = [&] (ECommandQueue Queue)-> uint64
        {
            return RenderContext->GetQueue(Queue)->LastFinishedID;
        };

        FDynamicBufferWrite& Write = DynamicBufferWrites[Buffer];

        if (!Write.bInitialized)
        {
            Write.MinVersion = Buffer->GetDescription().MaxVersions;
            Write.MaxVersion = -1;
            Write.bInitialized = true;
        }

        TArray<uint64, static_cast<uint32>(ECommandQueue::Num)> QueueCompletionValues =
        {
            GetQueueFinishID(ECommandQueue::Graphics),
            GetQueueFinishID(ECommandQueue::Compute),
            GetQueueFinishID(ECommandQueue::Transfer),
        };
        
        uint32 SearchStart = VulkanBuffer->VersionSearchStart;
        uint32 MaxVersions = Buffer->GetDescription().MaxVersions;
        uint32 Version = 0;
        
        uint64 OriginalVersionInfo = 0;

        while (true)
        {
            bool bFound = false;
            
            for (SIZE_T i = 0; i < MaxVersions; ++i)
            {
            
                Version = i + SearchStart;
                Version = (Version >= MaxVersions) ? (Version - MaxVersions) : Version;

                OriginalVersionInfo = VulkanBuffer->VersionTracking[Version];
                
                if (OriginalVersionInfo == 0)
                {
                    bFound = true;
                    break;
                }

                bool bSubmitted = (OriginalVersionInfo & GVersionSubmittedFlag) != 0;
                uint32 QueueIndex = static_cast<uint32>(OriginalVersionInfo >> GVersionQueueShift) & GVersionQueueMask;
                uint64 ID = OriginalVersionInfo & GVersionIDMask;

                if (bSubmitted)
                {
                    if (QueueIndex >= static_cast<uint32>(ECommandQueue::Num))
                    {
                        bFound = true;
                        break;
                    }

                    if (ID <= QueueCompletionValues[QueueIndex])
                    {
                        bFound = true;
                        break;
                    }
                }
            }

            if (!bFound)
            {
                LOG_ERROR("Dynamic Buffer [] has MaxVersions: {} - Which is insufficient", Buffer->GetDescription().MaxVersions);
                return;
            }

            uint64 NewVersionInfo = (static_cast<uint64>(Info.CommandQueue) << GVersionQueueShift) | (CurrentCommandBuffer->RecordingID);

            if (VulkanBuffer->VersionTracking[Version].compare_exchange_strong(OriginalVersionInfo, NewVersionInfo))
            {
                break;
            }
        }

        VulkanBuffer->VersionSearchStart = (Version + 1 < MaxVersions) ? (Version + 1) : 0;

        Write.LatestVersion = Version;
        Write.MinVersion = Math::Min<int64>(Version, Write.MinVersion);
        Write.MaxVersion = Math::Max<int64>(Version, Write.MaxVersion);

        void* HostData = (uint8*)VulkanBuffer->GetMappedMemory() + (Version * VulkanBuffer->GetDescription().Size);
        
        Memory::Memcpy(HostData, Data, Size);

        PendingState.AddPendingState(EPendingCommandState::DynamicBufferWrites);
    }
    
    void FVulkanCommandList::WriteBuffer(FRHIBuffer* RESTRICT Buffer, const void* RESTRICT Data, SIZE_T Size, SIZE_T Offset)
    {
        LUMINA_PROFILE_SCOPE();
        
        if (Size == 0)
        {
            // Size 0 is silent no-op for caller convenience.
            return;
        }

        CommandListStats.NumBufferWrites++;
        
        CurrentCommandBuffer->AddReferencedResource(Buffer);
        
        if (Buffer->GetDescription().Usage.IsFlagSet(BUF_Dynamic))
        {
            WriteDynamicBuffer(Buffer, Data, Size);
            
            return;
        }
        
        
        // vkCmdUpdateBuffer: <= 64kb, offset/size must be multiples of 4 (size rounded up below).
        if (Size <= Limits::vkCmdUpdateBufferLimit && (Offset & 3) == 0)
        {
            if (PendingState.IsInState(EPendingCommandState::AutomaticBarriers))
            {
                SetBufferState(Buffer, EResourceStates::CopyDest);
            }
            CommitBarriers();
            
            const SIZE_T SizeToWrite = (Size + 3) & ~3ull;
            
            LUMINA_PROFILE_SECTION("vkCmdUpdateBuffer");
            vkCmdUpdateBuffer(CurrentCommandBuffer->CommandBuffer, Buffer->GetAPI<VkBuffer>(), Offset, SizeToWrite, Data);
        }
        else
        {
            LUMINA_PROFILE_SECTION("VkCopyBuffer");

            FRHIBuffer* UploadBuffer;
            uint64 UploadOffset;
            void* UploadCPUVA;
            if (UploadManager->SuballocateBuffer(Size, UploadBuffer, UploadOffset, UploadCPUVA, MakeVersion(CurrentCommandBuffer->RecordingID, Info.CommandQueue, false)))
            {
                Memory::Memcpy(UploadCPUVA, Data, Size);
                CopyBuffer(UploadBuffer, UploadOffset, Buffer, Offset, Size);
            }
            else
            {
                PANIC("WriteBuffer: upload suballoc failed for buffer '{}', size {} bytes, offset {}",
                      Buffer->GetDescription().DebugName, Size, Offset);
            }
        }
    }

    FTransientAlloc FVulkanCommandList::AllocateTransient(uint64 Size, uint32 Alignment)
    {
        LUMINA_PROFILE_SCOPE();

        FTransientAlloc Result;
        if (Size == 0)
        {
            return Result;
        }

        FRHIBuffer* RingBuffer = nullptr;
        uint64      RingOffset = 0;
        void*       CpuVA      = nullptr;
        const uint64 Version   = MakeVersion(CurrentCommandBuffer->RecordingID, Info.CommandQueue, false);

        if (!UploadManager->SuballocateBuffer(Size, RingBuffer, RingOffset, CpuVA, Version, Alignment))
        {
            LOG_ERROR("Failed to suballocate %llu bytes from transient ring", Size);
            return Result;
        }
        
        CurrentCommandBuffer->AddReferencedResource(RingBuffer);

        CommandListStats.NumTransientAllocs++;

        Result.Cpu    = CpuVA;
        Result.Buffer = RingBuffer;
        Result.Offset = RingOffset;
        Result.Size   = Size;
        Result.Gpu    = RingBuffer->GetAddress() + RingOffset;
        return Result;
    }

    void FVulkanCommandList::FillBuffer(FRHIBuffer* Buffer, uint32 Value)
    {
        FVulkanBuffer* VulkanBuffer = static_cast<FVulkanBuffer*>(Buffer);
        EndRenderPass();

        if (PendingState.IsInState(EPendingCommandState::AutomaticBarriers))
        {
            RequireBufferState(VulkanBuffer, EResourceStates::CopyDest);
        }
        CommitBarriers();

        
        vkCmdFillBuffer(CurrentCommandBuffer->CommandBuffer, VulkanBuffer->Buffer, 0, VulkanBuffer->GetDescription().Size, Value);
        CurrentCommandBuffer->AddReferencedResource(Buffer);
    }

    void FVulkanCommandList::UpdateComputeDynamicBuffers()
    {
        if (PendingState.IsInState(EPendingCommandState::DynamicBufferWrites) && CurrentComputeState.Pipeline)
        {
            FVulkanComputePipeline* Pipeline = static_cast<FVulkanComputePipeline*>(CurrentComputeState.Pipeline);
            BindBindingSets(VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline->PipelineLayout, CurrentComputeState.Bindings);

            PendingState.ClearPendingState(EPendingCommandState::DynamicBufferWrites);
        }
    }

    void FVulkanCommandList::UpdateGraphicsDynamicBuffers()
    {
        if (PendingState.IsInState(EPendingCommandState::DynamicBufferWrites) && CurrentGraphicsState.Pipeline)
        {
            FVulkanGraphicsPipeline* PSO = static_cast<FVulkanGraphicsPipeline*>(CurrentGraphicsState.Pipeline);

            BindBindingSets(VK_PIPELINE_BIND_POINT_GRAPHICS, PSO->PipelineLayout, CurrentGraphicsState.Bindings);

            PendingState.ClearPendingState(EPendingCommandState::DynamicBufferWrites);
        }
    }

    void FVulkanCommandList::SetEnableUavBarriersForImage(FRHIImage* Image, bool bEnableBarriers)
    {
        FVulkanImage* VulkanImage = static_cast<FVulkanImage*>(Image);
        
        StateTracker.SetEnableUavBarriersForTexture(VulkanImage, bEnableBarriers);
    }

    void FVulkanCommandList::SetEnableUavBarriersForBuffer(FRHIBuffer* Buffer, bool bEnableBarriers)
    {
        FVulkanBuffer* VulkanBuffer = static_cast<FVulkanBuffer*>(Buffer);
        
        StateTracker.SetEnableUavBarriersForBuffer(VulkanBuffer, bEnableBarriers);
    }

    void FVulkanCommandList::FlushDynamicBufferWrites()
    {
        LUMINA_PROFILE_SCOPE();
        
        TFixedVector<VmaAllocation, 4> Allocations;
        TFixedVector<VkDeviceSize, 4> Offsets;
        TFixedVector<VkDeviceSize, 4> Sizes;
    
        for (auto& Pair : DynamicBufferWrites)
        {
            FVulkanBuffer* Buffer = Pair.first.As<FVulkanBuffer>();
            FDynamicBufferWrite& Write = Pair.second;

            if (Write.MaxVersion < Write.MinVersion || !Write.bInitialized)
            {
                continue;
            }

            uint64 NumVersions = Write.MaxVersion - Write.MinVersion + 1;
            VkDeviceSize Offset = Write.MinVersion * Buffer->GetDescription().Size;
            VkDeviceSize Size = NumVersions * Buffer->GetDescription().Size;

            VmaAllocation Allocation = Buffer->Allocation;
            if (!Allocation)
            {
                LOG_WARN("Attempted to flush a dynamic buffer with no valid VmaAllocation.");
                continue;
            }

            Allocations.push_back(Allocation);
            Offsets.push_back(Offset);
            Sizes.push_back(Size);
        }

        if (!Allocations.empty())
        {
            VK_CHECK(vmaFlushAllocations(RenderContext->GetDevice()->GetAllocator().GetVMA(),
                uint32(Allocations.size()),
                Allocations.data(),
                Offsets.data(),
                Sizes.data()
            ));
        }
    }

    void FVulkanCommandList::SubmitDynamicBuffers(uint64 RecordingID, uint64 SubmittedID)
    {
        LUMINA_PROFILE_SCOPE();

        uint64 StateToFind = (uint64(Info.CommandQueue) << GVersionQueueShift) | (RecordingID & GVersionIDMask);
        uint64 StateToReplace = (uint64(Info.CommandQueue) << GVersionQueueShift) | (SubmittedID & GVersionIDMask) | GVersionSubmittedFlag;

        for (auto& Pair : DynamicBufferWrites)
        {
            FRHIBufferRef Buffer = Pair.first;
            FDynamicBufferWrite& Write = Pair.second;

            if (!Write.bInitialized)
            {
                continue;
            }

            for (int64 i = Write.MinVersion; i <= Write.MaxVersion; ++i)
            {
                uint64 Expected = StateToFind;
                Buffer.As<FVulkanBuffer>()->VersionTracking[i].compare_exchange_strong(Expected, StateToReplace);
            }
        }
    }

    void FVulkanCommandList::SetPermanentImageState(FRHIImage* Image, EResourceStates StateBits)
    {
        FVulkanImage* VulkanImage = static_cast<FVulkanImage*>(Image);

        StateTracker.SetPermanentTextureState(VulkanImage, AllSubresources, StateBits);

        if (CurrentCommandBuffer)
        {
            CurrentCommandBuffer->ReferencedResources.push_back(Image);
        }
    }

    void FVulkanCommandList::SetPermanentBufferState(FRHIBuffer* Buffer, EResourceStates StateBits)
    {
        FVulkanBuffer* VulkanBuffer = static_cast<FVulkanBuffer*>(Buffer);

        StateTracker.SetPermanentBufferState(VulkanBuffer, StateBits);
        
        if (CurrentCommandBuffer)
        {
            CurrentCommandBuffer->ReferencedResources.push_back(Buffer);
        }
    }

    void FVulkanCommandList::BeginTrackingImageState(FRHIImage* Image, FTextureSubresourceSet Subresources, EResourceStates StateBits)
    {
        FVulkanImage* VulkanImage = static_cast<FVulkanImage*>(Image);

        StateTracker.BeginTrackingTextureState(VulkanImage, Subresources, StateBits);
    }

    void FVulkanCommandList::BeginTrackingBufferState(FRHIBuffer* Buffer, EResourceStates StateBits)
    {
        FVulkanBuffer* VulkanBuffer = static_cast<FVulkanBuffer*>(Buffer);

        StateTracker.BeginTrackingBufferState(VulkanBuffer, StateBits);
    }

    void FVulkanCommandList::SetImageState(FRHIImage* Image, FTextureSubresourceSet Subresources, EResourceStates StateBits)
    {
        FVulkanImage* VulkanImage = static_cast<FVulkanImage*>(Image);
        StateTracker.RequireTextureState(VulkanImage, Subresources, StateBits);

        if (CurrentCommandBuffer)
        {
            CurrentCommandBuffer->ReferencedResources.push_back(Image);
        }
    }

    void FVulkanCommandList::SetBufferState(FRHIBuffer* Buffer, EResourceStates StateBits)
    {
        FVulkanBuffer* VulkanBuffer = static_cast<FVulkanBuffer*>(Buffer);

        StateTracker.RequireBufferState(VulkanBuffer, StateBits);
        
        if (CurrentCommandBuffer)
        {
            CurrentCommandBuffer->ReferencedResources.push_back(Buffer);
        }
    }

    EResourceStates FVulkanCommandList::GetImageSubresourceState(FRHIImage* Image, uint32 ArraySlice, uint32 MipLevel)
    {
        FVulkanImage* VulkanImage = static_cast<FVulkanImage*>(Image);

        return StateTracker.GetTextureSubresourceState(VulkanImage, ArraySlice, MipLevel);
    }

    EResourceStates FVulkanCommandList::GetBufferState(FRHIBuffer* Buffer)
    {
        FVulkanBuffer* VulkanBuffer = static_cast<FVulkanBuffer*>(Buffer);

        return StateTracker.GetBufferState(VulkanBuffer);
    }

    void FVulkanCommandList::EnableAutomaticBarriers()
    {
        PendingState.AddPendingState(EPendingCommandState::AutomaticBarriers);
    }

    void FVulkanCommandList::DisableAutomaticBarriers()
    {
        PendingState.ClearPendingState(EPendingCommandState::AutomaticBarriers);
    }

    void FVulkanCommandList::CommitBarriers()
    {
        LUMINA_PROFILE_SCOPE();
        
        if (StateTracker.GetBufferBarriers().empty() && StateTracker.GetTextureBarriers().empty())
        {
            return;
        }

        EndRenderPass();

        CommitBarriersInternal();
    }

    void FVulkanCommandList::SetResourceStatesForBindingSet(FRHIBindingSet* BindingSet)
    {
        LUMINA_PROFILE_SCOPE();

        if (BindingSet->GetDesc() == nullptr)
        {
            return; // Bindless.
        }
        
        FVulkanBindingSet* VkBindingSet = static_cast<FVulkanBindingSet*>(BindingSet);
        
        for (uint32 Binding : VkBindingSet->BindingsRequiringTransitions)
        {
            const FBindingSetItem& Item = VkBindingSet->Desc.Bindings[Binding];

            switch (Item.Type)
            {
                case ERHIBindingResourceType::Texture_SRV:
                    {
                        FVulkanImage* VulkanImage = static_cast<FVulkanImage*>(Item.ResourceHandle);
                        RequireTextureState(VulkanImage, Item.GetTextureResource().Subresources, EResourceStates::ShaderResource);
                    }
                    break;
                case ERHIBindingResourceType::Texture_UAV:
                    {
                        FVulkanImage* VulkanImage = static_cast<FVulkanImage*>(Item.ResourceHandle);
                        RequireTextureState(VulkanImage, Item.GetTextureResource().Subresources, EResourceStates::UnorderedAccess);
                    }
                    break;
                case ERHIBindingResourceType::Buffer_SRV:
                    {
                        FVulkanBuffer* Buffer = static_cast<FVulkanBuffer*>(Item.ResourceHandle);
                        RequireBufferState(Buffer, EResourceStates::ShaderResource);
                    }
                    break;
                case ERHIBindingResourceType::Buffer_UAV:
                    {
                        FVulkanBuffer* Buffer = static_cast<FVulkanBuffer*>(Item.ResourceHandle);
                        RequireBufferState(Buffer, EResourceStates::UnorderedAccess);
                    }
                    break;
                case ERHIBindingResourceType::Buffer_CBV:
                    {
                        FVulkanBuffer* Buffer = static_cast<FVulkanBuffer*>(Item.ResourceHandle);
                        RequireBufferState(Buffer, EResourceStates::ConstantBuffer);
                    }
                    break;
            }
        }
    }

    void FVulkanCommandList::SetResourceStateForRenderPass(const FRenderPassDesc& PassInfo)
    {
        for (const FRenderPassDesc::FAttachment& Attachment : PassInfo.ColorAttachments)
        {
            SetImageState(Attachment.Image, Attachment.Subresources, EResourceStates::RenderTarget);
            if (Attachment.ResolveImage)
            {
                SetImageState(Attachment.ResolveImage, Attachment.Subresources, EResourceStates::RenderTarget);
            }
        }
        

        if (PassInfo.DepthAttachment.IsValid())
        {
            if (PassInfo.DepthAttachment.LoadOp == ERenderLoadOp::Clear || PassInfo.DepthAttachment.StoreOp != ERenderStoreOp::DontCare)
            {
                SetImageState(PassInfo.DepthAttachment.Image, PassInfo.DepthAttachment.Subresources, EResourceStates::DepthWrite);
            }
            else
            {
                SetImageState(PassInfo.DepthAttachment.Image, PassInfo.DepthAttachment.Subresources, EResourceStates::DepthRead);
            }

            if (PassInfo.DepthAttachment.ResolveImage)
            {
                SetImageState(PassInfo.DepthAttachment.ResolveImage, PassInfo.DepthAttachment.Subresources, EResourceStates::DepthWrite);
            }
        }
    }

    void FVulkanCommandList::BeginTimerQuery(ITimerQuery* Query)
    {
        EndRenderPass();
        
        FVulkanTimerQuery* VulkanQuery = static_cast<FVulkanTimerQuery*>(Query);

        VulkanQuery->bResolved = false;
        
        auto VulkanContext = static_cast<FVulkanRenderContext*>(GRenderContext);
        
        vkCmdResetQueryPool(CurrentCommandBuffer->CommandBuffer, VulkanContext->GetTimerQueryPool(), VulkanQuery->BeginQueryIndex, 2);
        vkCmdWriteTimestamp(CurrentCommandBuffer->CommandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VulkanContext->GetTimerQueryPool(), VulkanQuery->BeginQueryIndex);
    }

    void FVulkanCommandList::EndTimerQuery(ITimerQuery* Query)
    {
        FVulkanTimerQuery* VulkanQuery = static_cast<FVulkanTimerQuery*>(Query);

        auto VulkanContext = static_cast<FVulkanRenderContext*>(GRenderContext);

        vkCmdWriteTimestamp(CurrentCommandBuffer->CommandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VulkanContext->GetTimerQueryPool(), VulkanQuery->EndQueryIndex);
        VulkanQuery->bStarted = true;
    }

    void FVulkanCommandList::BeginPipelineStatsQuery(IPipelineStatsQuery* Query)
    {
        // PIPELINE_STATISTICS query is invalid inside dynamic-rendering pass; flush first.
        EndRenderPass();

        FVulkanPipelineStatsQuery* VulkanQuery = static_cast<FVulkanPipelineStatsQuery*>(Query);

        VulkanQuery->bResolved = false;

        auto VulkanContext = static_cast<FVulkanRenderContext*>(GRenderContext);

        vkCmdResetQueryPool(CurrentCommandBuffer->CommandBuffer, VulkanContext->GetPipelineStatsQueryPool(), VulkanQuery->QueryIndex, 1);
        vkCmdBeginQuery(CurrentCommandBuffer->CommandBuffer, VulkanContext->GetPipelineStatsQueryPool(), VulkanQuery->QueryIndex, 0);
    }

    void FVulkanCommandList::EndPipelineStatsQuery(IPipelineStatsQuery* Query)
    {
        EndRenderPass();

        FVulkanPipelineStatsQuery* VulkanQuery = static_cast<FVulkanPipelineStatsQuery*>(Query);

        auto VulkanContext = static_cast<FVulkanRenderContext*>(GRenderContext);

        vkCmdEndQuery(CurrentCommandBuffer->CommandBuffer, VulkanContext->GetPipelineStatsQueryPool(), VulkanQuery->QueryIndex);
        VulkanQuery->bStarted = true;
    }

    void FVulkanCommandList::AddMarker(const char* RESTRICT Name, const FColor& RESTRICT Color)
    {
        if (PendingState.IsRecording() && GRenderContext->GetRenderContextDescription().bDebugUtils)
        {
            VkDebugUtilsLabelEXT Label = {};
            Label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
            Label.pLabelName = Name;
            Label.color[0] = Color.R;
            Label.color[1] = Color.G;
            Label.color[2] = Color.B;
            Label.color[3] = Color.A;
            vkCmdBeginDebugUtilsLabelEXT(CurrentCommandBuffer->CommandBuffer, &Label);
        }
    }

    void FVulkanCommandList::PopMarker()
    {
        if(PendingState.IsRecording() && GRenderContext->GetRenderContextDescription().bDebugUtils)
        {
            vkCmdEndDebugUtilsLabelEXT(CurrentCommandBuffer->CommandBuffer);
        }
    }

    void FVulkanCommandList::BeginProfilerZone(const char* Name, const FColor& Color)
    {
        if (!PendingState.IsRecording())
        {
            return;
        }

        AddMarker(Name, Color);

        #if defined(TRACY_ENABLE)
        TracyVkCtx ZoneContext = CurrentCommandBuffer->Queue ? CurrentCommandBuffer->Queue->TracyContext : nullptr;
        if (ZoneContext != nullptr && TracyZoneDepth < MaxTracyZoneDepth && Name != nullptr)
        {
            constexpr const char* SourceFile = __FILE__;
            constexpr const char* SourceFunc = "GPU";
            new (TracyZoneStorage[TracyZoneDepth]) tracy::VkCtxScope(
                ZoneContext,
                (uint32_t)__LINE__,
                SourceFile, sizeof(__FILE__) - 1,
                SourceFunc, 3,
                Name, strlen(Name),
                CurrentCommandBuffer->CommandBuffer,
                true);
            ++TracyZoneDepth;
        }
        #endif
    }

    void FVulkanCommandList::EndProfilerZone()
    {
        if (!PendingState.IsRecording())
        {
            return;
        }

#if defined(TRACY_ENABLE)
        if (TracyZoneDepth > 0)
        {
            --TracyZoneDepth;
            auto* Scope = std::launder(reinterpret_cast<tracy::VkCtxScope*>(TracyZoneStorage[TracyZoneDepth]));
            Scope->~VkCtxScope();
        }
#endif

        PopMarker();
    }
    
    void FVulkanCommandList::BeginRenderPass(const FRenderPassDesc& InPassInfo)
    {
        LUMINA_PROFILE_SCOPE();

        if (CurrentGraphicsState.RenderPass.IsValid())
        {
            EndRenderPass();
        }

        // Clear-only passes skip SetGraphicsState; promote states here or LoadOp::Clear WAWs are invisible to SyncVal.
        if (PendingState.IsInState(EPendingCommandState::AutomaticBarriers))
        {
            SetResourceStateForRenderPass(InPassInfo);
        }
        CommitBarriers();

        const SIZE_T NumColorAttachments = InPassInfo.ColorAttachments.size();
        TFixedVector<VkRenderingAttachmentInfo, 4> ColorAttachments(NumColorAttachments);
        VkRenderingAttachmentInfo DepthAttachment = {};

        uint32 NumArraySlices = 0;

        for (SIZE_T i = 0; i < NumColorAttachments; ++i)
        {
            const FRenderPassDesc::FAttachment& PassAttachment = InPassInfo.ColorAttachments[i];
            CurrentCommandBuffer->AddReferencedResource(PassAttachment.Image);
            FVulkanImage* VulkanImage = static_cast<FVulkanImage*>(PassAttachment.Image);

            const FRHIImageDesc& ImageDesc = VulkanImage->GetDescription();
            const FTextureSubresourceSet Subresource = PassAttachment.Subresources.Resolve(ImageDesc, true);

            EImageDimension Dimension = GetDimensionForFramebuffer(ImageDesc.Dimension, Subresource.NumArraySlices > 1);
            EFormat Format = PassAttachment.Format == EFormat::UNKNOWN ? ImageDesc.Format : PassAttachment.Format;

            VkImageView View = VulkanImage->GetSubresourceView(Subresource, Dimension, Format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT).View;

            VkRenderingAttachmentInfo& Attachment = ColorAttachments[i];
            Attachment = {};
            Attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            Attachment.imageView = View;
            Attachment.imageLayout = RenderContext->GetEffectiveImageLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            Attachment.loadOp = (PassAttachment.LoadOp == ERenderLoadOp::Clear) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            Attachment.storeOp = (PassAttachment.StoreOp == ERenderStoreOp::Store) ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;

            if (PassAttachment.ResolveImage != nullptr)
            {
                CurrentCommandBuffer->AddReferencedResource(PassAttachment.ResolveImage);
                FVulkanImage* VkResolveImage = static_cast<FVulkanImage*>(PassAttachment.ResolveImage);

                VkImageView ResolveView = VkResolveImage->GetSubresourceView(Subresource, Dimension, Format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT).View;

                Attachment.resolveMode = PickColorResolveMode(Format);
                Attachment.resolveImageView = ResolveView;
                Attachment.resolveImageLayout = RenderContext->GetEffectiveImageLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            }

            if (PassAttachment.LoadOp == ERenderLoadOp::Clear)
            {
                Attachment.clearValue.color.float32[0] = PassAttachment.ClearColor.r;
                Attachment.clearValue.color.float32[1] = PassAttachment.ClearColor.g;
                Attachment.clearValue.color.float32[2] = PassAttachment.ClearColor.b;
                Attachment.clearValue.color.float32[3] = PassAttachment.ClearColor.a;
            }

            if (NumArraySlices)
            {
                ASSERT(NumArraySlices == Subresource.NumArraySlices);
            }
            else
            {
                NumArraySlices = Subresource.NumArraySlices;
            }
        }


        const FRenderPassDesc::FAttachment& DepthDesc = InPassInfo.DepthAttachment;
        if (DepthDesc.IsValid())
        {
            FVulkanImage* VulkanImage = static_cast<FVulkanImage*>(DepthDesc.Image);
            CurrentCommandBuffer->AddReferencedResource(VulkanImage);

            const FRHIImageDesc& ImageDesc = VulkanImage->GetDescription();
            const FTextureSubresourceSet Subresource = DepthDesc.Subresources.Resolve(ImageDesc, true);

            EImageDimension Dimension = GetDimensionForFramebuffer(ImageDesc.Dimension, Subresource.NumArraySlices > 1);
            EFormat Format = DepthDesc.Format == EFormat::UNKNOWN ? ImageDesc.Format : DepthDesc.Format;

            VkImageView View = VulkanImage->GetSubresourceView(Subresource, Dimension, Format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT).View;

            DepthAttachment.sType        = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            DepthAttachment.imageView    = View;
            DepthAttachment.imageLayout  = RenderContext->GetEffectiveImageLayout(DepthDesc.bReadOnly ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
            DepthAttachment.loadOp       = (DepthDesc.LoadOp == ERenderLoadOp::Clear) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            DepthAttachment.storeOp      = (DepthDesc.StoreOp == ERenderStoreOp::Store) ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;

            DepthAttachment.clearValue.depthStencil.depth = DepthDesc.ClearColor.r;
            DepthAttachment.clearValue.depthStencil.stencil = (uint32)DepthDesc.ClearColor.g;

            if (DepthDesc.ResolveImage != nullptr)
            {
                CurrentCommandBuffer->AddReferencedResource(DepthDesc.ResolveImage);
                FVulkanImage* VkResolveImage = static_cast<FVulkanImage*>(DepthDesc.ResolveImage);
                VkImageView ResolveView = VkResolveImage->GetSubresourceView(Subresource, Dimension, Format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT).View;

                DepthAttachment.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
                DepthAttachment.resolveImageView = ResolveView;
                DepthAttachment.resolveImageLayout = RenderContext->GetEffectiveImageLayout(DepthDesc.bReadOnly ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
            }

            if (NumArraySlices)
            {
                ASSERT(NumArraySlices == Subresource.NumArraySlices);
            }
            else
            {
                NumArraySlices = Subresource.NumArraySlices;
            }
        }

        VkRenderingInfo RenderInfo          = {};
        RenderInfo.sType                    = VK_STRUCTURE_TYPE_RENDERING_INFO;
        RenderInfo.colorAttachmentCount     = (uint32)ColorAttachments.size();
        RenderInfo.pColorAttachments        = ColorAttachments.data();
        RenderInfo.pDepthAttachment         = (DepthAttachment.imageView != VK_NULL_HANDLE) ? &DepthAttachment : nullptr;
        RenderInfo.renderArea.extent.width  = InPassInfo.RenderArea.x;
        RenderInfo.renderArea.extent.height = InPassInfo.RenderArea.y;
        RenderInfo.layerCount               = 1;//NumArraySlices;
        RenderInfo.viewMask                 = InPassInfo.ViewMask;

        vkCmdBeginRendering(CurrentCommandBuffer->CommandBuffer, &RenderInfo);
        // Store un-derived pass desc so SampleCount default doesn't break SetGraphicsState equality.
        CurrentGraphicsState.RenderPass = InPassInfo;

    }
    
    void FVulkanCommandList::EndRenderPass()
    {
        if (CurrentGraphicsState.RenderPass.IsValid())
        {
            LUMINA_PROFILE_SCOPE();
            
            vkCmdEndRendering(CurrentCommandBuffer->CommandBuffer);
            CurrentGraphicsState.RenderPass = {};
        }
    }

    void FVulkanCommandList::ClearImageColor(FRHIImage* RESTRICT Image, const FColor& RESTRICT Color)
    {
        LUMINA_PROFILE_SCOPE();

        CurrentCommandBuffer->AddReferencedResource(Image);

        if (PendingState.IsInState(EPendingCommandState::AutomaticBarriers))
        {
            RequireTextureState(Image, FTextureSubresourceSet(0, 1, 0, 1), EResourceStates::CopyDest);
        }
        CommitBarriers();
        
        VkClearColorValue Value;
        Value.float32[0] = Color.R;
        Value.float32[1] = Color.G;
        Value.float32[2] = Color.B;
        Value.float32[3] = Color.A;

        VkImageSubresourceRange Range;
        Range.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;   // Clearing the color aspect of the image
        Range.baseMipLevel   = 0;                           // First mip level
        Range.levelCount     = 1;                           // Only clearing one mip level
        Range.baseArrayLayer = 0;                           // First layer in the image
        Range.layerCount     = 1;                           // Only clearing one layer
        
        vkCmdClearColorImage(CurrentCommandBuffer->CommandBuffer, Image->GetAPI<VkImage, EAPIResourceType::Image>(), RenderContext->GetEffectiveImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL), &Value, 1, &Range);
    }

    void FVulkanCommandList::BindBindingSets(VkPipelineBindPoint BindPoint, VkPipelineLayout PipelineLayout, const TFixedVector<FRHIBindingSet*, 4>& BindingSets)
    {
        LUMINA_PROFILE_SCOPE();

        //@ TODO This might not be possible to support both, since we allocate binding sets, so having an API that expects both
        
        uint32 CurrentBatchStart = UINT32_MAX;
        TFixedVector<VkDescriptorSet, 8> CurrentDescriptorBatch;
        TFixedVector<uint32, 16> DynamicOffsets;
    
        for (size_t i = 0; i < BindingSets.size(); ++i)
        {
            SIZE_T SetIndex = i;
            FRHIBindingSet* Set = BindingSets[SetIndex];

            if (CurrentBatchStart == UINT32_MAX)
            {
                CurrentBatchStart = (uint32)i;
            }
            
            if (Set->GetDesc())
            {
                FVulkanBindingSet* VulkanSet = static_cast<FVulkanBindingSet*>(Set);

                for (FRHIBuffer* DynamicBuffer : VulkanSet->DynamicBuffers)
                {
                    auto Found = DynamicBufferWrites.find(DynamicBuffer);
                    uint32 Version = (Found != DynamicBufferWrites.end()) ? (uint32)Found->second.LatestVersion : 0;
                    uint64 Offset = Version * DynamicBuffer->GetDescription().Size;
                    DynamicOffsets.push_back(static_cast<uint32>(Offset));
                }

                CurrentCommandBuffer->AddReferencedResource(VulkanSet);
                CurrentDescriptorBatch.push_back(VulkanSet->DescriptorSet);
            }
            else
            {
                FVulkanDescriptorTable* VulkanTable = static_cast<FVulkanDescriptorTable*>(Set);
                CurrentDescriptorBatch.push_back(VulkanTable->DescriptorSet);
            }
        }
        
        if (!CurrentDescriptorBatch.empty())
        {
            CommandListStats.NumBindings += CurrentDescriptorBatch.size();
            vkCmdBindDescriptorSets(CurrentCommandBuffer->CommandBuffer,
                BindPoint,
                PipelineLayout,
                CurrentBatchStart,
                static_cast<uint32>(CurrentDescriptorBatch.size()),
                CurrentDescriptorBatch.data(),
                static_cast<uint32>(DynamicOffsets.size()),
                DynamicOffsets.data());
        }
    }

    void FVulkanCommandList::SetPushConstants(const void* Data, SIZE_T ByteSize)
    {
        // Anything larger than the engine cap belongs in a UBO; pushing past
        // the device limit is undefined and crashes outright on AMD.
        ASSERT(ByteSize <= MaxPushConstantSize);

        CommandListStats.NumPushConstants++;
        const VkShaderStageFlags Stages = (PushConstantVisibility != 0) ? PushConstantVisibility : VK_SHADER_STAGE_ALL;
        vkCmdPushConstants(CurrentCommandBuffer->CommandBuffer, CurrentPipelineLayout, Stages, 0, (uint32)ByteSize, Data);
    }

    VkViewport FVulkanCommandList::ToVkViewport(float MinX, float MinY, float MinZ, float MaxX, float MaxY, float MaxZ)
    {
        // Y-flip is baked into the projection matrix; do NOT use negative-height viewport here.
        VkViewport Viewport = {};
        Viewport.x        = MinX;
        Viewport.y        = MinY;
        Viewport.width    = MaxX - MinX;
        Viewport.height   = MaxY - MinY;
        Viewport.minDepth = MinZ;
        Viewport.maxDepth = MaxZ;
        return Viewport;
    }
    
    VkRect2D FVulkanCommandList::ToVkScissorRect(uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY)
    {
        VkRect2D Scissor = {};
        Scissor.offset.x = (int32)MinX;
        Scissor.offset.y = (int32)MinY;
        Scissor.extent.width = std::abs((int32)(MaxX - MinX));
        Scissor.extent.height = std::abs((int32)(MaxY - MinY));

        return Scissor;
    }

    void FVulkanCommandList::SetGraphicsState(const FGraphicsState& State)
    {
        LUMINA_PROFILE_SCOPE();

        VkCommandBuffer VkCmdBuffer = CurrentCommandBuffer->CommandBuffer;

        if (PendingState.IsInState(EPendingCommandState::AutomaticBarriers))
        {
            TrackResourcesAndBarriers(State);
        }
        
        if (CurrentGraphicsState.Pipeline != State.Pipeline)
        {
            CommandListStats.NumPipelineSwitches++;
            vkCmdBindPipeline(VkCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, State.Pipeline->GetAPI<VkPipeline, EAPIResourceType::Pipeline>());
            CurrentCommandBuffer->AddReferencedResource(State.Pipeline);

            // Dynamic states declared in the shared VkPipeline must be set here; leaving them unset is undefined.
            const FDynamicPipelineStates& Dyn = RenderContext->GetDynamicPipelineStates();
            const FGraphicsDynamicStateValues& DV = static_cast<FVulkanGraphicsPipeline*>(State.Pipeline)->GetDynamicStateValues();

            if (Dyn.bCullMode)         vkCmdSetCullMode(VkCmdBuffer, DV.CullMode);
            if (Dyn.bFrontFace)        vkCmdSetFrontFace(VkCmdBuffer, DV.FrontFace);
            if (Dyn.bDepthTestEnable)  vkCmdSetDepthTestEnable(VkCmdBuffer, DV.DepthTestEnable);
            if (Dyn.bDepthWriteEnable) vkCmdSetDepthWriteEnable(VkCmdBuffer, DV.DepthWriteEnable);
            if (Dyn.bDepthCompareOp)   vkCmdSetDepthCompareOp(VkCmdBuffer, DV.DepthCompareOp);
            if (Dyn.bPolygonMode)      vkCmdSetPolygonModeEXT(VkCmdBuffer, DV.PolygonMode);

            if (DV.ColorAttachmentCount > 0)
            {
                if (Dyn.bColorBlendEnable)   vkCmdSetColorBlendEnableEXT(VkCmdBuffer, 0, DV.ColorAttachmentCount, DV.BlendEnable);
                if (Dyn.bColorBlendEquation) vkCmdSetColorBlendEquationEXT(VkCmdBuffer, 0, DV.ColorAttachmentCount, DV.BlendEquation);
                if (Dyn.bColorWriteMask)     vkCmdSetColorWriteMaskEXT(VkCmdBuffer, 0, DV.ColorAttachmentCount, DV.ColorWriteMask);
            }
        }

        if (CurrentGraphicsState.RenderPass != State.RenderPass)
        {
            EndRenderPass();
        }
        
        CommitBarriers();
        
        if (!CurrentGraphicsState.RenderPass.IsValid())
        {
            BeginRenderPass(State.RenderPass);
        }

        CurrentPipelineLayout = State.Pipeline->GetAPI<VkPipelineLayout, EAPIResourceType::PipelineLayout>();
        PushConstantVisibility = ((FVulkanGraphicsPipeline*)State.Pipeline)->PushConstantVisibility;

        if ((!VectorsAreEqual(CurrentGraphicsState.Bindings, State.Bindings)) || PendingState.IsInState(EPendingCommandState::DynamicBufferWrites))
        {
            BindBindingSets(VK_PIPELINE_BIND_POINT_GRAPHICS, CurrentPipelineLayout, State.Bindings);
        }

        if (!State.ViewportState.Viewports.empty() && !VectorsAreTriviallyEqual(State.ViewportState.Viewports, CurrentGraphicsState.ViewportState.Viewports))
        {
            TFixedVector<VkViewport, 16, false> Viewports;
            for (const FViewport& Viewport : State.ViewportState.Viewports)
            {
                Viewports.emplace_back(ToVkViewport(Viewport.MinX, Viewport.MinY, Viewport.MinZ, Viewport.MaxX, Viewport.MaxY, Viewport.MaxZ));
            }
            
            vkCmdSetViewport(CurrentCommandBuffer->CommandBuffer, 0, (uint32)Viewports.size(), Viewports.data());
        }


        if (!State.ViewportState.Scissors.empty() && !VectorsAreTriviallyEqual(State.ViewportState.Scissors, CurrentGraphicsState.ViewportState.Scissors))
        {
            TFixedVector<VkRect2D, 16, false> Scissors;
            for (const FRect& Rect : State.ViewportState.Scissors)
            {
                Scissors.emplace_back(ToVkScissorRect(Rect.MinX, Rect.MinY, Rect.MaxX, Rect.MaxY));
            }

            vkCmdSetScissor(CurrentCommandBuffer->CommandBuffer, 0, Scissors.size(), Scissors.data());
        }
        
        if (State.IndexBuffer.Buffer && CurrentGraphicsState.IndexBuffer != State.IndexBuffer)
        {
            vkCmdBindIndexBuffer(VkCmdBuffer, State.IndexBuffer.Buffer->GetAPI<VkBuffer>(), State.IndexBuffer.Offset
                , State.IndexBuffer.Format == EFormat::R16_UINT ?
                VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);

            CurrentCommandBuffer->AddReferencedResource(State.IndexBuffer.Buffer);
        }

        if (!State.VertexBuffers.empty() && !VectorsAreTriviallyEqual(State.VertexBuffers, CurrentGraphicsState.VertexBuffers))
        {
            VkBuffer VertexBuffer[16];
            uint64 VertexBufferOffsets[16];
            uint32 BindingIndex = 0;
            
            for (const FVertexBufferBinding& Binding : State.VertexBuffers)
            {
                VertexBuffer[Binding.Slot] = Binding.Buffer->GetAPI<VkBuffer>();
                VertexBufferOffsets[Binding.Slot] = Binding.Offset;
                BindingIndex = std::max(BindingIndex, Binding.Slot);
                
                CurrentCommandBuffer->AddReferencedResource(Binding.Buffer);
            }

            vkCmdBindVertexBuffers(VkCmdBuffer, 0, BindingIndex + 1, VertexBuffer, VertexBufferOffsets);
        }

        if (State.IndirectParams)
        {
            CurrentCommandBuffer->AddReferencedResource(State.IndirectParams);
        }

        CurrentGraphicsState = State;
        PendingState.ClearPendingState(EPendingCommandState::DynamicBufferWrites);
    }

    void FVulkanCommandList::SetScissor(const FRect& Rect)
    {
        // Order is (MinX, MinY, MaxX, MaxY); swapping Y/X args put MaxX into offset.y → garbage extents.
        const VkRect2D VkScissor = ToVkScissorRect(Rect.MinX, Rect.MinY, Rect.MaxX, Rect.MaxY);
        vkCmdSetScissor(CurrentCommandBuffer->CommandBuffer, 0, 1, &VkScissor);

        // Reflect the dynamic change in the cached state so the next SetGraphicsState
        // diffs against what's actually bound (and doesn't skip a needed update).
        CurrentGraphicsState.ViewportState.Scissors.assign(1, Rect);
    }

    void FVulkanCommandList::SetLineWidth(float Width)
    {

        vkCmdSetLineWidth(CurrentCommandBuffer->CommandBuffer, Width);
    }

    void FVulkanCommandList::Draw(uint32 VertexCount, uint32 InstanceCount, uint32 FirstVertex, uint32 FirstInstance)
    {
        UpdateGraphicsDynamicBuffers();
        
        CommandListStats.NumDrawCalls++;
        vkCmdDraw(CurrentCommandBuffer->CommandBuffer, VertexCount, InstanceCount, FirstVertex, FirstInstance);
    }

    void FVulkanCommandList::DrawIndexed(uint32 IndexCount, uint32 InstanceCount, uint32 FirstIndex, int32 VertexOffset, uint32 FirstInstance)
    {
        UpdateGraphicsDynamicBuffers();

        CommandListStats.NumDrawCalls++;

        vkCmdDrawIndexed(CurrentCommandBuffer->CommandBuffer, IndexCount, InstanceCount, FirstIndex, VertexOffset, FirstInstance);
    }

    void FVulkanCommandList::DrawIndirect(uint32 DrawCount, uint64 Offset)
    {
        UpdateGraphicsDynamicBuffers();

        CommandListStats.NumDrawCalls++;
        
        vkCmdDrawIndirect(CurrentCommandBuffer->CommandBuffer, CurrentGraphicsState.IndirectParams->GetAPI<VkBuffer>(), Offset, DrawCount, sizeof(FDrawIndirectArguments));
    }

    void FVulkanCommandList::DrawIndexedIndirect(uint32 DrawCount, uint64 Offset)
    {
        UpdateGraphicsDynamicBuffers();

        CommandListStats.NumDrawCalls++;

        vkCmdDrawIndexedIndirect(CurrentCommandBuffer->CommandBuffer, CurrentGraphicsState.IndirectParams->GetAPI<VkBuffer>(), Offset, DrawCount, sizeof(FDrawIndexedIndirectArguments));
    }

    void FVulkanCommandList::SetComputeState(const FComputeState& State)
    {
        LUMINA_PROFILE_SCOPE();

        EndRenderPass();

        FVulkanComputePipeline* ComputePipeline = static_cast<FVulkanComputePipeline*>(State.Pipeline);

        bool bBindingsEqual = VectorsAreEqual(State.Bindings, CurrentComputeState.Bindings);
        if (PendingState.IsInState(EPendingCommandState::AutomaticBarriers) && (!bBindingsEqual))
        {
            for (SIZE_T i = 0; i < ComputePipeline->GetDesc().BindingLayouts.size(); ++i)
            {
                FVulkanBindingLayout* Layout = static_cast<FVulkanBindingLayout*>(ComputePipeline->GetDesc().BindingLayouts[i].GetReference());

                if ((Layout->Desc.StageFlags.IsFlagCleared(ERHIShaderType::Compute)))
                {
                    continue;
                }

                SetResourceStatesForBindingSet(State.Bindings[i]);
            }
        }

        if (!VectorsAreEqual(State.BufferAccesses, CurrentComputeState.BufferAccesses))
        {
            const bool bAutoBarriers = PendingState.IsInState(EPendingCommandState::AutomaticBarriers);
            for (const FBufferAccess& Access : State.BufferAccesses)
            {
                // Keep BDA/bindless resources alive, no descriptor binding holds them otherwise.
                CurrentCommandBuffer->AddReferencedResource(Access.Buffer);
                if (bAutoBarriers)
                {
                    RequireBufferState(Access.Buffer, Access.State);
                }
            }
        }

        if (!VectorsAreEqual(State.ImageAccesses, CurrentComputeState.ImageAccesses))
        {
            const bool bAutoBarriers = PendingState.IsInState(EPendingCommandState::AutomaticBarriers);
            for (const FImageAccess& Access : State.ImageAccesses)
            {
                CurrentCommandBuffer->AddReferencedResource(Access.Image);
                if (bAutoBarriers)
                {
                    RequireTextureState(Access.Image, Access.Subresources, Access.State);
                }
            }
        }

        if (CurrentComputeState.Pipeline != ComputePipeline)
        {
            CommandListStats.NumPipelineSwitches++;
            vkCmdBindPipeline(CurrentCommandBuffer->CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ComputePipeline->Pipeline);
            CurrentCommandBuffer->AddReferencedResource(ComputePipeline);
        }

        if (PendingState.IsInState(EPendingCommandState::DynamicBufferWrites) || (!bBindingsEqual))
        {
            BindBindingSets(VK_PIPELINE_BIND_POINT_COMPUTE, ComputePipeline->PipelineLayout, State.Bindings);
        }

        CurrentPipelineLayout = ComputePipeline->PipelineLayout;
        PushConstantVisibility = ComputePipeline->PushConstantVisibility;

        if (State.IndirectParams && State.IndirectParams != CurrentComputeState.IndirectParams)
        {
            FVulkanBuffer* IndirectBuffer = static_cast<FVulkanBuffer*>(State.IndirectParams);

            CurrentCommandBuffer->AddReferencedResource(IndirectBuffer);

            RequireBufferState(IndirectBuffer, EResourceStates::IndirectArgument);
        }
        

        CommitBarriers();
        
        CurrentGraphicsState = {};
        CurrentComputeState = State;
        PendingState.ClearPendingState(EPendingCommandState::DynamicBufferWrites);
    }

    void FVulkanCommandList::Dispatch(uint32 GroupCountX, uint32 GroupCountY, uint32 GroupCountZ)
    {
        LUMINA_PROFILE_SCOPE();

        CommandListStats.NumDispatchCalls++;
        
        UpdateComputeDynamicBuffers();


        vkCmdDispatch(CurrentCommandBuffer->CommandBuffer, GroupCountX, GroupCountY, GroupCountZ);
    }

    
    // Stages that only exist in the rasterization pipeline.
    static constexpr VkPipelineStageFlags2 GGraphicsOnlyStages =
        VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT                         |
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT                        |
        VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT          |
        VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT       |
        VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT                      |
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT                      |
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT                 |
        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT                  |
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT              |
        VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT               |
        VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT            |
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;

    // Access types produced/consumed exclusively by graphics-only stages.
    static constexpr VkAccessFlags2 GGraphicsOnlyAccess =
        VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT                        |
        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT                        |
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT                       |
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT                |
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT               |
        VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT                        |
        VK_ACCESS_2_INDEX_READ_BIT                                   |
        VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT                 |
        VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT          |
        VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT         |
        VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT               |
        VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR;

    // Stages valid on a dedicated transfer queue.
    static constexpr VkPipelineStageFlags2 GTransferValidStages =
        VK_PIPELINE_STAGE_2_TRANSFER_BIT       |
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT    |
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT |
        VK_PIPELINE_STAGE_2_HOST_BIT           |
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    static constexpr VkAccessFlags2 GTransferValidAccess =
        VK_ACCESS_2_TRANSFER_READ_BIT  |
        VK_ACCESS_2_TRANSFER_WRITE_BIT |
        VK_ACCESS_2_HOST_READ_BIT      |
        VK_ACCESS_2_HOST_WRITE_BIT     |
        VK_ACCESS_2_MEMORY_READ_BIT    |
        VK_ACCESS_2_MEMORY_WRITE_BIT;

    // Strip stage/access bits invalid for the queue; fall back to ALL_COMMANDS if empty.
    static void FilterBarrierForQueue(VkPipelineStageFlags2& StageMask,
                                      VkAccessFlags2& AccessMask,
                                      ECommandQueue Queue)
    {
        if (Queue == ECommandQueue::Graphics)
        {
            return;
        }

        if (Queue == ECommandQueue::Compute)
        {
            StageMask  &= ~GGraphicsOnlyStages;
            AccessMask &= ~GGraphicsOnlyAccess;
        }
        else if (Queue == ECommandQueue::Transfer)
        {
            StageMask  &= GTransferValidStages;
            AccessMask &= GTransferValidAccess;
        }

        if (StageMask == 0)
        {
            StageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        }
    }

    void FVulkanCommandList::CommitBarriersInternal()
    {
        LUMINA_PROFILE_SCOPE();

        TFixedVector<VkImageMemoryBarrier2, 64> ImageBarriers;
        TFixedVector<VkBufferMemoryBarrier2, 32> BufferBarriers;

        VkCommandBuffer CommandBuffer = CurrentCommandBuffer->CommandBuffer;

        for (const FTextureBarrier& Barrier : StateTracker.GetTextureBarriers())
        {
            FResourceStateMapping2 Before = Vk::ConvertResourceState2(Barrier.StateBefore);
            FResourceStateMapping2 After = Vk::ConvertResourceState2(Barrier.StateAfter);

            FilterBarrierForQueue(Before.StageFlags, Before.AccessMask, Info.CommandQueue);
            FilterBarrierForQueue(After.StageFlags,  After.AccessMask,  Info.CommandQueue);

            // Under unified image layouts, every transition collapses to GENERAL->GENERAL
            // (no layout change); only the access/stage sync remains.
            Before.ImageLayout = RenderContext->GetEffectiveImageLayout(Before.ImageLayout);
            After.ImageLayout  = RenderContext->GetEffectiveImageLayout(After.ImageLayout);

            ASSERT(After.ImageLayout != VK_IMAGE_LAYOUT_UNDEFINED);

            FVulkanImage* Image = static_cast<FVulkanImage*>(Barrier.Texture);
            const FFormatInfo& formatInfo = RHI::Format::Info(Image->GetDescription().Format);

            VkImageAspectFlags AspectMask = 0;
            if (formatInfo.bHasDepth)
            {
                AspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
            }
            if (formatInfo.bHasStencil)
            {
                AspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
            if (!AspectMask)
            {
                AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            }

            VkImageSubresourceRange SubresourceRange = {};
            SubresourceRange.baseArrayLayer = Barrier.bEntireTexture ? 0 : Barrier.ArraySlice;
            SubresourceRange.layerCount     = Barrier.bEntireTexture ? Image->GetDescription().ArraySize : Barrier.NumArraySlices;
            SubresourceRange.baseMipLevel   = Barrier.bEntireTexture ? 0 : Barrier.MipLevel;
            SubresourceRange.levelCount     = Barrier.bEntireTexture ? Image->GetDescription().NumMips : Barrier.NumMipLevels;
            SubresourceRange.aspectMask     = AspectMask;

            VkImageMemoryBarrier2 ImageBarrier  = {};
            ImageBarrier.sType                  = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            ImageBarrier.srcAccessMask          = Before.AccessMask;
            ImageBarrier.dstAccessMask          = After.AccessMask;
            ImageBarrier.srcStageMask           = Before.StageFlags;
            ImageBarrier.dstStageMask           = After.StageFlags;
            ImageBarrier.oldLayout              = Before.ImageLayout;
            ImageBarrier.newLayout              = After.ImageLayout;
            ImageBarrier.srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
            ImageBarrier.dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
            ImageBarrier.image                  = Image->GetAPI<VkImage, EAPIResourceType::Image>();
            ImageBarrier.subresourceRange       = SubresourceRange;

            ImageBarriers.push_back(ImageBarrier);
        }

        for (const FBufferBarrier& Barrier : StateTracker.GetBufferBarriers())
        {
            FResourceStateMapping2 Before = Vk::ConvertResourceState2(Barrier.StateBefore);
            FResourceStateMapping2 After = Vk::ConvertResourceState2(Barrier.StateAfter);

            FilterBarrierForQueue(Before.StageFlags, Before.AccessMask, Info.CommandQueue);
            FilterBarrierForQueue(After.StageFlags,  After.AccessMask,  Info.CommandQueue);

            FVulkanBuffer* Buffer = static_cast<FVulkanBuffer*>(Barrier.Buffer);

            VkBufferMemoryBarrier2 BufferBarrier    = {};
            BufferBarrier.sType                     = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            BufferBarrier.srcAccessMask             = Before.AccessMask;
            BufferBarrier.dstAccessMask             = After.AccessMask;
            BufferBarrier.srcStageMask              = Before.StageFlags;
            BufferBarrier.dstStageMask              = After.StageFlags;
            BufferBarrier.srcQueueFamilyIndex       = VK_QUEUE_FAMILY_IGNORED;
            BufferBarrier.dstQueueFamilyIndex       = VK_QUEUE_FAMILY_IGNORED;
            BufferBarrier.buffer                    = Buffer->Buffer;
            BufferBarrier.offset                    = 0;
            BufferBarrier.size                      = Buffer->GetDescription().Size;

            BufferBarriers.push_back(BufferBarrier);
        }

        if (!BufferBarriers.empty() || !ImageBarriers.empty())
        {
            const uint32 NumBufferBarriers = (uint32)BufferBarriers.size();
            const uint32 NumImageBarriers  = (uint32)ImageBarriers.size();

            VkDependencyInfo DependencyInfo         = {};
            DependencyInfo.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;

            DependencyInfo.pBufferMemoryBarriers    = BufferBarriers.data();
            DependencyInfo.bufferMemoryBarrierCount = NumBufferBarriers;

            DependencyInfo.pImageMemoryBarriers     = ImageBarriers.data();
            DependencyInfo.imageMemoryBarrierCount  = NumImageBarriers;

            vkCmdPipelineBarrier2(CommandBuffer, &DependencyInfo);

            // Per-frame barrier accounting (surfaced by the GPU profiler + Tracy).
            CommandListStats.NumBarriers += NumBufferBarriers + NumImageBarriers;
            FGPUProfiler::Get().AddBarriers(NumBufferBarriers, NumImageBarriers);
        }

        ImageBarriers.clear();
        BufferBarriers.clear();
        StateTracker.ClearBarriers();
    }   
    
    void FVulkanCommandList::TrackResourcesAndBarriers(const FGraphicsState& State)
    {
        if (!VectorsAreEqual(State.Bindings, CurrentGraphicsState.Bindings))
        {
            for (SIZE_T i = 0; i < State.Bindings.size(); ++i)
            {
                SetResourceStatesForBindingSet(State.Bindings[i]);
            }
        }

        if (!VectorsAreEqual(State.BufferAccesses, CurrentGraphicsState.BufferAccesses))
        {
            for (const FBufferAccess& Access : State.BufferAccesses)
            {
                // Declared access -> keep alive for the submission (see SetComputeState).
                CurrentCommandBuffer->AddReferencedResource(Access.Buffer);
                RequireBufferState(Access.Buffer, Access.State);
            }
        }

        if (!VectorsAreEqual(State.ImageAccesses, CurrentGraphicsState.ImageAccesses))
        {
            for (const FImageAccess& Access : State.ImageAccesses)
            {
                CurrentCommandBuffer->AddReferencedResource(Access.Image);
                RequireTextureState(Access.Image, Access.Subresources, Access.State);
            }
        }

        if (State.IndexBuffer.Buffer && State.IndexBuffer.Buffer != CurrentGraphicsState.IndexBuffer.Buffer)
        {
            RequireBufferState(State.IndexBuffer.Buffer, EResourceStates::IndexBuffer);
        }

        if (!VectorsAreEqual(State.VertexBuffers, CurrentGraphicsState.VertexBuffers))
        {
            for (const FVertexBufferBinding& Binding : State.VertexBuffers)
            {
                RequireBufferState(Binding.Buffer, EResourceStates::VertexBuffer);
            }
        }

        if (CurrentGraphicsState.RenderPass != State.RenderPass)
        {
            SetResourceStateForRenderPass(State.RenderPass);
        }
        
        if (State.IndirectParams && State.IndirectParams != CurrentGraphicsState.IndirectParams)
        {
            RequireBufferState(State.IndirectParams, EResourceStates::IndirectArgument);
        }
    }

    void FVulkanCommandList::RequireTextureState(FRHIImage* Texture, FTextureSubresourceSet Subresources, EResourceStates StateBits)
    {
        FVulkanImage* VulkanImage = static_cast<FVulkanImage*>(Texture);

        StateTracker.RequireTextureState(VulkanImage, Subresources, StateBits);
    }

    void FVulkanCommandList::RequireBufferState(FRHIBuffer* Buffer, EResourceStates StateBits)
    {
        FVulkanBuffer* VulkanBuffer = static_cast<FVulkanBuffer*>(Buffer);
        
        StateTracker.RequireBufferState(VulkanBuffer, StateBits);
    }
    
    void* FVulkanCommandList::GetAPIResourceImpl(EAPIResourceType)
    {
        return CurrentCommandBuffer->CommandBuffer;
    }
}
