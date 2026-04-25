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

        // Minimum size of memory chunks created to upload data to the device on DX12.
        size_t UploadChunkSize = 64 * 1024;

        // Minimum size of memory chunks created for AS build scratch buffers.
        size_t ScratchChunkSize = 64 * 1024;

        // Maximum total memory size used for all AS build scratch buffers owned by this command list.
        size_t ScratchMaxMemory = 1024 * 1024 * 1024;
        
        /** Type of command queue that this list is to be executed on */
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

        /**
         * Returns the underlying command list. Validation wrappers override this
         * to return their inner command list; concrete backends return themselves.
         */
        virtual ICommandList* GetUnwrappedCommandList() { return this; }

        /**
         * Opens the command list for recording commands
         */
        virtual void Open() = 0;
    
        /**
         * Closes the command list, preparing it for submission
         */
        virtual void Close() = 0;
    
        /**
         * Called when the command list has been executed by the GPU
         * @param Queue The queue that executed this command list
         * @param SubmissionID Unique identifier for this submission
         */
        virtual void Executed(FQueue* Queue, uint64 SubmissionID) = 0;
    
        /**
         * Copies a region from one GPU image to another
         * @param Src Source image to copy from
         * @param SrcSlice Region of the source image (array/mip levels)
         * @param Dst Destination image to copy to
         * @param DstSlice Region of the destination image (array/mip levels)
         */
        virtual void CopyImage(FRHIImage* Src, const FTextureSlice& SrcSlice, FRHIImage* Dst, const FTextureSlice& DstSlice) = 0;
    
        /**
         * Copies from a GPU image to a staging (CPU-accessible) image
         * @param Src Source GPU image to copy from
         * @param SrcSlice Region of the source image (array/mip levels)
         * @param Dst Destination staging image (CPU-accessible)
         * @param DstSlice Region of the destination image (array/mip levels)
         */
        virtual void CopyImage(FRHIImage* Src, const FTextureSlice& SrcSlice, FRHIStagingImage* Dst, const FTextureSlice& DstSlice) = 0;
        
        /**
         * Copies from a staging (CPU-accessible) image to a GPU image
         * @param Src Source staging image (CPU-accessible)
         * @param SrcSlice Region of the source image (array/mip levels)
         * @param Dst Destination GPU image to copy to
         * @param DstSlice Region of the destination image (array/mip levels)
         */
        virtual void CopyImage(FRHIStagingImage* Src, const FTextureSlice& SrcSlice, FRHIImage* Dst, const FTextureSlice& DstSlice) = 0;
    
        /**
         * Writes CPU data directly to a GPU image
         * @param Dst Destination image to write to
         * @param ArraySlice Array layer index to write to
         * @param MipLevel Mip level to write to
         * @param Data Pointer to source data in CPU memory (non-aliasing)
         * @param RowPitch Bytes between rows in the source data
         * @param DepthPitch Bytes between depth slices in the source data
         */
        virtual void WriteImage(FRHIImage* Dst, uint32 ArraySlice, uint32 MipLevel, const void* Data, uint32 RowPitch, uint32 DepthPitch) = 0;

        /**
         * 
         * @param Src 
         * @param SrcSubresources 
         * @param Dst 
         * @param DstSubresources 
         */
        virtual void ResolveImage(FRHIImage* Src, const FTextureSubresourceSet& SrcSubresources, FRHIImage* Dst, const FTextureSubresourceSet& DstSubresources) = 0;
        
        /**
         * Clears an image with a floating-point color value
         * @param Image Image to clear
         * @param Subresource Which array/mip levels to clear
         * @param Color Clear color (floating-point)
         */
        virtual void ClearImageFloat(FRHIImage* Image, FTextureSubresourceSet Subresource, const FColor& Color) = 0;
        
        /**
         * Clears an image with an unsigned integer value
         * @param Image Image to clear
         * @param Subresource Which array/mip levels to clear
         * @param Color Clear value (unsigned integer)
         */
        virtual void ClearImageUInt(FRHIImage* Image, FTextureSubresourceSet Subresource, uint32 Color) = 0;
    
        /**
         * Writes CPU data directly to a GPU buffer
         * @param Buffer Destination buffer to write to
         * @param Data Pointer to source data in CPU memory (non-aliasing)
         * @param Offset Byte offset in the destination buffer
         * @param Size Number of bytes to write
         */
        virtual void WriteBuffer(FRHIBuffer* Buffer, const void* Data, size_t Size, size_t Offset = 0) = 0;
        
        /**
         * Fills an entire buffer with a repeated 32-bit value
         * @param Buffer Buffer to fill
         * @param Value 32-bit value to repeat throughout the buffer
         */
        virtual void FillBuffer(FRHIBuffer* Buffer, uint32 Value) = 0;
        
        /**
         * Copies data from one buffer to another
         * @param Source Source buffer to copy from
         * @param SrcOffset Byte offset in the source buffer
         * @param Destination Destination buffer to copy to
         * @param DstOffset Byte offset in the destination buffer
         * @param CopySize Number of bytes to copy
         */
        virtual void CopyBuffer(FRHIBuffer* Source, uint64 SrcOffset, FRHIBuffer* Destination, uint64 DstOffset, uint64 CopySize) = 0;


        virtual void SetEnableUavBarriersForImage(FRHIImage* Image, bool bEnableBarriers) = 0;
        virtual void SetEnableUavBarriersForBuffer(FRHIBuffer* Buffer, bool bEnableBarriers) = 0;

        
        /**
         * Sets a fixed resource state that won't be automatically tracked
         * @param Image Image to set permanent state for
         * @param StateBits Resource state flags (e.g., read-only, shader resource)
         */
        virtual void SetPermanentImageState(FRHIImage* Image, EResourceStates StateBits) = 0;
        
        /**
         * Sets a fixed resource state that won't be automatically tracked
         * @param Buffer Buffer to set permanent state for
         * @param StateBits Resource state flags (e.g., read-only, shader resource)
         */
        virtual void SetPermanentBufferState(FRHIBuffer* Buffer, EResourceStates StateBits) = 0;
        
        /**
         * Begins tracking state changes for an image's subresources
         * @param Image Image to start tracking
         * @param Subresources Which array/mip levels to track
         * @param StateBits Initial resource state
         */
        virtual void BeginTrackingImageState(FRHIImage* Image, FTextureSubresourceSet Subresources, EResourceStates StateBits) = 0;
        
        /**
         * Begins tracking state changes for a buffer
         * @param Buffer Buffer to start tracking
         * @param StateBits Initial resource state
         */
        virtual void BeginTrackingBufferState(FRHIBuffer* Buffer, EResourceStates StateBits) = 0;
        
        /**
         * Transitions an image to a new resource state
         * @param Image Image to transition
         * @param Subresources Which array/mip levels to transition
         * @param StateBits Target resource state
         */
        virtual void SetImageState(FRHIImage* Image, FTextureSubresourceSet Subresources, EResourceStates StateBits) = 0;
        
        /**
         * Transitions a buffer to a new resource state
         * @param Buffer Buffer to transition
         * @param StateBits Target resource state
         */
        virtual void SetBufferState(FRHIBuffer* Buffer, EResourceStates StateBits) = 0;
        
        /**
         * Transitions all resources in a binding set to their required states
         * @param BindingSet Binding set containing resources to transition
         */
        virtual void SetResourceStatesForBindingSet(FRHIBindingSet* BindingSet) = 0;
        
        /**
         * Transitions resources to states required for a render pass
         * @param PassInfo Render pass descriptor containing attachment states
         */
        virtual void SetResourceStateForRenderPass(const FRenderPassDesc& PassInfo) = 0;
    
        /**
         * Enables automatic insertion of resource barriers
         */
        virtual void EnableAutomaticBarriers() = 0;
        
        /**
         * Disables automatic insertion of resource barriers (manual control)
         */
        virtual void DisableAutomaticBarriers() = 0;
        
        /**
         * Flushes all pending resource barriers to the GPU
         */
        virtual void CommitBarriers() = 0;
    
        /**
         * Queries the current resource state of a specific image subresource
         * @param Image Image to query
         * @param ArraySlice Array layer index
         * @param MipLevel Mip level
         * @return Current resource state flags
         */
        NODISCARD virtual EResourceStates GetImageSubresourceState(FRHIImage* Image, uint32 ArraySlice, uint32 MipLevel) = 0;
        
        /**
         * Queries the current resource state of a buffer
         * @param Buffer Buffer to query
         * @return Current resource state flags
         */
        NODISCARD virtual EResourceStates GetBufferState(FRHIBuffer* Buffer) = 0;
    
        
        virtual void BeginTimerQuery(ITimerQuery* Query) = 0;
        virtual void EndTimerQuery(ITimerQuery* Query) = 0;

        virtual void BeginPipelineStatsQuery(IPipelineStatsQuery* Query) = 0;
        virtual void EndPipelineStatsQuery(IPipelineStatsQuery* Query) = 0;
        
        /**
         * Begins a debug marker region for GPU profiling/debugging
         * @param Name Label for the marker region (non-aliasing)
         * @param Color Color for visualization in profiling tools
         */
        virtual void AddMarker(const char* Name, const FColor& Color = FColor::Red) = 0;
        
        /**
         * Ends the current debug marker region
         */
        virtual void PopMarker() = 0;
        
        /**
         * Begins a render pass with the specified attachments and operations
         * @param PassInfo Descriptor containing render targets and depth/stencil setup
         */
        virtual void BeginRenderPass(const FRenderPassDesc& PassInfo) = 0;
        
        /**
         * Ends the current render pass
         */
        virtual void EndRenderPass() = 0;
        
        /**
         * Clears a color attachment during a render pass
         * @param Image Image to clear (must be a current render target)
         * @param Color Clear color value
         */
        virtual void ClearImageColor(FRHIImage* Image, const FColor& Color) = 0;
        
        /**
         * Updates push constants for the currently bound pipeline
         * @param Data Pointer to push constant data (non-aliasing)
         * @param ByteSize Size of push constant data in bytes
         */
        virtual void SetPushConstants(const void* Data, size_t ByteSize) = 0;
        
        /**
         * Binds a complete graphics pipeline state
         * @param State Graphics state containing pipeline, bindings, and render targets
         */
        virtual void SetGraphicsState(const FGraphicsState& State) = 0;
        
        /**
         * Issues a non-indexed draw call
         * @param VertexCount Number of vertices to draw
         * @param InstanceCount Number of instances to draw
         * @param FirstVertex Starting vertex index
         * @param FirstInstance Starting instance ID
         */
        virtual void Draw(uint32 VertexCount, uint32 InstanceCount, uint32 FirstVertex, uint32 FirstInstance) = 0;
        
        /**
         * Issues an indexed draw call
         * @param IndexCount Number of indices to draw
         * @param InstanceCount Number of instances to draw
         * @param FirstIndex Starting index in the index buffer
         * @param VertexOffset Offset added to each index value
         * @param FirstInstance Starting instance ID
         */
        virtual void DrawIndexed(uint32 IndexCount, uint32 InstanceCount = 1, uint32 FirstIndex = 1, int32 VertexOffset = 0, uint32 FirstInstance = 0) = 0;
        
        /**
         * Issues draw calls from GPU buffer (indirect drawing)
         * @param DrawCount Number of draw commands in the buffer
         * @param Offset Byte offset to the first draw command
         */
        virtual void DrawIndirect(uint32 DrawCount, uint64 Offset) = 0;
        
        /**
         * Issues indexed draw calls from GPU buffer (indirect drawing)
         * @param DrawCount Number of draw commands in the buffer
         * @param Offset Byte offset to the first draw command
         */
        virtual void DrawIndexedIndirect(uint32 DrawCount, uint64 Offset) = 0;
    
        /**
         * Binds a complete compute pipeline state
         * @param State Compute state containing pipeline and bindings
         */
        virtual void SetComputeState(const FComputeState& State) = 0;
        
        /**
         * Dispatches a compute shader
         * @param GroupCountX Number of thread groups in X dimension
         * @param GroupCountY Number of thread groups in Y dimension
         * @param GroupCountZ Number of thread groups in Z dimension
         */
        virtual void Dispatch(uint32 GroupCountX, uint32 GroupCountY, uint32 GroupCountZ) = 0;
        
        /**
         * Gets metadata about this command list
         * @return Command list information structure
         */
        NODISCARD virtual const FCommandListInfo& GetCommandListInfo() const = 0;
        
        /**
         * Gets the current pending command state (for state tracking)
         * @return Mutable reference to pending command state
         */
        NODISCARD virtual FPendingCommandState& GetPendingCommandState() = 0;
    
        /**
         * Gets performance statistics for this command list
         * @return Statistics tracker with draw calls, dispatches, etc.
         */
        NODISCARD virtual const FCommandListStatTracker& GetCommandListStats() const = 0;
    };
}