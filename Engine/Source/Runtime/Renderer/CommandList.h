#pragma once

#include "PendingState.h"
#include "RenderResource.h"
#include "Core/Math/Color.h"
#include "Memory/Memcpy.h"
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
        // The persistent frame command list funnels all per-frame transient allocations (ImGui/RmlUi
        // vertex+index buffers, world debug geometry, per-pass constant uploads) through one upload
        // pool. Chunks only recycle once their GPU submission completes, so with several frames in
        // flight a too-small chunk forces a fresh vkCreateBuffer every frame at each chunk boundary.
        // 2 MB holds a typical frame's demand in one/two chunks that then pool + recycle.
        size_t UploadChunkSize = 2 * 1024 * 1024;
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
        uint32 NumTransientAllocs = 0;
    };

    // One-shot host-visible scratch from the command list's per-frame ring; write Cpu, read on
    // GPU via the BDA in Gpu. Lifetime is the current command-list submission.
    struct FTransientAlloc
    {
        void*       Cpu     = nullptr;
        uint64      Gpu     = 0;        // device address; already includes Offset
        FRHIBuffer* Buffer  = nullptr;  // ring chunk this allocation lives in
        uint64      Offset  = 0;
        uint64      Size    = 0;

        NODISCARD bool IsValid() const { return Cpu != nullptr; }
        explicit operator bool() const { return IsValid(); }
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

        // Keep Resource alive until this cmd list's submission retires; for native API draws that
        // bypass our Bind*/Copy* tracking (e.g. ImGui_ImplVulkan_RenderDrawData).
        virtual void KeepAlive(IRHIResource* Resource) = 0;

        virtual void CopyImage(FRHIImage* Src, const FTextureSlice& SrcSlice, FRHIImage* Dst, const FTextureSlice& DstSlice) = 0;
        virtual void CopyImage(FRHIImage* Src, const FTextureSlice& SrcSlice, FRHIStagingImage* Dst, const FTextureSlice& DstSlice) = 0;
        virtual void CopyImage(FRHIStagingImage* Src, const FTextureSlice& SrcSlice, FRHIImage* Dst, const FTextureSlice& DstSlice) = 0;

        virtual void WriteImage(FRHIImage* Dst, uint32 ArraySlice, uint32 MipLevel, const void* Data, uint32 RowPitch, uint32 DepthPitch) = 0;

        // Upload a sub-rectangle of a mip; Data is the region's top-left texel and RowPitch the
        // source stride (may span a wider backing buffer, so a dirty rect uploads from a full CPU array).
        virtual void WriteImageRegion(FRHIImage* Dst, uint32 ArraySlice, uint32 MipLevel, uint32 OffsetX, uint32 OffsetY, uint32 Width, uint32 Height, const void* Data, uint32 RowPitch) = 0;

        virtual void ResolveImage(FRHIImage* Src, const FTextureSubresourceSet& SrcSubresources, FRHIImage* Dst, const FTextureSubresourceSet& DstSubresources) = 0;

        virtual void ClearImageFloat(FRHIImage* Image, FTextureSubresourceSet Subresource, const FColor& Color) = 0;
        virtual void ClearImageUInt(FRHIImage* Image, FTextureSubresourceSet Subresource, uint32 Color) = 0;

        virtual void WriteBuffer(FRHIBuffer* Buffer, const void* Data, size_t Size, size_t Offset = 0) = 0;
        virtual void FillBuffer(FRHIBuffer* Buffer, uint32 Value) = 0;
        virtual void CopyBuffer(FRHIBuffer* Source, uint64 SrcOffset, FRHIBuffer* Destination, uint64 DstOffset, uint64 CopySize) = 0;

        // Suballocate Size bytes from the per-frame ring; returns a CPU write pointer + GPU device address.
        // Alive for this submission only -- never persist Cpu/Gpu beyond the command-list scope.
        virtual FTransientAlloc AllocateTransient(uint64 Size, uint32 Alignment = 16) = 0;

        // Typed handle over a transient allocation: write fields straight into Data (mapped host
        // memory, valid for this submission only), then push Gpu (the BDA) to the shader. This is the
        // one transient-upload primitive -- AllocTransient reserves, CopyTransient* reserve + copy.
        template<typename T>
        struct TTransientPtr
        {
            T*          Data   = nullptr;   // mapped CPU pointer -- write here
            uint64      Gpu    = 0;         // device address (BDA) for the shader
            FRHIBuffer* Buffer = nullptr;
            uint64      Offset = 0;
            uint64      Count  = 0;

            NODISCARD bool IsValid() const { return Data != nullptr; }
            explicit operator bool() const { return IsValid(); }
            T* operator->() const { return Data; }
            T& operator*()  const { return *Data; }
            T& operator[](uint64 i) const { return Data[i]; }
        };

        // Reserve Count contiguous T's in the ring; write into the returned Data pointer directly.
        // Default alignment 16 is safe for BDA structs with float4 members. THE primary path -- prefer
        // building data straight into the ring over building a temp and copying.
        template<typename T>
        TTransientPtr<T> AllocTransient(uint64 Count = 1, uint32 Alignment = 16)
        {
            FTransientAlloc Alloc = AllocateTransient(sizeof(T) * Count, Alignment);
            TTransientPtr<T> Result;
            Result.Data   = static_cast<T*>(Alloc.Cpu);
            Result.Gpu    = Alloc.Gpu;
            Result.Buffer = Alloc.Buffer;
            Result.Offset = Alloc.Offset;
            Result.Count  = Count;
            return Result;
        }

        // Reserve + copy a value already built elsewhere (e.g. game-thread FFrameData). Use AllocTransient
        // instead when you can produce the data in place.
        template<typename T>
        TTransientPtr<T> CopyTransient(const T& Data, uint32 Alignment = 16)
        {
            TTransientPtr<T> Alloc = AllocTransient<T>(1, Alignment);
            if (Alloc.Data)
            {
                Memory::Memcpy(Alloc.Data, &Data, sizeof(T));
            }
            return Alloc;
        }

        // Reserve + copy a tightly-packed array. Stride is sizeof(T).
        template<typename T>
        TTransientPtr<T> CopyTransientArray(const T* Data, uint64 Count, uint32 Alignment = 16)
        {
            TTransientPtr<T> Alloc = AllocTransient<T>(Count, Alignment);
            if (Alloc.Data && Count > 0)
            {
                Memory::Memcpy(Alloc.Data, Data, sizeof(T) * Count);
            }
            return Alloc;
        }


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

        // Begin a profiler scope: emits a Vulkan debug label AND a Tracy GPU zone.
        // Each Begin must be paired with End. Default no-op for non-Vulkan backends.
        virtual void BeginProfilerZone(const char* Name, const FColor& Color = FColor::White) {}
        virtual void EndProfilerZone() {}

        virtual void BeginRenderPass(const FRenderPassDesc& PassInfo) = 0;
        virtual void EndRenderPass() = 0;

        virtual void ClearImageColor(FRHIImage* Image, const FColor& Color) = 0;

        virtual void SetPushConstants(const void* Data, size_t ByteSize) = 0;

        virtual void SetGraphicsState(const FGraphicsState& State) = 0;

        // Dynamic scissor without rebuilding graphics state (valid after SetGraphicsState); lets a
        // batched pass (e.g. UI) change clip rects per draw without a full state set.
        virtual void SetScissor(const FRect& Rect) = 0;

        virtual void SetLineWidth(float Width) = 0;

        virtual void Draw(uint32 VertexCount, uint32 InstanceCount, uint32 FirstVertex, uint32 FirstInstance) = 0;
        virtual void DrawIndexed(uint32 IndexCount, uint32 InstanceCount = 1, uint32 FirstIndex = 1, int32 VertexOffset = 0, uint32 FirstInstance = 0) = 0;

        // Indirect draws against the buffer bound in FGraphicsState::IndirectParams (a tracked, GPU-written
        // args buffer -- gets the IndirectArgument barrier). Offset is in bytes; stride is the arg-struct size.
        virtual void DrawIndirect(uint32 DrawCount, uint64 Offset) = 0;
        virtual void DrawIndexedIndirect(uint32 DrawCount, uint64 Offset) = 0;

        // Indirect draws against an explicit args buffer -- the path for transient (ring-allocated) args.
        // No IndirectParams binding and no state transition: a host-visible ring chunk is made visible to
        // VK_ACCESS_INDIRECT_COMMAND_READ by the queue-submit host-write guarantee, same as transient vertex
        // pulling. Pass a ring chunk via the FTransientAlloc helper below.
        virtual void DrawIndirect(FRHIBuffer* ArgsBuffer, uint64 Offset, uint32 DrawCount, uint32 Stride) = 0;
        virtual void DrawIndexedIndirect(FRHIBuffer* ArgsBuffer, uint64 Offset, uint32 DrawCount, uint32 Stride) = 0;

        // Draw FDrawIndirectArguments / FDrawIndexedIndirectArguments built straight into the transient ring
        // (e.g. via AllocTransient<FDrawIndirectArguments>(DrawCount)). Stride is the arg-struct size.
        void DrawIndirect(const FTransientAlloc& Args, uint32 DrawCount)
        {
            DrawIndirect(Args.Buffer, Args.Offset, DrawCount, sizeof(FDrawIndirectArguments));
        }
        void DrawIndexedIndirect(const FTransientAlloc& Args, uint32 DrawCount)
        {
            DrawIndexedIndirect(Args.Buffer, Args.Offset, DrawCount, sizeof(FDrawIndexedIndirectArguments));
        }

        virtual void SetComputeState(const FComputeState& State) = 0;
        virtual void Dispatch(uint32 GroupCountX, uint32 GroupCountY, uint32 GroupCountZ) = 0;

        NODISCARD virtual const FCommandListInfo& GetCommandListInfo() const = 0;
        NODISCARD virtual FPendingCommandState& GetPendingCommandState() = 0;
        NODISCARD virtual const FCommandListStatTracker& GetCommandListStats() const = 0;
    };
}