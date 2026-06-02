#include "pch.h"
#include "RenderResource.h"

#include "RenderContext.h"
#include "Containers/Array.h"


namespace Lumina
{
    uint16 FRenderPassDesc::DeriveSampleCount() const noexcept
    {
        if (SampleCount > 1)
        {
            return SampleCount;
        }

        for (const FAttachment& Color : ColorAttachments)
        {
            if (Color.Image)
            {
                const uint8 N = Color.Image->GetDescription().NumSamples;
                if (N > 1) return (uint16)N;
            }
        }

        if (DepthAttachment.Image)
        {
            const uint8 N = DepthAttachment.Image->GetDescription().NumSamples;
            if (N > 1) return (uint16)N;
        }

        return 1;
    }

    template class RUNTIME_API TRefCountPtr<IRHIResource>;
    template class RUNTIME_API TRefCountPtr<IEventQuery>;
    template class RUNTIME_API TRefCountPtr<ITimerQuery>;
    template class RUNTIME_API TRefCountPtr<IPipelineStatsQuery>;
    template class RUNTIME_API TRefCountPtr<FRHIBuffer>;
    template class RUNTIME_API TRefCountPtr<FRHIImage>;
    template class RUNTIME_API TRefCountPtr<FRHISampler>;
    template class RUNTIME_API TRefCountPtr<FRHIShader>;
    template class RUNTIME_API TRefCountPtr<FRHIVertexShader>;
    template class RUNTIME_API TRefCountPtr<FRHIPixelShader>;
    template class RUNTIME_API TRefCountPtr<FRHIComputeShader>;
    template class RUNTIME_API TRefCountPtr<FRHITaskShader>;
    template class RUNTIME_API TRefCountPtr<FRHIMeshShader>;
    template class RUNTIME_API TRefCountPtr<ICommandList>;
    template class RUNTIME_API TRefCountPtr<FRHIViewport>;
    template class RUNTIME_API TRefCountPtr<FRHIGraphicsPipeline>;
    template class RUNTIME_API TRefCountPtr<FRHIComputePipeline>;
    template class RUNTIME_API TRefCountPtr<FRHIBindingLayout>;
    template class RUNTIME_API TRefCountPtr<FRHIBindingSet>;
    template class RUNTIME_API TRefCountPtr<FRHIInputLayout>;
    template class RUNTIME_API TRefCountPtr<FShaderLibrary>;
    template class RUNTIME_API TRefCountPtr<FRHIDescriptorTable>;


    void FRHIResourceList::Track(IRHIResource* Resource)
    {
        FShard& Shard = Shards[ShardIndex(Resource)];
        bool bInserted;
        {
            FScopeLock Lock(Shard.Mutex);
            bInserted = Shard.Set.insert(Resource).second;
        }
        if (bInserted)
        {
            LiveCount.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void FRHIResourceList::Untrack(IRHIResource* Resource)
    {
        FShard& Shard = Shards[ShardIndex(Resource)];
        size_t Removed;
        {
            FScopeLock Lock(Shard.Mutex);
            Removed = Shard.Set.erase(Resource);
        }
        if (Removed != 0)
        {
            LiveCount.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    void FRHIResourceList::Clear()
    {
        for (FShard& Shard : Shards)
        {
            FScopeLock Lock(Shard.Mutex);
            Shard.Set.clear();
        }
        LiveCount.store(0, std::memory_order_relaxed);
    }

    FRHIResourceList& FRHIResourceList::Get()
    {
        static FRHIResourceList Instance;
        return Instance;
    }

    IRHIResource::IRHIResource()
    {
        BeginTrackingResource(this);
    }

    IRHIResource::~IRHIResource()
    {
        EndTrackingResource(this);
    }

    void IRHIResource::BeginTrackingResource(IRHIResource* InResource)
    {
        FRHIResourceList::Get().Track(InResource);
    }

    void IRHIResource::EndTrackingResource(IRHIResource* InResource)
    {
        FRHIResourceList::Get().Untrack(InResource);
    }

    void IRHIResource::ReleaseAllRHIResources()
    {
        FRHIResourceList& List = FRHIResourceList::Get();

        TVector<IRHIResource*> ResourcesSnapshot;
        const int64 Live = List.LiveCount.load(std::memory_order_relaxed);
        if (Live > 0)
        {
            ResourcesSnapshot.reserve((size_t)Live);
        }

        for (FRHIResourceList::FShard& Shard : List.Shards)
        {
            FScopeLock Lock(Shard.Mutex);
            ResourcesSnapshot.insert(ResourcesSnapshot.end(), Shard.Set.begin(), Shard.Set.end());
        }

        // No shard lock held: each ~IRHIResource re-enters Untrack and locks its own shard.
        for (IRHIResource* Resource : ResourcesSnapshot)
        {
            Memory::Delete(Resource);
        }

        ASSERT(List.LiveCount.load(std::memory_order_relaxed) == 0);
    }

    uint32 IRHIResource::GetNumberRHIResources()
    {
        return (uint32)FRHIResourceList::Get().LiveCount.load(std::memory_order_relaxed);
    }

    const char* GetRHIResourceTypeName(ERHIResourceType Type)
    {
        switch (Type)
        {
        case RRT_SamplerState:        return "Sampler State";
        case RTT_InputLayout:         return "Input Layout";
        case RRT_BindingLayout:       return "Binding Layout";
        case RRT_BindingSet:          return "Binding Set";
        case RRT_DepthStencilState:   return "Depth Stencil State";
        case RRT_BlendState:          return "Blend State";
        case RRT_VertexShader:        return "Vertex Shader";
        case RRT_PixelShader:         return "Pixel Shader";
        case RRT_ComputeShader:       return "Compute Shader";
        case RTT_GeometryShader:      return "Geometry Shader";
        case RRT_MeshShader:          return "Mesh Shader";
        case RRT_TaskShader:          return "Task Shader";
        case RRT_ShaderLibrary:       return "Shader Library";
        case RRT_GraphicsPipeline:    return "Graphics Pipeline";
        case RRT_ComputePipeline:     return "Compute Pipeline";
        case RRT_UniformBufferLayout: return "Uniform Buffer Layout";
        case RRT_UniformBuffer:       return "Uniform Buffer";
        case RRT_Buffer:              return "Buffer";
        case RRT_Image:               return "Image";
        case RRT_GPUFence:            return "GPU Fence";
        case RRT_Viewport:            return "Viewport";
        case RRT_StagingBuffer:       return "Staging Buffer";
        case RRT_StagingImage:        return "Staging Image";
        case RRT_CommandList:         return "Command List";
        case RRT_DescriptorTable:     return "Descriptor Table";
        case RTT_EventQuery:          return "Event Query";
        case RTT_TimerQuery:          return "Timer Query";
        case RTT_PipelineStatsQuery:  return "Pipeline Stats Query";
        default:                      return "Unknown";
        }
    }

    const char* GetGPUMemoryCategoryName(EGPUMemoryCategory Category)
    {
        switch (Category)
        {
        case EGPUMemoryCategory::RenderTarget:   return "Render Targets";
        case EGPUMemoryCategory::DepthStencil:   return "Depth / Stencil";
        case EGPUMemoryCategory::ShadowMap:      return "Shadow Maps";
        case EGPUMemoryCategory::Texture:        return "Textures";
        case EGPUMemoryCategory::Cubemap:        return "Cubemaps";
        case EGPUMemoryCategory::VolumeTexture:  return "Volume Textures";
        case EGPUMemoryCategory::VertexBuffer:   return "Vertex Buffers";
        case EGPUMemoryCategory::IndexBuffer:    return "Index Buffers";
        case EGPUMemoryCategory::UniformBuffer:  return "Uniform Buffers";
        case EGPUMemoryCategory::StorageBuffer:  return "Storage Buffers";
        case EGPUMemoryCategory::Staging:        return "Staging / Upload";
        case EGPUMemoryCategory::Other:          return "Other";
        default:                                 return "Unknown";
        }
    }

    uint64 EstimateImageMemory(const FRHIImageDesc& Desc)
    {
        const FFormatInfo& Info = RHI::Format::Info(Desc.Format);
        if (Info.BytesPerBlock == 0)
        {
            return 0;
        }

        const uint32 Block       = (Info.BlockSize > 0) ? Info.BlockSize : 1;
        const uint16 ArraySlices = (Desc.ArraySize > 0) ? Desc.ArraySize : 1;
        const uint8  Samples     = (Desc.NumSamples > 0) ? Desc.NumSamples : 1;
        const uint8  Mips        = (Desc.NumMips > 0) ? Desc.NumMips : 1;

        uint64 Bytes = 0;
        for (uint8 Mip = 0; Mip < Mips; ++Mip)
        {
            const uint32 W = (Desc.Extent.x >> Mip) > 0 ? (Desc.Extent.x >> Mip) : 1u;
            const uint32 H = (Desc.Extent.y >> Mip) > 0 ? (Desc.Extent.y >> Mip) : 1u;
            const uint32 D = ((uint32)Desc.Depth >> Mip) > 0 ? ((uint32)Desc.Depth >> Mip) : 1u;

            const uint64 BlocksX = (W + Block - 1) / Block;
            const uint64 BlocksY = (H + Block - 1) / Block;

            Bytes += BlocksX * BlocksY * D * Info.BytesPerBlock;
        }

        return Bytes * ArraySlices * Samples;
    }

    static EGPUMemoryCategory ClassifyImage(const FRHIImageDesc& Desc)
    {
        if (Desc.Flags.IsFlagSet(EImageCreateFlags::DepthStencil))
        {
            // Shadow atlases are depth targets too -- split them out so they read as their own line.
            if (Desc.DebugName.find("Shadow") != FString::npos || Desc.DebugName.find("Cascade") != FString::npos)
            {
                return EGPUMemoryCategory::ShadowMap;
            }
            return EGPUMemoryCategory::DepthStencil;
        }

        if (Desc.Flags.IsFlagSet(EImageCreateFlags::RenderTarget))
        {
            return EGPUMemoryCategory::RenderTarget;
        }

        switch (Desc.Dimension)
        {
        case EImageDimension::TextureCube:
        case EImageDimension::TextureCubeArray: return EGPUMemoryCategory::Cubemap;
        case EImageDimension::Texture3D:        return EGPUMemoryCategory::VolumeTexture;
        default:                                return EGPUMemoryCategory::Texture;
        }
    }

    static EGPUMemoryCategory ClassifyBuffer(const FRHIBuffer* Buffer)
    {
        const TBitFlags<EBufferUsageFlags>& Usage = Buffer->GetUsage();
        if (Usage.IsFlagSet(EBufferUsageFlags::StagingBuffer) || Usage.IsFlagSet(EBufferUsageFlags::CPUReadable) || Usage.IsFlagSet(EBufferUsageFlags::CPUWritable))
        {
            return EGPUMemoryCategory::Staging;
        }
        if (Usage.IsFlagSet(EBufferUsageFlags::VertexBuffer))  { return EGPUMemoryCategory::VertexBuffer; }
        if (Usage.IsFlagSet(EBufferUsageFlags::IndexBuffer))   { return EGPUMemoryCategory::IndexBuffer; }
        if (Usage.IsFlagSet(EBufferUsageFlags::UniformBuffer)) { return EGPUMemoryCategory::UniformBuffer; }
        if (Usage.IsFlagSet(EBufferUsageFlags::StorageBuffer)) { return EGPUMemoryCategory::StorageBuffer; }
        return EGPUMemoryCategory::Other;
    }

    void GatherGPUMemoryByCategory(FGPUMemoryCategoryUsage* Out, uint32 Count)
    {
        if (Out == nullptr || Count < (uint32)EGPUMemoryCategory::Count)
        {
            return;
        }

        for (uint32 i = 0; i < Count; ++i)
        {
            Out[i] = FGPUMemoryCategoryUsage{};
        }

        FRHIResourceList::ForEach([&](IRHIResource* Resource)
        {
            switch (Resource->GetResourceType())
            {
            case RRT_Image:
            {
                const FRHIImageDesc& Desc = static_cast<FRHIImage*>(Resource)->GetDescription();
                const EGPUMemoryCategory Cat = ClassifyImage(Desc);
                Out[(int)Cat].Bytes += EstimateImageMemory(Desc);
                Out[(int)Cat].Count++;
                break;
            }
            case RRT_StagingImage:
            {
                const FRHIImageDesc& Desc = static_cast<FRHIStagingImage*>(Resource)->GetDesc();
                Out[(int)EGPUMemoryCategory::Staging].Bytes += EstimateImageMemory(Desc);
                Out[(int)EGPUMemoryCategory::Staging].Count++;
                break;
            }
            case RRT_Buffer:
            {
                FRHIBuffer* Buffer = static_cast<FRHIBuffer*>(Resource);
                const EGPUMemoryCategory Cat = ClassifyBuffer(Buffer);
                Out[(int)Cat].Bytes += Buffer->GetSize();
                Out[(int)Cat].Count++;
                break;
            }
            default:
                break;
            }
        });
    }

    void FRHIViewport::SetSize(const FUIntVector2& InSize)
    {
        if (Size == InSize)
        {
            return;
        }
        
        CreateRenderTarget(InSize);
    }

    void FRHIViewport::CreateRenderTarget(const FUIntVector2& InSize)
    {
        FRHIImageDesc Desc;
        Desc.Format = EFormat::RGBA8_UNORM;
        Desc.Flags.SetMultipleFlags(EImageCreateFlags::RenderTarget, EImageCreateFlags::ShaderResource);
        Desc.Extent = InSize;
        Desc.InitialState = EResourceStates::ShaderResource;
        Desc.bKeepInitialState = true;
        Desc.DebugName = DebugName;

        RenderTarget = RenderContext->CreateImage(Desc);
    }

    FTextureSlice FTextureSlice::Resolve(const FRHIImageDesc& Desc) const
    {
        FTextureSlice Ret(*this);

        ASSERT(MipLevel < Desc.NumMips);

        if (X == static_cast<uint32>(0))
        {
            Ret.X = Math::Max((uint32)Desc.Extent.x >> MipLevel, 1u);
        }

        if (Y == static_cast<uint32>(0))
        {
            Ret.Y = Math::Max((uint32)Desc.Extent.y >> MipLevel, 1u);
        }

        if (Z == static_cast<uint32>(0))
        {
            if (Desc.Dimension == EImageDimension::Texture3D)
            {
                Ret.Z = Math::Max((uint32)Desc.Depth >> MipLevel, 1u);
            }
            else
            {
                Ret.Z = 1;
            }
        }

        return Ret;
    }

    FTextureSubresourceSet FTextureSubresourceSet::Resolve(const FRHIImageDesc& Desc, bool bSingleMipLevel) const
    {
        FTextureSubresourceSet Subresource;
        Subresource.BaseMipLevel = BaseMipLevel;

        if (bSingleMipLevel)
        {
            Subresource.NumMipLevels = 1;
        }
        else
        {
            uint32 LastMipLevelPlusOne = std::min<uint32>(BaseMipLevel + NumMipLevels, Desc.NumMips);
            Subresource.NumMipLevels = std::max<uint32>(0u, LastMipLevelPlusOne - BaseMipLevel);
        }

        switch (Desc.Dimension)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case EImageDimension::Unknown:
        case EImageDimension::Texture2DArray:
        case EImageDimension::Texture3D:
        case EImageDimension::TextureCube:
        case EImageDimension::TextureCubeArray:
            {
                Subresource.BaseArraySlice = BaseArraySlice;
                uint32 LastArraySlicePlusOne = std::min<uint32>(BaseArraySlice + NumArraySlices, Desc.ArraySize);
                Subresource.NumArraySlices = std::max<uint32>(0u, LastArraySlicePlusOne - BaseArraySlice);
                break;
            }
        default:
            
            Subresource.BaseArraySlice = 0;
            Subresource.NumArraySlices = 1;
            break;
        }

        return Subresource;
    }

    bool FTextureSubresourceSet::IsEntireTexture(const FRHIImageDesc& Desc) const
    {
        if (BaseMipLevel > 0u || BaseMipLevel + NumMipLevels < Desc.NumMips)
        {
            return false;
        }

        switch (Desc.Dimension)
        {
            case EImageDimension::Texture2D:
            case EImageDimension::Texture2DArray:
            case EImageDimension::Texture3D:
            case EImageDimension::TextureCube:
            case EImageDimension::TextureCubeArray:
            if (BaseArraySlice > 0u || BaseArraySlice + NumArraySlices < Desc.ArraySize)
            {
                return false;
            }
            default: return true;
        }
    }

    FBufferRange FBufferRange::Resolve(const FRHIBufferDesc& Desc) const
    {
        FBufferRange result;
        result.ByteOffset = std::min(ByteOffset, Desc.Size);
        if (ByteSize == 0)
        {
            result.ByteSize = Desc.Size - result.ByteOffset;
        }
        else
        {
            result.ByteSize = std::min(ByteSize, Desc.Size - result.ByteOffset);
        }
        return result;
    }
    
}

    // Format mapping table. The rows must be in the exactly same order as Format enum members are defined.
    static const FFormatInfo GFormatInfo[] =
    {
        { EFormat::UNKNOWN,           "UNKNOWN",           0,   0, EFormatKind::Integer,      false, false, false, false, false, false, false, false },
        { EFormat::R8_UINT,           "R8_UINT",           1,   1, EFormatKind::Integer,      true,  false, false, false, false, false, false, false },
        { EFormat::R8_SINT,           "R8_SINT",           1,   1, EFormatKind::Integer,      true,  false, false, false, false, false, true,  false },
        { EFormat::R8_UNORM,          "R8_UNORM",          1,   1, EFormatKind::Normalized,   true,  false, false, false, false, false, false, false },
        { EFormat::R8_SNORM,          "R8_SNORM",          1,   1, EFormatKind::Normalized,   true,  false, false, false, false, false, true,  false },
        { EFormat::RG8_UINT,          "RG8_UINT",          2,   1, EFormatKind::Integer,      true,  true,  false, false, false, false, false, false },
        { EFormat::RG8_SINT,          "RG8_SINT",          2,   1, EFormatKind::Integer,      true,  true,  false, false, false, false, true,  false },
        { EFormat::RG8_UNORM,         "RG8_UNORM",         2,   1, EFormatKind::Normalized,   true,  true,  false, false, false, false, false, false },
        { EFormat::RG8_SNORM,         "RG8_SNORM",         2,   1, EFormatKind::Normalized,   true,  true,  false, false, false, false, true,  false },
        { EFormat::R16_UINT,          "R16_UINT",          2,   1, EFormatKind::Integer,      true,  false, false, false, false, false, false, false },
        { EFormat::R16_SINT,          "R16_SINT",          2,   1, EFormatKind::Integer,      true,  false, false, false, false, false, true,  false },
        { EFormat::R16_UNORM,         "R16_UNORM",         2,   1, EFormatKind::Normalized,   true,  false, false, false, false, false, false, false },
        { EFormat::R16_SNORM,         "R16_SNORM",         2,   1, EFormatKind::Normalized,   true,  false, false, false, false, false, true,  false },
        { EFormat::R16_FLOAT,         "R16_FLOAT",         2,   1, EFormatKind::Float,        true,  false, false, false, false, false, true,  false },
        { EFormat::BGRA4_UNORM,       "BGRA4_UNORM",       2,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
        { EFormat::B5G6R5_UNORM,      "B5G6R5_UNORM",      2,   1, EFormatKind::Normalized,   true,  true,  true,  false, false, false, false, false },
        { EFormat::B5G5R5A1_UNORM,    "B5G5R5A1_UNORM",    2,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
        { EFormat::RGBA8_UINT,        "RGBA8_UINT",        4,   1, EFormatKind::Integer,      true,  true,  true,  true,  false, false, false, false },
        { EFormat::RGBA8_SINT,        "RGBA8_SINT",        4,   1, EFormatKind::Integer,      true,  true,  true,  true,  false, false, true,  false },
        { EFormat::RGBA8_UNORM,       "RGBA8_UNORM",       4,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
        { EFormat::RGBA8_SNORM,       "RGBA8_SNORM",       4,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, true,  false },
        { EFormat::BGRA8_UNORM,       "BGRA8_UNORM",       4,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
        { EFormat::SRGBA8_UNORM,      "SRGBA8_UNORM",      4,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, true  },
        { EFormat::SBGRA8_UNORM,      "SBGRA8_UNORM",      4,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
        { EFormat::R10G10B10A2_UNORM, "R10G10B10A2_UNORM", 4,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
        { EFormat::R11G11B10_FLOAT,   "R11G11B10_FLOAT",   4,   1, EFormatKind::Float,        true,  true,  true,  false, false, false, false, false },
        { EFormat::RG16_UINT,         "RG16_UINT",         4,   1, EFormatKind::Integer,      true,  true,  false, false, false, false, false, false },
        { EFormat::RG16_SINT,         "RG16_SINT",         4,   1, EFormatKind::Integer,      true,  true,  false, false, false, false, true,  false },
        { EFormat::RG16_UNORM,        "RG16_UNORM",        4,   1, EFormatKind::Normalized,   true,  true,  false, false, false, false, false, false },
        { EFormat::RG16_SNORM,        "RG16_SNORM",        4,   1, EFormatKind::Normalized,   true,  true,  false, false, false, false, true,  false },
        { EFormat::RG16_FLOAT,        "RG16_FLOAT",        4,   1, EFormatKind::Float,        true,  true,  false, false, false, false, true,  false },
        { EFormat::R32_UINT,          "R32_UINT",          4,   1, EFormatKind::Integer,      true,  false, false, false, false, false, false, false },
        { EFormat::R32_SINT,          "R32_SINT",          4,   1, EFormatKind::Integer,      true,  false, false, false, false, false, true,  false },
        { EFormat::R32_FLOAT,         "R32_FLOAT",         4,   1, EFormatKind::Float,        true,  false, false, false, false, false, true,  false },
        { EFormat::RGBA16_UINT,       "RGBA16_UINT",       8,   1, EFormatKind::Integer,      true,  true,  true,  true,  false, false, false, false },
        { EFormat::RGBA16_SINT,       "RGBA16_SINT",       8,   1, EFormatKind::Integer,      true,  true,  true,  true,  false, false, true,  false },
        { EFormat::RGBA16_FLOAT,      "RGBA16_FLOAT",      8,   1, EFormatKind::Float,        true,  true,  true,  true,  false, false, true,  false },
        { EFormat::RGBA16_UNORM,      "RGBA16_UNORM",      8,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
        { EFormat::RGBA16_SNORM,      "RGBA16_SNORM",      8,   1, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, true,  false },
        { EFormat::RG32_UINT,         "RG32_UINT",         8,   1, EFormatKind::Integer,      true,  true,  false, false, false, false, false, false },
        { EFormat::RG32_SINT,         "RG32_SINT",         8,   1, EFormatKind::Integer,      true,  true,  false, false, false, false, true,  false },
        { EFormat::RG32_FLOAT,        "RG32_FLOAT",        8,   1, EFormatKind::Float,        true,  true,  false, false, false, false, true,  false },
        { EFormat::RGB32_UINT,        "RGB32_UINT",        12,  1, EFormatKind::Integer,      true,  true,  true,  false, false, false, false, false },
        { EFormat::RGB32_SINT,        "RGB32_SINT",        12,  1, EFormatKind::Integer,      true,  true,  true,  false, false, false, true,  false },
        { EFormat::RGB32_FLOAT,       "RGB32_FLOAT",       12,  1, EFormatKind::Float,        true,  true,  true,  false, false, false, true,  false },
        { EFormat::RGBA32_UINT,       "RGBA32_UINT",       16,  1, EFormatKind::Integer,      true,  true,  true,  true,  false, false, false, false },
        { EFormat::RGBA32_SINT,       "RGBA32_SINT",       16,  1, EFormatKind::Integer,      true,  true,  true,  true,  false, false, true,  false },
        { EFormat::RGBA32_FLOAT,      "RGBA32_FLOAT",      16,  1, EFormatKind::Float,        true,  true,  true,  true,  false, false, true,  false },
        { EFormat::D16,               "D16",               2,   1, EFormatKind::DepthStencil, false, false, false, false, true,  false, false, false },
        { EFormat::D24S8,             "D24S8",             4,   1, EFormatKind::DepthStencil, false, false, false, false, true,  true,  false, false },
        { EFormat::X24G8_UINT,        "X24G8_UINT",        4,   1, EFormatKind::Integer,      false, false, false, false, false, true,  false, false },
        { EFormat::D32,               "D32",               4,   1, EFormatKind::DepthStencil, false, false, false, false, true,  false, false, false },
        { EFormat::D32S8,             "D32S8",             8,   1, EFormatKind::DepthStencil, false, false, false, false, true,  true,  false, false },
        { EFormat::X32G8_UINT,        "X32G8_UINT",        8,   1, EFormatKind::Integer,      false, false, false, false, false, true,  false, false },
        { EFormat::BC1_UNORM,         "BC1_UNORM",         8,   4, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
        { EFormat::BC1_UNORM_SRGB,    "BC1_UNORM_SRGB",    8,   4, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, true  },
        { EFormat::BC2_UNORM,         "BC2_UNORM",         16,  4, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
        { EFormat::BC2_UNORM_SRGB,    "BC2_UNORM_SRGB",    16,  4, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, true  },
        { EFormat::BC3_UNORM,         "BC3_UNORM",         16,  4, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
        { EFormat::BC3_UNORM_SRGB,    "BC3_UNORM_SRGB",    16,  4, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, true  },
        { EFormat::BC4_UNORM,         "BC4_UNORM",         8,   4, EFormatKind::Normalized,   true,  false, false, false, false, false, false, false },
        { EFormat::BC4_SNORM,         "BC4_SNORM",         8,   4, EFormatKind::Normalized,   true,  false, false, false, false, false, true,  false },
        { EFormat::BC5_UNORM,         "BC5_UNORM",         16,  4, EFormatKind::Normalized,   true,  true,  false, false, false, false, false, false },
        { EFormat::BC5_SNORM,         "BC5_SNORM",         16,  4, EFormatKind::Normalized,   true,  true,  false, false, false, false, true,  false },
        { EFormat::BC6H_UFLOAT,       "BC6H_UFLOAT",       16,  4, EFormatKind::Float,        true,  true,  true,  false, false, false, false, false },
        { EFormat::BC6H_SFLOAT,       "BC6H_SFLOAT",       16,  4, EFormatKind::Float,        true,  true,  true,  false, false, false, true,  false },
        { EFormat::BC7_UNORM,         "BC7_UNORM",         16,  4, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, false },
        { EFormat::BC7_UNORM_SRGB,    "BC7_UNORM_SRGB",    16,  4, EFormatKind::Normalized,   true,  true,  true,  true,  false, false, false, true  },
    };


namespace Lumina::RHI::Format
{
    const FFormatInfo& Info(EFormat format)
    {
        static_assert(sizeof(GFormatInfo) / sizeof(FFormatInfo) == static_cast<size_t>(EFormat::COUNT), "The format info table doesn't have the right number of elements");

        if (static_cast<uint32>(format) >= static_cast<uint32>(EFormat::COUNT))
        {
            return GFormatInfo[0];
        }

        const FFormatInfo& info = GFormatInfo[static_cast<uint32>(format)];
        DEBUG_ASSERT(info.Format == format);
        return info;
    }

    uint8 BytesPerBlock(EFormat Format)
    {
        return Info(Format).BytesPerBlock;
    }
}
