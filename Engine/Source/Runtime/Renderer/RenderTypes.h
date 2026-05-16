#pragma once

#include "Format.h"
#include "RHIFwd.h"
#include "Core/LuminaMacros.h"
#include "Core/Math/Hash/Hash.h"

#define FRAMES_IN_FLIGHT 3
#define SWAPCHAIN_IMAGES 3

namespace Lumina
{
    struct FVertex;
    struct FVertexAttribute;
    

    enum class ERHIAccess : uint32
    {
        None = 0,
        Read = 1 << 0,
        Write = 1 << 1,
        TransferRead = 1 << 2,
        TransferWrite = 1 << 3,
        ShaderRead = 1 << 4,
        ShaderWrite = 1 << 5,
        ColorAttachmentWrite = 1 << 6,
        DepthStencilAttachmentWrite = 1 << 7,
        PresentRead = 1 << 8,
        HostRead = 1 << 9,
        HostWrite = 1 << 10,
        ComputeRead = 1 << 11,
        ComputeWrite = 1 << 12,

        General = HostRead,

        All = Read | Write | TransferRead | TransferWrite | ShaderRead | ShaderWrite | ColorAttachmentWrite |
          DepthStencilAttachmentWrite | PresentRead | HostRead | HostWrite | ComputeRead | ComputeWrite
    };

    enum class EResourceStates : uint32
    {
        Unknown                     = 0,
        Common                      = 0x00000001,
        ConstantBuffer              = 0x00000002,
        VertexBuffer                = 0x00000004,
        IndexBuffer                 = 0x00000008,
        IndirectArgument            = 0x00000010,
        ShaderResource              = 0x00000020,
        UnorderedAccess             = 0x00000040,
        RenderTarget                = 0x00000080,
        DepthWrite                  = 0x00000100,
        DepthRead                   = 0x00000200,
        StreamOut                   = 0x00000400,
        CopyDest                    = 0x00000800,
        CopySource                  = 0x00001000,
        ResolveDest                 = 0x00002000,
        ResolveSource               = 0x00004000,
        Present                     = 0x00008000,
        AccelStructRead             = 0x00010000,
        AccelStructWrite            = 0x00020000,
        AccelStructBuildInput       = 0x00040000,
        AccelStructBuildBlas        = 0x00080000,
        ShadingRateSurface          = 0x00100000,
        OpacityMicromapWrite        = 0x00200000,
        OpacityMicromapBuildInput   = 0x00400000,
    };

    ENUM_CLASS_FLAGS(EResourceStates)

    enum class ERHIPipeline : uint8
    {
        Graphics = 1 << 0,
        AsyncCompute = 1 << 1,

        None = 0,
        All = Graphics | AsyncCompute,
        Num = 2
    };

    enum class ECommandQueue : uint8
    {
        Graphics    = 0,
        Compute     = 1,
        Transfer    = 2,
        
        Num,
    };

    #define Q_Graphics ECommandQueue::Graphics
    #define Q_Compute  ECommandQueue::Compute
    #define Q_Transfer ECommandQueue::Transfer

    

    enum class EBufferUsageFlags : uint32
    {
        None = 0,
        NullResource,
        Dynamic,
        SourceCopy,
        VertexBuffer,
        IndexBuffer,
        UniformBuffer,
        IndirectBuffer,
        StorageBuffer,
        StagingBuffer,
        CPUWritable,
        CPUReadable,
        Transient,
    };
    
    ENUM_CLASS_FLAGS(EBufferUsageFlags);

    #define BUF_NullResource    EBufferUsageFlags::NullResource
    #define BUF_Dynamic         EBufferUsageFlags::Dynamic
    #define BUF_SourceCopy      EBufferUsageFlags::SourceCopy
    #define BUF_VertexBuffer    EBufferUsageFlags::VertexBuffer
    #define BUF_IndexBuffer     EBufferUsageFlags::IndexBuffer
    #define BUF_UniformBuffer   EBufferUsageFlags::UniformBuffer
    #define BUF_StorageBuffer   EBufferUsageFlags::StorageBuffer
    #define BUF_Indirect        EBufferUsageFlags::IndirectBuffer
    
    enum class ERenderDeviceBufferMemoryUsage : uint8
    {
        None              = 0,
        GPUOnly           = 1 << 0,
        CPUToGPU          = 1 << 1,
        GPUToCPU          = 1 << 2,
        Transient         = 1 << 3,
    };

    enum class ERenderLoadOp : uint8
    {
        Clear,
        Load,
    };

    enum class ERenderStoreOp : uint8
    {
        Store,
        DontCare,
    };

    struct FViewport
    {
        float MinX, MaxX;
        float MinY, MaxY;
        float MinZ, MaxZ;

        FViewport() : MinX(0.f), MaxX(0.f), MinY(0.f), MaxY(0.f), MinZ(0.f), MaxZ(1.f) { }

        FViewport(float width, float height) : MinX(0.0f), MaxX(width), MinY(0.0f), MaxY(height), MinZ(0.0f), MaxZ(1.0f) { }

        FViewport(float _minX, float _maxX, float _minY, float _maxY, float _minZ, float _maxZ)
            : MinX(_minX), MaxX(_maxX), MinY(_minY), MaxY(_maxY), MinZ(_minZ), MaxZ(_maxZ)
        { }

        bool operator ==(const FViewport& b) const
        {
            return MinX == b.MinX
                && MinY == b.MinY
                && MinZ == b.MinZ
                && MaxX == b.MaxX
                && MaxY == b.MaxY
                && MaxZ == b.MaxZ;
        }
        bool operator !=(const FViewport& b) const { return !(*this == b); }

        NODISCARD float Width() const { return MaxX - MinX; }
        NODISCARD float Height() const { return MaxY - MinY; }
    };

    

    struct FRect
    {
        int MinX, MaxX;
        int MinY, MaxY;

        FRect() : MinX(0), MaxX(0), MinY(0), MaxY(0) { }
        FRect(int width, int height) : MinX(0), MaxX(width), MinY(0), MaxY(height) { }
        FRect(int _minX, int _maxX, int _minY, int _maxY) : MinX(_minX), MaxX(_maxX), MinY(_minY), MaxY(_maxY) { }
        explicit FRect(const FViewport& viewport)
            : MinX(int(floorf(viewport.MinX)))
            , MaxX(int(ceilf(viewport.MaxX)))
            , MinY(int(floorf(viewport.MinY)))
            , MaxY(int(ceilf(viewport.MaxY)))
        {
        }

        bool operator ==(const FRect& b) const {
            return MinX == b.MinX && MinY == b.MinY && MaxX == b.MaxX && MaxY == b.MaxY;
        }
        bool operator !=(const FRect& b) const { return !(*this == b); }

        NODISCARD int Width() const { return MaxX - MinX; }
        NODISCARD int Height() const { return MaxY - MinY; }
    };
    

    enum class EImageDimension : uint8
    {
        Unknown,
        Texture2D,
        Texture2DArray,
        Texture3D,
        TextureCube,
        TextureCubeArray
    };

    enum class EImageCreateFlags : uint32
    {
        None = 0,
        Presentable = 1,
        RenderTarget = 2,
        ShaderResource = 3,
        Storage = 4,
        InputAttachment = 5,
        DepthStencil = 6,
        UnorderedAccess = 7,
        GenerateMipMaps = 8,
        CubeCompatible = 9,
        Aliasable = 10,
        MultiSampled = 11,

        ColorAttachment = RenderTarget,
        DepthAttachment = DepthStencil,
    };

    enum class EImageSampleCount : uint8
    {
        ONE,
        FOUR,
    };
    
    enum class EPipelineType : uint8
    {
        Graphics,
        Compute,
        RayTracing
    };

    enum class EPipelineStage : uint8
    {
        TopOfPipe,
        BottomOfPipe,
        VertexInput,
        VertexShader,
        TessellationControlShader,
        TessellationEvaluationShader,
        GeometryShader,
        FragmentShader,
        ColorAttachmentOutput,
        ComputeShader,
        Transfer,
        EarlyFragmentTests,
        LateFragmentTests,
        Host,
        AllGraphics,
        AllCommands
    };


    enum class EPipelineCullingMode : uint8
    {
        BACK,
        FRONT,
        NONE
    };

    enum class EPipelineFrontFace : uint8
    {
        CLOCKWISE,
        COUNTER_CLOCKWISE
    };

    enum class EPipelineTopology : uint8
    {
        TRIANGLES,
        LINES,
        POINTS
    };

    enum class EPipelineBlending : uint8
    {
        BLEND_FACTOR_ZERO = 0,
        BLEND_FACTOR_ONE = 1,
    };

    enum class EPipelineFillMode : uint8
    {
        FILL,
        EDGE_ONLY
    };

    enum class EShaderDataType : uint8
    {
        INT,
        INT2,
        INT3,
        INT4,

        FLOAT,
        FLOAT2,
        FLOAT3,
        FLOAT4,

        IMAT3,
        IMAT4,

        MAT3,
        MAT4
    };

    constexpr uint32 DeviceDataTypeSize(const EShaderDataType& Type)
    {
        switch (Type)
        {
            case EShaderDataType::INT:           return sizeof(int);
            case EShaderDataType::INT2:          return sizeof(int) * 2;
            case EShaderDataType::INT3:          return sizeof(int) * 3;
            case EShaderDataType::INT4:          return sizeof(int) * 4;
            case EShaderDataType::FLOAT:         return sizeof(float);
            case EShaderDataType::FLOAT2:        return sizeof(float) * 2;
            case EShaderDataType::FLOAT3:        return sizeof(float) * 3;
            case EShaderDataType::FLOAT4:        return sizeof(float) * 4;
            case EShaderDataType::IMAT3:         return sizeof(int) * 3 * 3;
            case EShaderDataType::IMAT4:         return sizeof(int) * 4 * 4;
            case EShaderDataType::MAT3:          return sizeof(float) * 3 * 3;
            case EShaderDataType::MAT4:          return sizeof(float) * 4 * 4;
            default:                             std::unreachable();
        }
    }
    
    struct FVertexBufferBinding
    {
        FRHIBuffer* Buffer = nullptr;
        uint32 Slot = 0;
        uint32 Offset = 0;

        bool operator ==(const FVertexBufferBinding& b) const
        {
            return Buffer == b.Buffer
                && Slot == b.Slot
                && Offset == b.Offset;
        }
        bool operator !=(const FVertexBufferBinding& b) const { return !(*this == b); }

        FVertexBufferBinding& SetBuffer(FRHIBuffer* value) { Buffer = value; return *this; }
        FVertexBufferBinding& SetSlot(uint32 value) { Slot = value; return *this; }
        FVertexBufferBinding& SetOffset(uint32 value) { Offset = value; return *this; }
    };

    static_assert(std::is_trivially_copyable_v<FVertexBufferBinding>);

    struct FIndexBufferBinding
    {
        FRHIBuffer* Buffer = nullptr;
        EFormat Format = EFormat::R32_UINT;
        uint32 Offset = 0;

        bool operator ==(const FIndexBufferBinding& b) const
        {
            return Buffer == b.Buffer
                && Format == b.Format
                && Offset == b.Offset;
        }
        bool operator !=(const FIndexBufferBinding& b) const { return !(*this == b); }

        FIndexBufferBinding& SetBuffer(FRHIBuffer* value) { Buffer = value; return *this; }
        FIndexBufferBinding& SetFormat(EFormat value) { Format = value; return *this; }
        FIndexBufferBinding& SetOffset(uint32 value) { Offset = value; return *this; }
    };

    static_assert(std::is_trivially_copyable_v<FIndexBufferBinding>);

    struct alignas(16) FViewportState
    {
        FViewportState() = default;
        
        FViewportState(const FViewport& InViewport, const FRect& Scissor)
        {
            Viewports.emplace_back(InViewport);
            Scissors.emplace_back(Scissor);
        }
        
        void SetViewport(const FViewport& Viewport)
        {
            Viewports.empty() ? Viewports.emplace_back(Viewport) : Viewports[0] = Viewport;
        }
        
        void SetScissorRect(const FRect& Rect)
        {
            Scissors.empty() ? Scissors.emplace_back(Rect) : Scissors[0] = Rect;
        }
        
        TFixedVector<FViewport, 1>   Viewports;
        TFixedVector<FRect, 1>       Scissors;
    };
    
}
