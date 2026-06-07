#pragma once

#include "Format.h"
#include "Containers/Array.h"
#include "Containers/Tuple.h"
#include "Platform/GenericPlatform.h"

namespace Lumina::RHI
{
    constexpr auto kDefaultAlign = 16;
    
    template<typename T>
    struct THandle
    {
        uint64 Handle;
    };
    
    struct FTexture;
    struct FPipeline;
    struct FSemaphore;
    struct FTextureHeap;
    
    using GPUPtr        = uint64;
    using FPipelineH    = THandle<FPipeline>;
    using FTextureH     = THandle<FTexture>;
    using FSemaphoreH   = THandle<FSemaphore>;
    using FTextureHeapH = THandle<FTextureHeap>;
    using FDevice       = struct FDeviceImpl*;
    using FCmdList      = struct FCommandListImpl*;
    using FQueue        = struct FQueueImpl*;
    
    
    template<typename T>
    class TGenerationMap
    {
        using HandleT       = THandle<T>;
        using FDtorFn       = void(*)(T*);
    
    public:
        
        TGenerationMap() = default;
        TGenerationMap(FDtorFn Fn): DtorFn(Fn) {}
    
        template<typename... TArgs>
        HandleT Emplace(TArgs&&... Value)
        {
            if (Head == kEndOfList)
            {
                AddSegment();
            }
            
            uint32 Index = Head;
            
            FEntry* Entry = Get(Index);
            Head = Entry->Next;
            Entry->Next = kNotInFreeList;
            
            ::new(&Entry->Data) T(eastl::forward<TArgs>(Value)...);
            
            return ToHandle(Index, ++Entry->Gen);
        }
        
        void Erase(HandleT Handle)
        {
            auto&& [I, G] = FromHandle(Handle);
            FEntry* Entry = Get(I);
            DtorFn(&Entry->Data);
            
            Entry->Next = Head;
            Head = I;
        }
        
        void Clear()
        {
            for (uint32 SegmentIndex = 0; SegmentIndex < UsedSegments; ++SegmentIndex)
            {
                uint32 SegmentSize = SlotsInSegments(SegmentIndex);
                FEntry* Segment = Segments[SegmentIndex];
                
                for (uint32 Index = 0; Index < SegmentSize; ++Index)
                {
                    if (Segment[Index].Next == kNotInFreeList)
                    {
                        DtorFn(&Segment[Index].Data);
                    }
                }
                
                Memory::Free(Segment);
                Segments[SegmentIndex] = nullptr;
            }
            
            UsedSegments = 0;
        }
        
        T& operator[](HandleT Handle)
        {
            auto&& [I, G] = FromHandle(Handle);
            FEntry* Entry = Get(I);
            return Entry->Data;
        }
        
        const T& operator[](HandleT Handle) const
        {
            auto&& [I, G] = FromHandle(Handle);
            FEntry* Entry = Get(I);
            return Entry->Data;
        }
        
        
    private:
        
        static constexpr auto kSmallSegmentsToSkip = 6;
        static constexpr auto kNotInFreeList = UINT32_MAX;
        static constexpr auto kEndOfList = kNotInFreeList - 1;
        
        struct FEntry
        {
            T Data;
            uint32 Next;
            uint32 Gen;
        };
        
        struct FDecomposedHandle
        {
            uint32 Index;
            uint32 Gen;
        };
        
        static constexpr uint32 SlotsInSegments(uint32 SegmentIndex)
        {
            return (1 << kSmallSegmentsToSkip) << SegmentIndex;
        }
        
        static constexpr uint32 CapacityForSegmentCount(uint32 SegmentCount)
        {
            return ((1 < kSmallSegmentsToSkip) << SegmentCount) - (1 << kSmallSegmentsToSkip);
        }
        
        void AddSegment()
        {
            uint64 SegmentSize = SlotsInSegments(UsedSegments);
            auto Entry = Memory::Malloc(sizeof(FEntry) * SegmentSize);
            auto Segment = static_cast<FEntry*>(Entry);
            
            Segment[UsedSegments++] = Segment;
            
            uint32 SegmentOffset = CapacityForSegmentCount(UsedSegments - 1);
            for (uint64 i = SegmentSize; i > 0; --i)
            {
                Segment[i - 1].Gen = 0;
                Segment[i - 1].Next = Head;
                Head = i + SegmentOffset;
            }
        }
        
        FEntry* Get(uint32 Index)
        {
            uint64 Segment = 63 - std::countl_zero(Index);
            uint32 Slot = Index - CapacityForSegmentCount(Segment);
            
            return &Segments[Segment][Slot];
        }
        
        static constexpr HandleT ToHandle(uint32 Index, uint32 Generation)
        {
            return {.Handle = (0x8000'0000'0000'0000 | (uint64)Generation) << 32ull | Index};
        }
        
        static constexpr FDecomposedHandle FromHandle(HandleT Handle)
        {
            return 
            {
                .Index  = static_cast<uint32>(Handle.Handle & 0xFFFF'FFFFull),
                .Gen    = static_cast<uint32>((Handle.Handle >> 32) & 0x7FFF'FFFFull)
            };
        }
        
    private:
        
        FDtorFn     DtorFn;
        uint32      UsedSegments = 0;
        uint32      Head = kEndOfList;
        FEntry*     Segments[26]{};
    };
    
    
    enum class EBackend : uint8
    {
        Vulkan,
    };
    
    enum class EMemoryType : uint8
    {
        CPUWrite,
        CPURead,
        GPUOnly,
        
        Default = CPUWrite
    };
    
    enum class EQueueType : uint8
    {
        Graphics,
        Transfer,
        Compute,
        
        Default = Graphics
    };
    
    enum class EStageFlags : uint16 
    {
        None              = 0,
        IndirectArguments = BIT(1),
        Transfer          = BIT(2),
        Compute           = BIT(3),
        RasterColorOut    = BIT(4),
        PixelShader       = BIT(5),
        FragmentTests     = BIT(6),
        VertexShader      = BIT(7),
        Host              = BIT(8),
    };
    
    enum class EFrontFace : uint8
    {
        CCW,
        CW,
    };
    
    enum class ECullMode : uint8
    {
        Front,
        Back,
        None,
    };
    
    enum class ELoadOp : uint8
    {
        Undefined,
        Load,
        Clear,
    };
    
    enum class EStoreOp : uint8
    {
        Undefined,
        Store,
        Discard,
    };
    
    enum class ETopology : uint8
    {
        TriangleList,
        TriangleStrip,
    };
    
    enum class EBlend : uint8
    {
        Add,
        Subtract,
        RevSubtract,
        Min,
        Max
    };
    
    enum class EFactor : uint8
    {
        Zero,
        One,
        SrcColor,
        DstColor,
        SrcAlpha,
        OneMinusSrcAlpha,
    };
    
    struct FShaderSource
    {
        TSpan<std::byte>    Source;
        FStringView         EntryPoint;
    };
    
    struct FRect
    {
        int MinX, MaxX;
        int MinY, MaxY;
    };
    
    struct FDrawIndirectArguments
    {
        
    };
    
    struct FDrawIndexedIndirectArguments
    {
        
    };
    
    struct FBlendDesc
    {
        EBlend  ColorOp         = EBlend::Add;
        EFactor SrcColorFactor  = EFactor::One;
        EFactor DstColorFactor  = EFactor::Zero;
        EBlend  AlphaOp         = EBlend::Add;
        EFactor SrcAlphaFactor  = EFactor::One;
        EFactor DstAlphaFactor  = EFactor::Zero;
        uint8   ColorWriteMask  = 0xF;
    };
    
    struct FColorTarget
    {
        EFormat Format;
        
    };
    
    struct FRasterDesc
    {
        ETopology   Topology = ETopology::TriangleList;
        bool        bAlphaToCoverage = false;
        uint8       SampleCount = 1;
        EFormat     DepthFormat;
        EFormat     StencilFormat;
        TSpan<const FColorTarget> ColorTargets;
    };
    
    struct FRenderAttachment
    {
        THandle<FTexture>   Texture;
        ELoadOp             LoadOp;
        EStoreOp            StoreOp;
        float               Color[4];
    };
    
    struct FRenderPassDesc
    {
        TSpan<const FRenderAttachment>  ColorAttachments;
        FRenderAttachment               DepthAttachment;
        FRenderAttachment               StencilAttachment;
        FUIntVector2                    RenderArea;
    };
    
    void        CreateDevice();
    void        FreeDevice();
    void        WaitDeviceIdle();
    void        WaitSemaphore(FSemaphoreH Semaphore, uint64 Value);
    
    
    GPUPtr      Malloc(uint64 Size, uint64 Alignment = kDefaultAlign, EMemoryType Type = EMemoryType::Default);
    GPUPtr      Malloc(uint64 Size, EMemoryType Type);
    void*       ToHost(GPUPtr GPU);
    void        Free(GPUPtr GPU);
    
    FPipelineH  CreateGraphicsPipeline(const FShaderSource& Vertex, const FShaderSource& Fragment, const FRasterDesc& Desc);
    FPipelineH  CreateComputePipeline(const FShaderSource& Compute);
    FSemaphoreH CreateSemaphore(uint64 Value);
    FTextureHeapH CreateTextureHeap(uint32 TextureCount, uint32 RWTextureCount, uint32 SamplerCount);
    
    FCmdList    OpenCommandList(EQueueType Type);
    void        CloseCommandList(FCmdList CL);
    void        Submit(TSpan<FCmdList> CommandLists);
                
    void        CmdMemcpy(FCmdList CL, GPUPtr Dest, GPUPtr Source);
    
    void        CmdBarrier(FCmdList CL, EStageFlags Before, EStageFlags After);
    
    void        CmdBeginRenderPass(FCmdList CL, const FRenderPassDesc& Desc);
    void        CmdEndRenderPass(FCmdList CL);
    
    void        CmdSetFrontFace(FCmdList CL, EFrontFace Front);
    void        CmdSetCullMode(FCmdList CL, ECullMode Mode);
    
    void        CmdSetPipeline(FCmdList CL, FPipelineH Pipeline);
    
    void        CmdSetScissor(FCmdList CL, const FRect& Rect);
    void        CmdSetViewport(FCmdList CL, const FRect& Rect);
                
    void        CmdDispatch(FCmdList CL, GPUPtr DrawArgs, uint32 GroupX, uint32 GroupY, uint32 GroupZ);
                
    void        CmdDraw(FCmdList CL, GPUPtr DrawArgs, uint32 VertexCount, uint32 InstanceCount, uint32 FirstVertex, uint32 FirstInstance);
    void        CmdDrawIndexed(FCmdList CL, GPUPtr DrawArgs, uint32 IndexCount, uint32 InstanceCount, uint32 FirstIndex, uint32 VertexOffset, uint32 FirstInstance);
    void        CmdDrawIndirect(FCmdList CL, GPUPtr DrawArgs, uint32 Offset, uint32 DrawCount, uint32 Stride);
    void        CmdDrawIndexedIndirect(FCmdList CL, GPUPtr DrawArgs, uint32 Offset, uint32 DrawCount, uint32 Stride);
    
    
    template<typename T>
    TTuple<T*, GPUPtr> New(uint64 Count = 1)
    {
        GPUPtr GPU = Malloc(Count * sizeof(T), alignof(T));
        T* Host = static_cast<T*>(ToHost(GPU));
        std::uninitialized_value_construct_n(Host, Count);
        return {Host, GPU};
    }
    
}