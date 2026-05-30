#pragma once

#include <volk/volk.h>
#include "VulkanDevice.h"
#include "VulkanMacros.h"
#include "Containers/Tuple.h"
#include "Memory/SmartPtr.h"
#include "Renderer/RenderResource.h"
#include "Renderer/Shader.h"
#include "Renderer/StateTracking.h"

namespace Lumina
{
    class FBitSetAllocator;
    class FVulkanRenderContext;
    class FVulkanPipelineCache;
    struct FDynamicPipelineStates;

    // Note: avoid vec3 in UBOs (std140 padding); pad to vec4 manually.
    VkFormat ConvertFormat(EFormat Format);
    
    class FVulkanSwapchain;

    struct FBufferChunk
    {
        FRHIBufferRef Buffer;
        uint64 Version = 0;
        uint64 BufferSize = 0;
        uint64 WritePointer = 0;
        void* MappedMemory = nullptr;
        // Submission cycles this chunk has sat in the pool unused. Reset to 0 when used,
        // incremented each SubmitChunks; drives reclamation of oversized idle chunks.
        uint32 IdleCycles = 0;

        static constexpr uint64 GSizeAlignment = 4096;
    };

    class FUploadManager : public IDeviceChild
    {
    public:
        
        
        FUploadManager(FVulkanRenderContext* Ctx, uint64 InDefaultChunkSize, uint64 InMemoryLimit, bool bInIsScratchBuffer);
        ~FUploadManager() = default;
        LE_NO_COPYMOVE(FUploadManager);
        
        bool SuballocateBuffer(uint64 Size, FRHIBuffer*& Buffer, uint64& Offset, void*& CpuVA, uint64 CurrentVersion, uint32 Alignment = 256);
        void SubmitChunks(uint64 CurrentVersion, uint64 SubmittedVersion);
        
    private:
        
        TSharedPtr<FBufferChunk> CreateChunk(uint64 Size) const;

    private:

        TFixedList<TSharedPtr<FBufferChunk>, 8>     ChunkPool;
        TSharedPtr<FBufferChunk>                    CurrentChunk;

        FVulkanRenderContext*                       Context;
        uint64                                      DefaultChunkSize = 0;
        uint64                                      MemoryLimit = 0;
        uint64                                      AllocatedMemory = 0;
        uint64                                      LargestChunkSize = 0;
        bool                                        bIsScratchBuffer = false;

    };
    
    class FVulkanEventQuery : public IEventQuery
    {
    public:
        RENDER_RESOURCE(RTT_EventQuery)

        FVulkanEventQuery();
        ~FVulkanEventQuery() override;

        ECommandQueue   Queue = ECommandQueue::Graphics;
        uint64          CommandListID = 0;
    };
    
    class FVulkanTimerQuery : public ITimerQuery
    {
    public:
        RENDER_RESOURCE(RTT_TimerQuery)

        FVulkanTimerQuery(FBitSetAllocator& InAllocator);
        ~FVulkanTimerQuery() override;


        FBitSetAllocator& Allocator;
        int32 BeginQueryIndex = -1;
        int32 EndQueryIndex = -1;

        bool bStarted = false;
        bool bResolved = false;
        float Time = 0.0f;
    };

    class FVulkanPipelineStatsQuery : public IPipelineStatsQuery
    {
    public:
        RENDER_RESOURCE(RTT_PipelineStatsQuery)

        FVulkanPipelineStatsQuery(FBitSetAllocator& InAllocator);
        ~FVulkanPipelineStatsQuery() override;

        FBitSetAllocator& Allocator;
        int32               QueryIndex = -1;

        bool                bStarted   = false;
        bool                bResolved  = false;
        FPipelineStats      Stats;
    };
    
    class FVulkanViewport : public FRHIViewport
    {
    public:
        
        friend class FVulkanRenderContext;
        

        FVulkanViewport(const FUIntVector2& InSize, IRenderContext* InContext, FString&& DebugName)
            : FRHIViewport(InSize, InContext, Move(DebugName))
        {}

    private:

        FVulkanSwapchain* Swapchain = nullptr;
        
    };

    // Copyable std::atomic.
    class FBufferVersionItem : public std::atomic<uint64>
    {
    public:
        FBufferVersionItem()
        { }

        FBufferVersionItem(const FBufferVersionItem& other)
        {
            store(other);
        }

        FBufferVersionItem& operator=(const uint64 a)
        {
            store(a);
            return *this;
        }
    };

    class FVulkanBuffer : public IDeviceChild, public FRHIBuffer, public FBufferStateExtension
    {
    public:
        friend class FVulkanCommandList;
        friend class FQueue;
        friend class FVulkanMemoryAllocator;
        
        FVulkanBuffer(FVulkanDevice* InDevice, const FRHIBufferDesc& InDescription);
        ~FVulkanBuffer() override;
        LE_NO_COPYMOVE(FVulkanBuffer);

        void* GetAPIResourceImpl(EAPIResourceType) override
        {
            return Buffer;
        }

        NODISCARD VkBuffer GetBuffer() const { return Buffer; }
        NODISCARD VmaAllocation GetAllocation() const { return Allocation; }
        NODISCARD void* GetMappedMemory() const;
        
        NODISCARD const FRHIBufferDesc& GetDescription() const override { return Description; }
        NODISCARD bool IsStorageBuffer() const override { return Description.Usage.IsFlagSet(EBufferUsageFlags::StorageBuffer); }
        NODISCARD bool IsUniformBuffer() const override { return Description.Usage.IsFlagSet(EBufferUsageFlags::UniformBuffer); }
        NODISCARD bool IsVertexBuffer() const override { return Description.Usage.IsFlagSet(EBufferUsageFlags::VertexBuffer); }
        NODISCARD bool IsIndexBuffer() const override { return Description.Usage.IsFlagSet(EBufferUsageFlags::IndexBuffer); }
        NODISCARD bool IsStagingBuffer() const override { return Description.Usage.IsFlagSet(EBufferUsageFlags::StagingBuffer); }
        NODISCARD uint64 GetSize() const override { return Description.Size; }
        NODISCARD uint32 GetStride() const override { return Description.Stride; }
        NODISCARD const TBitFlags<EBufferUsageFlags>& GetUsage() const override { return Description.Usage; }
        NODISCARD uint64 GetAddress() const override { return BufferAddress; }

    private:
        
        FRHIBufferDesc                      Description;
        TFixedVector<FBufferVersionItem, 2> VersionTracking;
        
        VmaAllocation                       Allocation = nullptr;
        VkBuffer                            Buffer = VK_NULL_HANDLE;

        uint64                              LastUseCommandListID = 0;
        VkDeviceAddress                     BufferAddress = 0;

        uint32                              VersionSearchStart = 0;
        ECommandQueue                       LastUseQueue = ECommandQueue::Graphics;

    };


    class FVulkanSampler : public FRHISampler,  public IDeviceChild
    {
    public:

        FVulkanSampler(FVulkanDevice* InDevice, const FSamplerDesc& InDesc);
        ~FVulkanSampler() override;

        const FSamplerDesc& GetDesc() const override { return Desc; }

        void* GetAPIResourceImpl(EAPIResourceType) override { return Sampler; }

        
    private:

        FSamplerDesc    Desc;
        VkSampler       Sampler;
    };

    struct FTextureSubresourceView
    {
        class FVulkanImage& Image;
        FTextureSubresourceSet Subresource;

        VkImageView View = nullptr;
        VkImageSubresourceRange SubresourceRange;

        FTextureSubresourceView(FVulkanImage& InImage)
            : Image(InImage)
            , SubresourceRange({})
        {
        }

        FTextureSubresourceView(const FTextureSubresourceView&) = delete;

        bool operator==(const FTextureSubresourceView& Other) const
        {
            return &Image == &Other.Image && Subresource == Other.Subresource && View == Other.View
                && SubresourceRange.aspectMask == Other.SubresourceRange.aspectMask
                && SubresourceRange.baseArrayLayer == Other.SubresourceRange.baseArrayLayer
                && SubresourceRange.baseMipLevel == Other.SubresourceRange.baseMipLevel
                && SubresourceRange.layerCount == Other.SubresourceRange.layerCount
                && SubresourceRange.levelCount == Other.SubresourceRange.levelCount;
        }
    };
    
    class FVulkanImage : public IDeviceChild, public FRHIImage, public FTextureStateExtension
    {
    public:

        enum EInternal : uint8
        {
            ExternallyManaged
        };

        enum class ESubresourceViewType : uint8
        {
            AllAspects,
            DepthOnly,
            StencilOnly,
        };

        using FSubresourceViewKey = TTuple<FTextureSubresourceSet, ESubresourceViewType, EImageDimension, EFormat, VkImageUsageFlags>;
        
        struct Hash
        {
            std::size_t operator()(const FSubresourceViewKey& TupleKey) const noexcept
            {
                const auto& [Subresources, ViewType, Dimension, Format, Usage] = TupleKey;

                size_t Hash = 0;

                Lumina::Hash::HashCombine(Hash, Subresources.BaseMipLevel);
                Lumina::Hash::HashCombine(Hash, Subresources.NumMipLevels);
                Lumina::Hash::HashCombine(Hash, Subresources.BaseArraySlice);
                Lumina::Hash::HashCombine(Hash, Subresources.NumArraySlices);
                Lumina::Hash::HashCombine(Hash, ViewType);
                Lumina::Hash::HashCombine(Hash, Dimension);
                Lumina::Hash::HashCombine(Hash, Format);
                Lumina::Hash::HashCombine(Hash, static_cast<uint32>(Usage));

                return Hash;
            }
        };
        
        using SubresourceMap = THashMap<FSubresourceViewKey, FTextureSubresourceView, Hash>;

        struct FInlineViewEntry
        {
            FSubresourceViewKey         Key;
            FTextureSubresourceView*    View = nullptr;
        };


        FVulkanImage(FVulkanDevice* InDevice, const FRHIImageDesc& InDescription);
        FVulkanImage(FVulkanDevice* InDevice, const FRHIImageDesc& InDescription, VkImage RawImage, EInternal);
        ~FVulkanImage() override;
        LE_NO_COPYMOVE(FVulkanImage);

        void* GetAPIResourceImpl(EAPIResourceType) override;

        VkImage GetImage() const { return Image; }
        void* GetRHIView(EFormat Format, FTextureSubresourceSet Subresources, EImageDimension Dimension, bool bReadyOnlyDSV) override;
        
        VkImageAspectFlags GetFullAspectMask() const { return FullAspectMask; }
        VkImageAspectFlags GetPartialAspectMask() const { return PartialAspectMask; }
        FTextureSubresourceView& GetSubresourceView(const FTextureSubresourceSet& Subresource, EImageDimension Dimension, EFormat Format, VkImageUsageFlags Usage, ESubresourceViewType ViewType = ESubresourceViewType::AllAspects);

        uint32 GetNumSubresources() const;
        uint32 GetSubresourceIndex(uint32 MipLevel, uint32 ArrayLayer) const;
        
        const FRHIImageDesc& GetDescription() const override { return Description; }
        const FUIntVector2& GetExtent() const override { return Description.Extent; }
        uint32 GetSizeX() const override { return Description.Extent.x; }
        uint32 GetSizeY() const override { return Description.Extent.y; }
        EFormat GetFormat() const override { return Description.Format; }
        TBitFlags<EImageCreateFlags> GetFlags() const override { return Description.Flags; }
        uint8 GetNumMips() const override { return Description.NumMips; }
		int32 GetResourceID() const override { return ResourceID; }
		void SetResourceID(int32 Index) override { ResourceID = Index; }

		int32 GetMipUAVIndex(uint32 Mip) const override
		{
			return Mip < MipUAVIndices.size() ? MipUAVIndices[Mip] : -1;
		}
		TVector<int32>& GetMipUAVIndices() override { return MipUAVIndices; }

    private:

		int32                   ResourceID = -1;
		TVector<int32>          MipUAVIndices;
        
        FRHIImageDesc           Description;
        SubresourceMap          SubresourceViews;
        TFixedVector<FInlineViewEntry, 4, false> InlineViewCache;
        FMutex                  SubresourceMutex;

        VkImage                 Image                   = VK_NULL_HANDLE;
        VmaAllocation           Allocation              = VK_NULL_HANDLE;
        
        VkImageAspectFlags      FullAspectMask          = VK_IMAGE_ASPECT_NONE;
        VkImageAspectFlags      PartialAspectMask       = VK_IMAGE_ASPECT_NONE;
        
        bool                    bImageManagedExternal   = false;
    };

    class FVulkanStagingImage : public FRHIStagingImage, public IDeviceChild
    {
    public:

        FVulkanStagingImage(FVulkanDevice* InDevice);

        struct FRegion
        {
            off_t Offset;
            size_t Size;
        };
        
        size_t GetBufferSize()
        {
            size_t Size = SliceRegions.back().Offset + SliceRegions.back().Size;
            return Size;
        }

        size_t ComputeSliceSize(uint32 MipLevel);
        const FRegion& GetSliceRegion(uint32 MipLevel, uint32 ArraySlice, uint32 Z);
        void PopulateSliceRegions();
        

        FRHIImageDesc Desc;
        TRefCountPtr<FVulkanBuffer> Buffer;
        TFixedVector<FRegion, 4> SliceRegions;
        
        const FRHIImageDesc& GetDesc() const override { return Desc; }
        
    };

    constexpr uint32 ShaderBinarySize = sizeof(uint32);
    
    class IVulkanShader : public IDeviceChild
    {
    public:

        IVulkanShader(FVulkanDevice* InDevice, const FShaderHeader& Shader, ERHIResourceType Type);
        ~IVulkanShader();
        

        void GetByteCodeImpl(void** ByteCode, uint64* Size)
        {
            *ByteCode = ShaderHeader.Binaries.data();
            *Size = ShaderHeader.Binaries.size() * ShaderBinarySize;
        }

        bool CompareSpirV(const IVulkanShader* Other) const
        {
            return Other->ShaderHeader.Binaries == ShaderHeader.Binaries;
        }

        NODISCARD uint64 GetShaderHashKeyImpl() const noexcept
        {
            return ShaderHashKey;
        }
    
    protected:
        
        uint64 ShaderHashKey;
        FShaderHeader ShaderHeader;
        VkShaderModule  ShaderModule = VK_NULL_HANDLE;
    };

    
    
    class FVulkanVertexShader : public FRHIVertexShader, public IVulkanShader
    {
    public:
        RENDER_RESOURCE(RRT_VertexShader)

        FVulkanVertexShader(FVulkanDevice* InDevice, const FShaderHeader& Shader)
            :IVulkanShader(InDevice, Shader, RRT_VertexShader)
        {}

        void* GetAPIResourceImpl(EAPIResourceType ResourceType) override
        {
            return ShaderModule;
        }
        
        void GetByteCode(void** ByteCode, uint64* Size) override
        {
            GetByteCodeImpl(ByteCode, Size);
        }

        uint64 GetHashCode() const override
        {
            return GetShaderHashKeyImpl();
        }

        const FShaderHeader& GetShaderHeader() const override
        {
            return ShaderHeader;
        }
    };

    class FVulkanPixelShader : public FRHIPixelShader, public IVulkanShader
    {
    public:

        RENDER_RESOURCE(RRT_PixelShader)

        FVulkanPixelShader(FVulkanDevice* InDevice, const FShaderHeader& Shader)
            :IVulkanShader(InDevice, Shader, RRT_PixelShader)
        {}

        void* GetAPIResourceImpl(EAPIResourceType ResourceType) override
        {
            return ShaderModule;
        }
        
        void GetByteCode(void** ByteCode, uint64* Size) override
        {
            GetByteCodeImpl(ByteCode, Size);
        }

        uint64 GetHashCode() const override
        {
            return GetShaderHashKeyImpl();
        }

        const FShaderHeader& GetShaderHeader() const override
        {
            return ShaderHeader;
        }
    };

    class FVulkanGeometryShader : public FRHIGeometryShader, public IVulkanShader
    {
    public:
        RENDER_RESOURCE(RTT_GeometryShader)

        FVulkanGeometryShader(FVulkanDevice* InDevice, const FShaderHeader& Shader)
            :IVulkanShader(InDevice, Shader, RTT_GeometryShader)
        {}

        void* GetAPIResourceImpl(EAPIResourceType ResourceType) override
        {
            return ShaderModule;
        }
        
        void GetByteCode(void** ByteCode, uint64* Size) override
        {
            GetByteCodeImpl(ByteCode, Size);
        }

        uint64 GetHashCode() const override
        {
            return GetShaderHashKeyImpl();
        }

        const FShaderHeader& GetShaderHeader() const override
        {
            return ShaderHeader;
        }
    };

    class FVulkanComputeShader : public FRHIComputeShader, public IVulkanShader
    {
    public:
        RENDER_RESOURCE(RRT_ComputeShader)

        FVulkanComputeShader(FVulkanDevice* InDevice, const FShaderHeader& Shader)
            :IVulkanShader(InDevice, Shader, RRT_ComputeShader)
        {}

        void* GetAPIResourceImpl(EAPIResourceType ResourceType) override
        {
            return ShaderModule;
        }
        
        void GetByteCode(void** ByteCode, uint64* Size) override
        {
            GetByteCodeImpl(ByteCode, Size);
        }

        uint64 GetHashCode() const override
        {
            return GetShaderHashKeyImpl();
        }

        const FShaderHeader& GetShaderHeader() const override
        {
            return ShaderHeader;
        }
    };

    class FVulkanInputLayout : public FRHIInputLayout
    {
    public:
    
        RENDER_RESOURCE(RTT_InputLayout)

        FVulkanInputLayout(const FVertexAttributeDesc* InAttributeDesc, uint32 AttributeCount);
        void* GetAPIResourceImpl(EAPIResourceType) override;
        
        
        TFixedVector<FVertexAttributeDesc, 4> InputDesc;
        TFixedVector<VkVertexInputBindingDescription, 4> BindingDesc;
        TFixedVector<VkVertexInputAttributeDescription, 4> AttributeDesc;
        
        uint32 GetNumAttributes() const override;
        const FVertexAttributeDesc* GetAttributeDesc(uint32 index) const override;
    };


    class FVulkanBindingLayout : public FRHIBindingLayout, public IDeviceChild
    {
    public:

        RENDER_RESOURCE(RRT_BindingLayout)

        FVulkanBindingLayout(FVulkanDevice* InDevice, const FBindingLayoutDesc& InDesc);
        FVulkanBindingLayout(FVulkanDevice* InDevice, const FBindlessLayoutDesc& InDesc);

        ~FVulkanBindingLayout() override;

        bool Bake();
        const FBindingLayoutDesc* GetDesc() const override { return bBindless ? nullptr : &Desc; }
        const FBindlessLayoutDesc* GetBindlessDesc() const override { return bBindless ? &BindlessDesc : nullptr; }
        void* GetAPIResourceImpl(EAPIResourceType) override;
        
        bool                                            bBindless = false;
        FBindingLayoutDesc                              Desc;
        FBindlessLayoutDesc                             BindlessDesc;
        
        VkDescriptorSetLayout                           DescriptorSetLayout;
        TFixedVector<VkDescriptorSetLayoutBinding, 2>   Bindings;
        TFixedVector<VkDescriptorPoolSize, 2>           PoolSizes;
    };

    class FVulkanBindingSet : public FRHIBindingSet, public IDeviceChild
    {
    public:

        RENDER_RESOURCE(RRT_BindingSet)
        
        FVulkanBindingSet(FVulkanRenderContext* RenderContext, const FBindingSetDesc& InDesc, FVulkanBindingLayout* InLayout);
        ~FVulkanBindingSet() override;

        const FBindingSetDesc* GetDesc() const override { return &Desc; }
        FRHIBindingLayout* GetLayout() const override { return Layout; }
        
    protected:
        
        void* GetAPIResourceImpl(EAPIResourceType) override;
    
    public:
        
        TFixedVector<FRHIBufferRef, 2>              DynamicBuffers;
        TFixedVector<FRHIResourceRef, 4>            Resources;
        TFixedVector<uint16, 4>                     BindingsRequiringTransitions;

        
        TRefCountPtr<FVulkanBindingLayout>          Layout;
        FBindingSetDesc                             Desc;
        VkDescriptorPool                            DescriptorPool;
        VkDescriptorSet                             DescriptorSet;
    };

    class FVulkanDescriptorTable : public FRHIDescriptorTable, public IDeviceChild
    {
    public:

        RENDER_RESOURCE(RRT_DescriptorTable)

        FVulkanDescriptorTable(const FVulkanRenderContext* InContext, FVulkanBindingLayout* InLayout);
        ~FVulkanDescriptorTable() override;
        
        const FBindingSetDesc* GetDesc() const override { return nullptr; }
        FRHIBindingLayout* GetLayout() const override { return Layout; }
        uint32_t GetCapacity() const override { return Capacity; }

        uint32_t GetFirstDescriptorIndexInHeap() const override { return 0; }
        void* GetAPIResourceImpl(EAPIResourceType) override;
        
        TRefCountPtr<FVulkanBindingLayout> Layout;
        uint32 Capacity = 0;

        VkDescriptorPool DescriptorPool = nullptr;
        VkDescriptorSet DescriptorSet = nullptr;
    };

    class FVulkanPipeline : public IDeviceChild
    {
    public:
        
        ~FVulkanPipeline();

        FVulkanPipeline(FVulkanDevice* InDevice)
            : IDeviceChild(InDevice)
            , PipelineLayout(nullptr)
            , Pipeline(nullptr)
            , PushConstantVisibility(VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM)
        {
        }
        
        void CreatePipelineLayout(const FString& DebugName, const TFixedVector<FRHIBindingLayoutRef, MaxBindingLayouts>& BindingLayouts, VkShaderStageFlags InStageMask, VkShaderStageFlags& OutStageFlags);
        
        VkPipelineLayout            PipelineLayout;
        VkPipeline                  Pipeline;
        VkShaderStageFlags          PushConstantVisibility;

    };
    
    // Vulkan-ready dynamic-state values, precomputed at pipeline creation so SetGraphicsState
    // feeds vkCmdSet* without re-converting per draw. Per-attachment arrays cover blend states.
    struct FGraphicsDynamicStateValues
    {
        VkCullModeFlags         CullMode            = VK_CULL_MODE_NONE;
        VkFrontFace             FrontFace           = VK_FRONT_FACE_CLOCKWISE;
        VkBool32                DepthTestEnable     = VK_FALSE;
        VkBool32                DepthWriteEnable    = VK_FALSE;
        VkCompareOp             DepthCompareOp      = VK_COMPARE_OP_ALWAYS;
        VkPolygonMode           PolygonMode         = VK_POLYGON_MODE_FILL;
        uint32                  ColorAttachmentCount = 0;
        VkBool32                BlendEnable[MaxRenderTargets]              = {};
        VkColorBlendEquationEXT BlendEquation[MaxRenderTargets]           = {};
        VkColorComponentFlags   ColorWriteMask[MaxRenderTargets]          = {};
    };

    // Builds a VkPipeline from a (canonicalized) desc, declaring the states in Dyn as
    // dynamic. Shared across RHI pipeline objects that differ only in dynamic state.
    VkPipeline CreateGraphicsVkPipeline(FVulkanDevice* Device, const FGraphicsPipelineDesc& Desc, const FRenderPassDesc& RenderPassDesc, VkPipelineLayout Layout, const FDynamicPipelineStates& Dyn);
    FGraphicsDynamicStateValues ComputeGraphicsDynamicStateValues(const FGraphicsPipelineDesc& Desc, const FRenderPassDesc& RenderPassDesc);

    class FVulkanGraphicsPipeline : public FRHIGraphicsPipeline,  public FVulkanPipeline
    {
    public:

        friend class FVulkanRenderContext;

        FVulkanGraphicsPipeline(FVulkanDevice* InDevice, const FGraphicsPipelineDesc& InDesc, const FRenderPassDesc& RenderPassDesc, FVulkanPipelineCache* InCache);

        // The shared VkPipeline (stored in the base Pipeline member) is owned by the
        // pipeline cache, not this object -- null it so ~FVulkanPipeline doesn't free it.
        ~FVulkanGraphicsPipeline() { Pipeline = VK_NULL_HANDLE; }

        const FGraphicsPipelineDesc& GetDesc() const override { return Desc; }

        const FGraphicsDynamicStateValues& GetDynamicStateValues() const { return DynamicStateValues; }
        size_t GetSharedPipelineHash() const { return SharedPipelineHash; }

    protected:

        void* GetAPIResourceImpl(EAPIResourceType InType) override;

    private:

        FGraphicsPipelineDesc       Desc;
        FGraphicsDynamicStateValues DynamicStateValues;
        size_t                      SharedPipelineHash = 0;

    };

    class FVulkanComputePipeline : public FRHIComputePipeline,  public FVulkanPipeline
    {
    public:

        friend class FVulkanRenderContext;

        FVulkanComputePipeline(FVulkanDevice* InDevice, const FComputePipelineDesc& InDesc);

        const FComputePipelineDesc& GetDesc() const override { return Desc; }
        void* GetAPIResourceImpl(EAPIResourceType InType) override;
    
    private:

        FComputePipelineDesc Desc;
    };
    
}
