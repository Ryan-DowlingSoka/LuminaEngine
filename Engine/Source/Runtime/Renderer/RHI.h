#pragma once

#include "Format.h"
#include "Containers/Array.h"
#include "Containers/SegmentArray.h"
#include "Containers/Tuple.h"
#include "Platform/GenericPlatform.h"

namespace Lumina::RHI
{
    constexpr auto kDefaultAlign = 16;
    
    struct FTexture;
    struct FTextureHeap;
    struct FPipeline;
    struct FSemaphore;
    struct FCommandList;
    
    using GPUPtr        = uint64;
    using FPipelineH    = THandle<FPipeline>;
    using FTextureH     = THandle<FTexture>;
    using FTextureHeapH = THandle<FTextureHeap>;
    using FSemaphoreH   = THandle<FSemaphore>;
    using FCmdListH     = THandle<FCommandList>;
    using FDevice       = struct FDeviceImpl*;
    using FQueue        = struct FQueueImpl*;
    
    struct FDispatchTable
    {
        
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
    
    enum class ETextureType : uint8
    {
        Tex1D,
        Tex2D,
        Tex3D,
        TexCube,
        Tex2DArray,
        TexCubeArray,
    };
    
    enum class EImageUsageFlags : uint16
    {
        None                = 0,
        Sampled             = BIT(1),
        Storage             = BIT(2),
        ColorAttachment     = BIT(3),
        DepthAttachment     = BIT(4),
        TransferSrc         = BIT(5),
        TransferDst         = BIT(6),
    };
    
    ENUM_CLASS_FLAGS(EImageUsageFlags);
    
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
    
    struct FTextureDesc
    {
        ETextureType Type = ETextureType::Tex2D;
        FUIntVector3 Dimension;
        uint32 MipCount = 1;
        uint32 LayerCount = 1;
        uint32 SampleCount = 1;
        EFormat Format;
        EImageUsageFlags Usage = EImageUsageFlags::None;
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
    void        Free(FSemaphoreH Handle);
    
    FPipelineH  CreateGraphicsPipeline(const FShaderSource& Vertex, const FShaderSource& Fragment, const FRasterDesc& Desc);
    FPipelineH  CreateComputePipeline(const FShaderSource& Compute);
    FSemaphoreH CreateSemaphore(uint64 Value);
    
    FTextureH       CreateTexture(const FTextureDesc& Desc, GPUPtr Location = 0);
    FTextureHeapH   CreateTextureHeap(uint32 TextureCount, uint32 RWTextureCount, uint32 SamplerCount);
    
    FCmdListH   OpenCommandList(EQueueType Type);
    void        CloseCommandList(FCmdListH CL);
    void        Submit(TSpan<FCmdListH> CommandLists);
                
    void        CmdMemcpy(FCmdListH CL, GPUPtr Dest, GPUPtr Source);
    
    void        CmdBarrier(FCmdListH CL, EStageFlags Before, EStageFlags After);
    
    void        CmdBeginRenderPass(FCmdListH CL, const FRenderPassDesc& Desc);
    void        CmdEndRenderPass(FCmdListH CL);
    
    void        CmdSetFrontFace(FCmdListH CL, EFrontFace Front);
    void        CmdSetCullMode(FCmdListH CL, ECullMode Mode);
    
    void        CmdSetPipeline(FCmdListH CL, FPipelineH Pipeline);
    
    void        CmdSetScissor(FCmdListH CL, const FRect& Rect);
    void        CmdSetViewport(FCmdListH CL, const FRect& Rect);
                
    void        CmdDispatch(FCmdListH CL, GPUPtr DrawArgs, uint32 GroupX, uint32 GroupY, uint32 GroupZ);
                
    void        CmdDraw(FCmdListH CL, GPUPtr DrawArgs, uint32 VertexCount, uint32 InstanceCount, uint32 FirstVertex, uint32 FirstInstance);
    void        CmdDrawIndexed(FCmdListH CL, GPUPtr DrawArgs, uint32 IndexCount, uint32 InstanceCount, uint32 FirstIndex, uint32 VertexOffset, uint32 FirstInstance);
    void        CmdDrawIndirect(FCmdListH CL, GPUPtr DrawArgs, uint32 Offset, uint32 DrawCount, uint32 Stride);
    void        CmdDrawIndexedIndirect(FCmdListH CL, GPUPtr DrawArgs, uint32 Offset, uint32 DrawCount, uint32 Stride);
    
    
    template<typename T>
    TTuple<T*, GPUPtr> New(uint64 Count = 1)
    {
        GPUPtr GPU = Malloc(Count * sizeof(T), alignof(T));
        T* Host = static_cast<T*>(ToHost(GPU));
        std::uninitialized_value_construct_n(Host, Count);
        return {Host, GPU};
    }
    
}