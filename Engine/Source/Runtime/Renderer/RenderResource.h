#pragma once

#include "Format.h"
#include "RenderTypes.h"
#include "ViewVolume.h"
#include "Containers/String.h"
#include "Core/LuminaMacros.h"
#include "Core/Math/Color.h"
#include "Core/Math/Hash/CoreHashTypes.h"
#include "Core/Serialization/Archiver.h"
#include "Core/Threading/Thread.h"
#include "Core/Variant/Variant.h"
#include "Memory/RefCounted.h"
#include "Types/BitFlags.h"


namespace Lumina
{
	class FRHITaskShader;
	class FRHIMeshShader;
	struct FShaderCompileOptions;
	class FRHIInputLayout;
	struct FShaderHeader;
	enum class EBufferUsageFlags : uint32;
    class IRenderContext;
}

    static constexpr uint32 MaxRenderTargets = 8;
	static constexpr uint32 MaxVertexAttributes = 16;
	// 256 B matches D3D12 root-signature cap; ColorGrading needs 144 B.
	static constexpr uint32 MaxPushConstantSize = 256;
	static constexpr uint32 MaxBindingLayouts = 4;
	static constexpr uint32 MaxBindingsPerLayout = 128;


enum class EFormatKind : uint8
{
	Integer,
	Normalized,
	Float,
	DepthStencil
};

struct RUNTIME_API FFormatInfo
{
	EFormat Format;
	const char* Name;
	uint8 BytesPerBlock;
	uint8 BlockSize;
	EFormatKind Kind;
	uint8 bHasRed : 1;
	uint8 bHasGreen : 1;
	uint8 bHasBlue : 1;
	uint8 bHasAlpha : 1;
	uint8 bHasDepth : 1;
	uint8 bHasStencil : 1;
	uint8 bIsSigned : 1;
	uint8 bIsSRGB : 1;
};

namespace Lumina::RHI::Format
{
	RUNTIME_API const FFormatInfo& Info(EFormat format);
	RUNTIME_API uint8 BytesPerBlock(EFormat Format);
}

enum ERHIResourceType : uint8
{
	RRT_None,

	RRT_SamplerState,
	RTT_InputLayout,
	RRT_BindingLayout,
	RRT_BindingSet,
	RRT_DepthStencilState,
	RRT_BlendState,
	RRT_VertexShader,
	RRT_PixelShader,
	RRT_ComputeShader,
	RTT_GeometryShader,
	RRT_MeshShader,
	RRT_TaskShader,
	RRT_ShaderLibrary,
	RRT_GraphicsPipeline,
	RRT_ComputePipeline,
	RRT_UniformBufferLayout,
	RRT_UniformBuffer,
	RRT_Buffer,
	RRT_Image,
	RRT_GPUFence,
	RRT_Viewport,
	RRT_StagingBuffer,
	RRT_CommandList,
	RRT_DescriptorTable,
	RTT_EventQuery,
	RTT_TimerQuery,
	RTT_PipelineStatsQuery,

	RRT_Num
};

enum class ERHIBindingResourceType : uint8
{
	Unknown,
	
	Texture_SRV,
	Buffer_SRV,

	Sampler,

	Texture_UAV,
	Buffer_UAV,

	Buffer_CBV,
	Buffer_Uniform = Buffer_CBV,
	Buffer_Uniform_Dynamic,
	Buffer_Storage_Dynamic,
};

enum class ERHIBindingPoint : uint8
{
	Graphics,
	Compute,
};

ENUM_CLASS_FLAGS(ERHIResourceType)


enum class ERHIShaderType : uint8
{
	None =		RRT_None,
	Vertex =	RRT_VertexShader,
	Fragment =	RRT_PixelShader,
	Compute =	RRT_ComputeShader,
	Geometry =  RTT_GeometryShader,
	Mesh =      RRT_MeshShader,
	Task =      RRT_TaskShader,
};

ENUM_CLASS_FLAGS(ERHIShaderType)

enum class EAPIResourceType : uint8
{
	Default = 0,
	Buffer,
	Image,
	ImageView,
	Sampler,
	ShaderModule,
	Pipeline,
	PipelineLayout,
	RenderPass,
	Framebuffer,
	DescriptorSet,
	DescriptorSetLayout,
	DescriptorPool,
	CommandPool,
	CommandBuffer,
	Semaphore,
	Fence,
	Event,
	QueryPool,
	DeviceMemory,
	Swapchain,
	Surface,
	Device,
	Instance,
	Queue
};

enum class EPrimitiveType : uint8
{
	PointList,
	LineList,
	LineStrip,
	TriangleList,
	TriangleStrip,
	TriangleFan,
	TriangleListWithAdjacency,
	TriangleStripWithAdjacency,
	PatchList
};

#define RENDER_RESOURCE(TypeValue) \
static constexpr ERHIResourceType Type = ERHIResourceType::TypeValue; \
static constexpr ERHIResourceType GetStaticResourceType() { return ERHIResourceType::TypeValue; } \
virtual ERHIResourceType GetResourceType() const override { return ERHIResourceType:: TypeValue; }

namespace Lumina
{
	constexpr uint64 GVersionSubmittedFlag = 0x8000000000000000;
	constexpr uint32 GVersionQueueShift = 60;
	constexpr uint32 GVersionQueueMask = 0x7;
	constexpr uint64 GVersionIDMask = 0x0FFFFFFFFFFFFFFF;

	constexpr uint64 MakeVersion(uint64 ID, ECommandQueue Queue, bool Submitted)
	{
		uint64 Result = (ID & GVersionIDMask) | (uint64(Queue) << GVersionQueueShift);
		if (Submitted)
		{
			Result |= GVersionSubmittedFlag;
		}
		return Result;
	}

	constexpr uint64_t VersionGetInstance(uint64 version)
	{
		return version & GVersionIDMask;
	}

	constexpr ECommandQueue VersionGetQueue(uint64 version)
	{
		return ECommandQueue((version >> GVersionQueueShift) & GVersionQueueMask);
	}

	constexpr bool VersionGetSubmitted(uint64 version)
	{
		return (version & GVersionSubmittedFlag) != 0;
	}
	
	struct FDrawMeshTasksArguments
	{
		uint32 GroupCountY;
		uint32 GroupCountX;
		uint32 GroupCountZ;
	};
	
	struct FDrawIndirectArguments
	{
		uint32 VertexCount = 0;
		uint32 InstanceCount = 1;
		uint32 StartVertexLocation = 0;
		uint32 StartInstanceLocation = 0;

		constexpr FDrawIndirectArguments& SetVertexCount(uint32_t value) { VertexCount = value; return *this; }
		constexpr FDrawIndirectArguments& SetInstanceCount(uint32_t value) { InstanceCount = value; return *this; }
		constexpr FDrawIndirectArguments& SetStartVertexLocation(uint32_t value) { StartVertexLocation = value; return *this; }
		constexpr FDrawIndirectArguments& SetStartInstanceLocation(uint32_t value) { StartInstanceLocation = value; return *this; }
	};

	struct FDrawIndexedIndirectArguments
	{
		uint32 IndexCount = 0;
		uint32 InstanceCount = 1;
		uint32 StartIndexLocation = 0;
		int32  BaseVertexLocation = 0;
		uint32 StartInstanceLocation = 0;

		constexpr FDrawIndexedIndirectArguments& SetIndexCount(uint32 value) { IndexCount = value; return *this; }
		constexpr FDrawIndexedIndirectArguments& SetInstanceCount(uint32 value) { InstanceCount = value; return *this; }
		constexpr FDrawIndexedIndirectArguments& SetStartIndexLocation(uint32 value) { StartIndexLocation = value; return *this; }
		constexpr FDrawIndexedIndirectArguments& SetBaseVertexLocation(int32 value) { BaseVertexLocation = value; return *this; }
		constexpr FDrawIndexedIndirectArguments& SetStartInstanceLocation(uint32 value) { StartInstanceLocation = value; return *this; }
	};

	class RUNTIME_API FRHIResourceList
	{
	public:

		friend class IRHIResource;
		
		void Track(IRHIResource* Resource);

		void Untrack(IRHIResource* Resource);

		void Clear();

		template<typename TCallable>
		requires(eastl::is_invocable_v<TCallable, IRHIResource*>)
		static void ForEach(TCallable&& Callable)
		{
			FRHIResourceList& List = Get();
			
			FRecursiveScopeLock Lock(List.Mutex);
			for (IRHIResource* Resource : List.ResourceList)
			{
				eastl::invoke(Callable, Resource);
			}
		}
		
	private:

		static FRHIResourceList& Get();

		FRecursiveMutex Mutex;
		TFixedHashSet<IRHIResource*, 1> ResourceList;
	};

	// Free-threaded, atomically ref-counted; may be deleted from any thread.
	class RUNTIME_API IRHIResource
    {
    public:

    	virtual ERHIResourceType GetResourceType() const = 0;

    	IRHIResource();
    	virtual ~IRHIResource();
    	IRHIResource(const IRHIResource&) = delete;
    	IRHIResource(const IRHIResource&&) = delete;
    	IRHIResource& operator=(const IRHIResource&) = delete;
    	IRHIResource& operator=(const IRHIResource&&) = delete;

    	static void BeginTrackingResource(IRHIResource* InResource);
    	static void EndTrackingResource(IRHIResource* InResource);
    	static void ReleaseAllRHIResources();
    	static uint32 GetNumberRHIResources();
    	
    	
    	template<typename T, EAPIResourceType Type = EAPIResourceType::Default>
		T GetAPI()
    	{
    		void* Resource = GetAPIResourceImpl(Type);
    		return static_cast<T>(Resource);
    	}

    	void* GetAPI(EAPIResourceType Type)
    	{
    		void* Resource = GetAPIResourceImpl(Type);
    		return Resource;
    	}
    
    protected:

    	virtual void* GetAPIResourceImpl(EAPIResourceType Type) { return nullptr; }
    
    public:

    	        
    	uint32 AddRef() const
    	{
    		uint32 NewValue = RefCount.fetch_add(1, std::memory_order_relaxed) + 1;
    		return NewValue;
    	}
        
    	uint32 Release()
    	{
    		DEBUG_ASSERT((RefCount.load(std::memory_order_relaxed) > 0));
    		uint32 Value = RefCount.fetch_sub(1, std::memory_order_release);
    		if(Value == 1)
    		{
    			std::atomic_thread_fence(std::memory_order_acquire);
    			Memory::Delete(this);
    		}

    		return Value;
    	}
        
    	uint32 GetRefCount() const
    	{
    		return RefCount.load(std::memory_order_relaxed);
    	}

    	bool IsValid() const
    	{
    		return RefCount.load(std::memory_order_acquire) > 0;
    	}
	
    private:

    	mutable TAtomic<uint32> RefCount{0};
    };
	
	//-------------------------------------------------------------------------------------------------------------------

	class IEventQuery : public IRHIResource { };
	class ITimerQuery : public IRHIResource { };

	// Hardware pipeline-statistics counters collected between Begin/EndPipelineStats.
	// Values reflect exactly what the GPU saw; missing counters read back as 0.
	struct FPipelineStats
	{
		uint64 InputAssemblyVertices    = 0;
		uint64 InputAssemblyPrimitives  = 0;
		uint64 VertexShaderInvocations  = 0;
		uint64 ClippingInvocations      = 0;
		uint64 ClippingPrimitives       = 0;
		uint64 FragmentShaderInvocations= 0;
		uint64 ComputeShaderInvocations = 0;

		void operator += (const FPipelineStats& Other)
		{
			InputAssemblyVertices     += Other.InputAssemblyVertices;
			InputAssemblyPrimitives   += Other.InputAssemblyPrimitives;
			VertexShaderInvocations   += Other.VertexShaderInvocations;
			ClippingInvocations       += Other.ClippingInvocations;
			ClippingPrimitives        += Other.ClippingPrimitives;
			FragmentShaderInvocations += Other.FragmentShaderInvocations;
			ComputeShaderInvocations  += Other.ComputeShaderInvocations;
		}
	};

	class IPipelineStatsQuery : public IRHIResource { };

	using FRHIEventQueryRef         = TRefCountPtr<IEventQuery>;
	using FRHITimerQueryRef			= TRefCountPtr<ITimerQuery>;
	using FRHIPipelineStatsQueryRef = TRefCountPtr<IPipelineStatsQuery>;
	using FRHIResourceRef           = TRefCountPtr<IRHIResource>;
	using FRHIBufferRef             = TRefCountPtr<FRHIBuffer>;
	using FRHIImageRef              = TRefCountPtr<FRHIImage>;
	using FRHIStagingImageRef       = TRefCountPtr<FRHIStagingImage>;
	using FRHISamplerRef            = TRefCountPtr<FRHISampler>;
	using FRHIShaderRef             = TRefCountPtr<FRHIShader>;
	using FRHIVertexShaderRef       = TRefCountPtr<FRHIVertexShader>;
	using FRHIPixelShaderRef        = TRefCountPtr<FRHIPixelShader>;
	using FRHIComputeShaderRef      = TRefCountPtr<FRHIComputeShader>;
	using FRHIGeometryShaderRef     = TRefCountPtr<FRHIGeometryShader>;
	using FRHIMeshShaderRef         = TRefCountPtr<FRHIMeshShader>;
	using FRHITaskShaderRef         = TRefCountPtr<FRHITaskShader>;
	using FRHICommandListRef        = TRefCountPtr<ICommandList>;
	using FRHIViewportRef           = TRefCountPtr<FRHIViewport>;
	using FRHIGraphicsPipelineRef   = TRefCountPtr<FRHIGraphicsPipeline>;
	using FRHIComputePipelineRef    = TRefCountPtr<FRHIComputePipeline>;
	using FRHIBindingLayoutRef      = TRefCountPtr<FRHIBindingLayout>;
	using FRHIBindingSetRef         = TRefCountPtr<FRHIBindingSet>;
	using FRHIInputLayoutRef        = TRefCountPtr<FRHIInputLayout>;
	using FRHIShaderLibraryRef      = TRefCountPtr<FShaderLibrary>;
	using FRHIDescriptorTableRef    = TRefCountPtr<FRHIDescriptorTable>;
	

	class RUNTIME_API FRHIViewport : public IRHIResource
	{
	public:

		RENDER_RESOURCE(RRT_Viewport)
		
		FRHIViewport(const glm::uvec2& InSize, IRenderContext* InContext, FString&& InDebugName)
			: RenderContext(InContext)
			, Size(InSize)
			, DebugName(Move(InDebugName))
		{
			CreateRenderTarget(InSize);
		}

		FRHIViewport(const FViewVolume& InVolume, const glm::uvec2& InSize, IRenderContext* InContext, FString&& InDebugName)
			: RenderContext(InContext)
			, ViewVolume(InVolume)
			, Size(InSize)
			, DebugName(Move(InDebugName))
		{
			CreateRenderTarget(InSize);
		}
		
		void SetSize(const glm::uvec2& InSize);
		

		const FViewVolume& GetViewVolume() const { return ViewVolume; }
		const glm::uvec2& GetSize() const { return Size; }

		void SetViewVolume(const FViewVolume& InVolume) { ViewVolume = InVolume;}
		virtual FRHIImageRef GetRenderTarget() const { return RenderTarget; }

	private:

		void CreateRenderTarget(const glm::uvec2& InSize);

	protected:

		IRenderContext*		RenderContext;
		FRHIImageRef		RenderTarget;
		FViewVolume        	ViewVolume;
		glm::uvec2       	Size; 
		FString				DebugName;
	};


	//-------------------------------------------------------------------------------------------------------------------

	struct FDynamicBufferWrite
	{
		int64 LatestVersion = 0;
		int64 MinVersion = 0;
		int64 MaxVersion = 0;
		bool bInitialized = false;
	};

	struct RUNTIME_API FRHIBufferDesc
	{
		uint64 Size = 0;
		uint32 Stride = 0;
		uint32 MaxVersions = 0;
		FString DebugName;
		EResourceStates InitialState = EResourceStates::Common;
		bool bKeepInitialState = false;
		TBitFlags<EBufferUsageFlags> Usage;

		FRHIBufferDesc() = default;
		FRHIBufferDesc(uint64 InSize, uint32 InStride, EBufferUsageFlags InUsage)
			: Size  (InSize)
			, Stride(InStride)
			, Usage (InUsage)
		{}

		static FRHIBufferDesc Null()
		{
			return {0, 0, BUF_NullResource};
		}

		NODISCARD bool IsNull() const
		{
			if (Usage.IsFlagSet(BUF_NullResource))
			{
				DEBUG_ASSERT(Size == 0 && Stride == 0 && Usage.IsFlagSet(BUF_NullResource));
				return true;
			}

			return false;
		}
	};

	
	class RUNTIME_API FRHIBuffer : public IRHIResource
	{
	public:

		RENDER_RESOURCE(RRT_Buffer)
		
		virtual const FRHIBufferDesc& GetDescription() const = 0;
		virtual bool IsStorageBuffer() const = 0;
		virtual bool IsUniformBuffer() const = 0;
		virtual bool IsVertexBuffer() const = 0;
		virtual bool IsIndexBuffer() const = 0;
		virtual bool IsStagingBuffer() const = 0;
		virtual uint64 GetSize() const = 0;
		virtual uint32 GetStride() const = 0;
		virtual const TBitFlags<EBufferUsageFlags>& GetUsage() const = 0;
		virtual uint64 GetAddress() const = 0;
		
	};


	//-------------------------------------------------------------------------------------------------------------------
	
	
	struct FRHIImageDesc
	{
		TBitFlags<EImageCreateFlags> Flags = 0;
		uint32 ExtraData = 0;
		glm::uvec2 Extent = glm::uvec2(1);
		uint16 Depth = 1;
		uint16 ArraySize = 1;
		uint8 NumMips = 1;
		uint8 NumSamples = 1;
		EImageDimension Dimension = EImageDimension::Texture2D;
		EFormat Format = EFormat::UNKNOWN;
		FString DebugName;
		EResourceStates InitialState = EResourceStates::Unknown;

		// If true, command lists auto-track from InitialState and restore it on close.
		bool bKeepInitialState = false;
		
		friend FArchive& operator << (FArchive& Ar, FRHIImageDesc& Data)
		{
			Ar << Data.Flags;
			Ar << Data.ExtraData;
			Ar << Data.Extent;
			Ar << Data.Depth;
			Ar << Data.ArraySize;
			Ar << Data.NumMips;
			Ar << Data.NumSamples;
			Ar << Data.Dimension;
			Ar << Data.Format;
			Ar << Data.DebugName;
			Ar << Data.InitialState;
			Ar << Data.bKeepInitialState;
			
			return Ar;
		}
		
	};

	// Describes a 2D or 3D section of a single mip level, single array slice of a texture.
	struct FTextureSlice
	{
		uint32 X = 0;
		uint32 Y = 0;
		uint32 Z = 0;

		uint32 Width = uint32(-1);
		uint32 Height = uint32(-1);
		uint32 Depth = uint32(-1);

		uint32 MipLevel = 0;
		uint32 ArraySlice = 0;

		NODISCARD RUNTIME_API FTextureSlice Resolve(const FRHIImageDesc& desc) const;

		constexpr FTextureSlice& SetOrigin(uint32 vx = 0, uint32 vy = 0, uint32 vz = 0) { X = vx; Y = vy; Z = vz; return *this; }
		constexpr FTextureSlice& SetMipLevel(uint32 level) { MipLevel = level; return *this; }
		constexpr FTextureSlice& SetArraySlice(uint32 slice) { ArraySlice = slice; return *this; }
	};
	
	struct FTextureSubresourceSet
    {
        static constexpr uint32 AllMipLevels = uint32(-1);
        static constexpr uint32 AllArraySlices = uint32(-1);
        
		uint32 BaseMipLevel = 0;
		uint32 NumMipLevels = 1;
		uint32 BaseArraySlice = 0;
		uint32 NumArraySlices = 1;

        FTextureSubresourceSet() = default;

        FTextureSubresourceSet(uint32 _baseMipLevel, uint32 _numMipLevels, uint32 _baseArraySlice, uint32 _numArraySlices)
            : BaseMipLevel(_baseMipLevel)
            , NumMipLevels(_numMipLevels)
            , BaseArraySlice(_baseArraySlice)
            , NumArraySlices(_numArraySlices)
        {}

        NODISCARD RUNTIME_API FTextureSubresourceSet Resolve(const FRHIImageDesc& Desc, bool bSingleMipLevel) const;
        NODISCARD RUNTIME_API bool IsEntireTexture(const FRHIImageDesc& Desc) const;

        bool operator ==(const FTextureSubresourceSet& other) const
        {
            return BaseMipLevel == other.BaseMipLevel &&
					NumMipLevels == other.NumMipLevels &&
					BaseArraySlice == other.BaseArraySlice &&
					NumArraySlices == other.NumArraySlices;
        }
        bool operator !=(const FTextureSubresourceSet& other) const { return !(*this == other); }

        constexpr FTextureSubresourceSet& SetBaseMipLevel(uint32 value) { BaseMipLevel = value; return *this; }
        constexpr FTextureSubresourceSet& SetNumMipLevels(uint32 value) { NumMipLevels = value; return *this; }
        constexpr FTextureSubresourceSet& SetMipLevels(uint32 base, uint32 num) { BaseMipLevel = base; NumMipLevels = num; return *this; }
        constexpr FTextureSubresourceSet& SetBaseArraySlice(uint32 value) { BaseArraySlice = value; return *this; }
        constexpr FTextureSubresourceSet& SetNumArraySlices(uint32 value) { NumArraySlices = value; return *this; }
        constexpr FTextureSubresourceSet& SetArraySlices(uint32 base, uint32 num) { BaseArraySlice = base; NumArraySlices = num; return *this; }

        // see the bottom of this file for a specialization of std::hash<TextureSubresourceSet>
    };

    static const FTextureSubresourceSet AllSubresources = FTextureSubresourceSet(0, FTextureSubresourceSet::AllMipLevels, 0, FTextureSubresourceSet::AllArraySlices);
	
	enum class ESamplerAddressMode : uint8
	{
		// D3D names
		Clamp = 0,
		Wrap = 1,
		Border = 2,
		Mirror = 3,
		MirrorOnce = 4,

		// Vulkan names
		ClampToEdge			= Clamp,
		Repeat				= Wrap,
		ClampToBorder		= Border,
		MirroredRepeat		= Mirror,
		MirrorClampToEdge	= MirrorOnce
	};

	#define AM_Clamp ESamplerAddressMode::Clamp
	#define AM_Wrap ESamplerAddressMode::Wrap
	#define AM_Repeat ESamplerAddressMode::Repeat
	#define AM_Border ESamplerAddressMode::Border
	#define AM_Mirror ESamplerAddressMode::Mirror
	#define AM_MirrorOnce ESamplerAddressMode::MirrorOnce

	struct FRenderPassDesc
	{
		struct alignas(16) FAttachment
		{
			FTextureSubresourceSet	Subresources	= FTextureSubresourceSet(0, 1, 0 ,1);
			glm::vec4				ClearColor		= glm::vec4(0.0f);
			FRHIImage*				Image			= nullptr;
			FRHIImage*				ResolveImage	= nullptr;
			ERenderLoadOp			LoadOp			= ERenderLoadOp::Clear;
			ERenderStoreOp			StoreOp			= ERenderStoreOp::Store;
			EFormat					Format			= EFormat::UNKNOWN;
			bool					bReadOnly		= false;

			FORCEINLINE constexpr FAttachment& SetFormat(EFormat f) { Format = f; return *this; }
			FORCEINLINE constexpr FAttachment& SetImage(FRHIImage* t) { Image = t; return *this; }
			FORCEINLINE constexpr FAttachment& SetResolveImage(FRHIImage* Value) { ResolveImage = Value; return *this; }
			FORCEINLINE constexpr FAttachment& SetLoadOp(ERenderLoadOp Op) { LoadOp = Op; return *this; }
			FORCEINLINE constexpr FAttachment& SetStoreOp(ERenderStoreOp Op) { StoreOp = Op; return *this; }
			FORCEINLINE constexpr FAttachment& SetDepthClearValue(float Value) { ClearColor.r = Value; return *this; }
			FORCEINLINE constexpr FAttachment& SetClearColor(glm::vec4 Col) { ClearColor = Col; return *this; }
			FORCEINLINE constexpr FAttachment& SetSubresources(const FTextureSubresourceSet& Value) { Subresources = Value; return *this; }
			FORCEINLINE constexpr FAttachment& SetArraySlice(uint32 index) { Subresources.BaseArraySlice = index; Subresources.NumArraySlices = 1; return *this; }
			FORCEINLINE constexpr FAttachment& SetNumSlices(uint32 Value) { Subresources.NumArraySlices = Value; return *this; }
			FORCEINLINE constexpr FAttachment& SetArraySliceRange(uint32 index, uint32 count) { Subresources.BaseArraySlice = index; Subresources.NumArraySlices = count; return *this; }
			FORCEINLINE constexpr FAttachment& SetMipLevel(uint32 level) { Subresources.BaseMipLevel = level; Subresources.NumMipLevels = 1; return *this; }
			FORCEINLINE constexpr FAttachment& SetReadOnly(bool ro) { bReadOnly = ro; return *this; }
        	
			FORCEINLINE bool IsValid() const { return Image != nullptr; }
			FORCEINLINE explicit operator bool() const noexcept { return IsValid(); }
		};
        
		TFixedVector<FAttachment, 2>	ColorAttachments;
		FAttachment						DepthAttachment;
		glm::uvec2						RenderArea;
		uint32							ViewMask = 0;
		uint16							SampleCount = 1;

		FORCEINLINE FRenderPassDesc& SetSampleCount(uint16 Count) { SampleCount = Count; return *this; }

		// If SampleCount==1, infer it from the first MSAA-capable attachment.
		RUNTIME_API uint16 DeriveSampleCount() const noexcept;

		FORCEINLINE FRenderPassDesc& SetViewMask(uint32 Mask) { ViewMask = Mask; return *this; }
		FORCEINLINE FRenderPassDesc& SetRenderArea(const glm::uvec2& Area) { RenderArea = Area; return *this; }
		FORCEINLINE FRenderPassDesc& AddColorAttachment(const FAttachment& a) { ColorAttachments.push_back(a); return *this; }
		FORCEINLINE FRenderPassDesc& AddColorAttachment(FRHIImage* texture) { ColorAttachments.push_back(FAttachment().SetImage(texture)); return *this; }
		FORCEINLINE FRenderPassDesc& AddColorAttachment(FRHIImage* texture, FTextureSubresourceSet subresources) { ColorAttachments.push_back(FAttachment().SetImage(texture).SetSubresources(subresources)); return *this; }
		FORCEINLINE FRenderPassDesc& SetDepthAttachment(const FAttachment& Attachment) { DepthAttachment = Attachment; return *this; }
		FORCEINLINE FRenderPassDesc& SetDepthAttachment(const FAttachment& Attachment, FTextureSubresourceSet Subresources) { DepthAttachment = Attachment; DepthAttachment.SetSubresources(Subresources); return *this; }
		FORCEINLINE FRenderPassDesc& SetDepthAttachment(FRHIImage* Texture) { DepthAttachment.SetImage(Texture); return *this; }
		FORCEINLINE FRenderPassDesc& SetDepthAttachment(FRHIImage* Texture, const FTextureSubresourceSet& Subresources) { DepthAttachment.SetImage(Texture).SetSubresources(Subresources); return *this; }

		FORCEINLINE bool IsValid() const noexcept { return !ColorAttachments.empty() || DepthAttachment.IsValid(); }

		static bool CompareAttachments(const FAttachment& A, const FAttachment& B)
		{
			if (A.Image != B.Image)
			{
				return false;
			}
			if (A.ResolveImage != B.ResolveImage)
			{
				return false;
			}

			return A.Subresources == B.Subresources &&
				   A.LoadOp == B.LoadOp &&
				   A.StoreOp == B.StoreOp &&
				   A.Format == B.Format &&
				   A.bReadOnly == B.bReadOnly &&
				   A.ClearColor == B.ClearColor; 
		}

		bool operator!=(const FRenderPassDesc& Other) const
		{
			return !(*this == Other);
		}
		
		bool operator==(const FRenderPassDesc& Other) const
		{
			if (SampleCount != Other.SampleCount)
			{
				return false;
			}
			if (ViewMask != Other.ViewMask)
			{
				return false;
			}
			if (RenderArea != Other.RenderArea)
			{
				return false;
			}

			if (ColorAttachments.size() != Other.ColorAttachments.size())
			{
				return false;
			}

			const bool DepthValid = DepthAttachment.IsValid();
			const bool OtherDepthValid = Other.DepthAttachment.IsValid();
    
			if (DepthValid != OtherDepthValid)
			{
				return false;
			}

			if (DepthValid && !CompareAttachments(DepthAttachment, Other.DepthAttachment))
			{
				return false;
			}
    
			for (size_t i = 0; i < ColorAttachments.size(); ++i)
			{
				if (!CompareAttachments(ColorAttachments[i], Other.ColorAttachments[i]))
				{
					return false;
				}
			}
    
			return true;
		}
	};

	enum class RUNTIME_API ESamplerReductionType : uint8
	{
		Standard,
		Comparison,
		Minimum,
		Max
	};

	struct RUNTIME_API FSamplerDesc
	{
		FString DebugName;
		FColor BorderColor	= 1.0f;
		float MaxAnisotropy = 1.0f;
		float MipBias		= 0.0f;

		bool MinFilter = true;
		bool MagFilter = true;
		bool MipFilter = true;
		ESamplerAddressMode AddressU 		= ESamplerAddressMode::Clamp;
		ESamplerAddressMode AddressV 		= ESamplerAddressMode::Clamp;
		ESamplerAddressMode AddressW 		= ESamplerAddressMode::Clamp;
		ESamplerReductionType ReductionType = ESamplerReductionType::Standard;

		FSamplerDesc& SetBorderColor(const FColor& color) { BorderColor = color; return *this; }
		FSamplerDesc& SetMaxAnisotropy(float value) { MaxAnisotropy = value; return *this; }
		FSamplerDesc& SetMipBias(float value) { MipBias = value; return *this; }
		FSamplerDesc& SetMinFilter(bool enable) { MinFilter = enable; return *this; }
		FSamplerDesc& SetMagFilter(bool enable) { MagFilter = enable; return *this; }
		FSamplerDesc& SetMipFilter(bool enable) { MipFilter = enable; return *this; }
		FSamplerDesc& SetAllFilters(bool enable) { MinFilter = MagFilter = MipFilter = enable; return *this; }
		FSamplerDesc& SetAddressU(ESamplerAddressMode mode) { AddressU = mode; return *this; }
		FSamplerDesc& SetAddressV(ESamplerAddressMode mode) { AddressV = mode; return *this; }
		FSamplerDesc& SetAddressW(ESamplerAddressMode mode) { AddressW = mode; return *this; }
		FSamplerDesc& SetAllAddressModes(ESamplerAddressMode mode) { AddressU = AddressV = AddressW = mode; return *this; }
		FSamplerDesc& SetReductionType(ESamplerReductionType type) { ReductionType = type; return *this; }
	};

	class RUNTIME_API FRHISampler : public IRHIResource
	{
	public:

		RENDER_RESOURCE(RRT_SamplerState)

		NODISCARD virtual const FSamplerDesc& GetDesc() const = 0;
	};
	
	class RUNTIME_API FRHIImage : public IRHIResource
	{
	public:

		RENDER_RESOURCE(RRT_Image)
		
		virtual const FRHIImageDesc& GetDescription() const = 0;
		virtual const glm::uvec2& GetExtent() const = 0;
		virtual uint32 GetSizeX() const = 0;
		virtual uint32 GetSizeY() const = 0;
		virtual EFormat GetFormat() const = 0;
		virtual TBitFlags<EImageCreateFlags> GetFlags() const = 0;
		virtual uint8 GetNumMips() const = 0;
		virtual int32 GetTextureCacheIndex() const = 0;
		virtual void SetTextureCacheIndex(int32 InIndex) = 0;

		virtual void* GetRHIView(EFormat Format, FTextureSubresourceSet Subresources, EImageDimension Dimension, bool bReadyOnlyDSV = false) = 0;
	};

	class RUNTIME_API FRHIStagingImage : public IRHIResource
	{
	public:
		RENDER_RESOURCE(RRT_Image)

		NODISCARD virtual const FRHIImageDesc& GetDesc() const = 0;
	};
	
	//-------------------------------------------------------------------------------------------------------------------

	class RUNTIME_API FRHIShader : public IRHIResource
	{
	public:

		virtual uint64 GetHashCode() const = 0;
		virtual void GetByteCode(void** ByteCode, uint64* Size) = 0;

		virtual const FShaderHeader& GetShaderHeader() const = 0;
		
	private:
	};

	class RUNTIME_API FRHIVertexShader : public FRHIShader
	{
	public:

		RENDER_RESOURCE(RRT_VertexShader)

	};

	class RUNTIME_API FRHIPixelShader : public FRHIShader
	{
	public:

		RENDER_RESOURCE(RRT_PixelShader)

	};
	
	class RUNTIME_API FRHIComputeShader : public FRHIShader
	{
	public:

		RENDER_RESOURCE(RRT_ComputeShader)

	};

	class RUNTIME_API FRHIGeometryShader : public FRHIShader
	{
	public:

		RENDER_RESOURCE(RTT_GeometryShader)

	};

	class RUNTIME_API FRHIMeshShader : public FRHIShader
	{
	public:

		RENDER_RESOURCE(RRT_MeshShader)

	};

	class RUNTIME_API FRHITaskShader : public FRHIShader
	{
	public:

		RENDER_RESOURCE(RRT_TaskShader)

	};

	class RUNTIME_API FShaderLibrary : public IRHIResource
	{
	public:

		RENDER_RESOURCE(RRT_ShaderLibrary)
		
		using ShaderKey = uint64;

		void CreateAndAddShader(const FName& Path, const FShaderHeader& Header, bool bReloadPipelines);
		void AddShader(const FName& Path, FRHIShader* Shader);
		
		static FRHIVertexShaderRef GetVertexShader(const FName& Path, TSpan<FString> Macros = {});
		static FRHIPixelShaderRef GetPixelShader(const FName& Path, TSpan<FString> Macros = {});
		static FRHIComputeShaderRef GetComputeShader(const FName& Path, TSpan<FString> Macros = {});
		static FRHIGeometryShaderRef GetGeometryShader(const FName& Path, TSpan<FString> Macros = {});
		static FRHIMeshShaderRef GetMeshShader(const FName& Path, TSpan<FString> Macros = {});
		static FRHITaskShaderRef GetTaskShader(const FName& Path, TSpan<FString> Macros = {});

		template<typename T>
		TRefCountPtr<T> GetShader(FName Key, TSpan<FString> Macros = {})
		{
			return GetShader(Key, Macros).As<T>();
		}

		FRHIShaderRef GetShader(const FName& Path, TSpan<FString> Macros = {});

	private:

		FMutex Mutex;
		THashMap<ShaderKey, FRHIShaderRef> Shaders;
    
	};
	
	//-------------------------------------------------------------------------------------------------------------------

	enum class RUNTIME_API ERasterFillMode : uint8
	{
		Solid,
		Wireframe,

		// Vulkan names
		Fill = Solid,
		Line = Wireframe
	};

	enum class RUNTIME_API ERasterCullMode : uint8
	{
		Back,
		Front,
		None
	};

	enum class RUNTIME_API EBlendFactor : uint8
	{
		Zero = 1,
		One = 2,
		SrcColor = 3,
		InvSrcColor = 4,
		SrcAlpha = 5,
		InvSrcAlpha = 6,
		DstAlpha  = 7,
		InvDstAlpha = 8,
		DstColor = 9,
		InvDstColor = 10,
		SrcAlphaSaturate = 11,
		ConstantColor = 14,
		InvConstantColor = 15,
		Src1Color = 16,
		InvSrc1Color = 17,
		Src1Alpha = 18,
		InvSrc1Alpha = 19,

		// Vulkan names
		OneMinusSrcColor = InvSrcColor,
		OneMinusSrcAlpha = InvSrcAlpha,
		OneMinusDstAlpha = InvDstAlpha,
		OneMinusDstColor = InvDstColor,
		OneMinusConstantColor = InvConstantColor,
		OneMinusSrc1Color = InvSrc1Color,
		OneMinusSrc1Alpha = InvSrc1Alpha,
	};
    
	enum class RUNTIME_API EBlendOp : uint8
	{
		Add = 1,
		Subrtact = 2,
		ReverseSubtract = 3,
		Min = 4,
		Max = 5
	};

	enum class RUNTIME_API EColorMask : uint8
	{
		// These values are equal to their counterparts in DX11, DX12, and Vulkan.
		Red = 1,
		Green = 2,
		Blue = 4,
		Alpha = 8,
		All = 0xF
	};

	ENUM_CLASS_FLAGS(EColorMask)

	struct RUNTIME_API FVertexAttributeDesc
	{
		EFormat Format = EFormat::UNKNOWN;
		uint32 ArraySize = 1;
		uint32 BufferIndex = 0;
		uint32 Offset = 0;
		uint32 ElementStride = 0;
		bool bInstanced = false;
		
		constexpr FVertexAttributeDesc& SetArraySize(uint32 value) { ArraySize = value; return *this; }
		constexpr FVertexAttributeDesc& SetBufferIndex(uint32 value) { BufferIndex = value; return *this; }
		constexpr FVertexAttributeDesc& SetOffset(uint32 value) { Offset = value; return *this; }
		constexpr FVertexAttributeDesc& SetElementStride(uint32 value) { ElementStride = value; return *this; }
		constexpr FVertexAttributeDesc& SetIsInstanced(bool value) { bInstanced = value; return *this; }
	};
	
	class RUNTIME_API FRHIInputLayout : public IRHIResource
	{
	public:
		
		NODISCARD virtual uint32 GetNumAttributes() const = 0;
		NODISCARD virtual const FVertexAttributeDesc* GetAttributeDesc(uint32 index) const = 0;
	};
	
	struct RUNTIME_API FBlendState
    {
        struct RenderTarget
        {
            bool			bBlendEnable = false;
            EBlendFactor 	SrcBlend = EBlendFactor::One;
            EBlendFactor 	DestBlend = EBlendFactor::Zero;
            EBlendOp     	BlendOp = EBlendOp::Add;
            EBlendFactor 	SrcBlendAlpha = EBlendFactor::One;
            EBlendFactor 	DestBlendAlpha = EBlendFactor::Zero;
            EBlendOp     	BlendOpAlpha = EBlendOp::Add;
            EColorMask   	ColorWriteMask = EColorMask::All;

            constexpr RenderTarget& SetBlendEnable(bool enable) { bBlendEnable = enable; return *this; }
            constexpr RenderTarget& EnableBlend() { bBlendEnable = true; return *this; }
            constexpr RenderTarget& DisableBlend() { bBlendEnable = false; return *this; }
            constexpr RenderTarget& SetSrcBlend(EBlendFactor value) { SrcBlend = value; return *this; }
            constexpr RenderTarget& SetDestBlend(EBlendFactor value) { DestBlend = value; return *this; }
            constexpr RenderTarget& SetBlendOp(EBlendOp value) { BlendOp = value; return *this; }
            constexpr RenderTarget& SetSrcBlendAlpha(EBlendFactor value) { SrcBlendAlpha = value; return *this; }
            constexpr RenderTarget& SetDestBlendAlpha(EBlendFactor value) { DestBlendAlpha = value; return *this; }
            constexpr RenderTarget& SetBlendOpAlpha(EBlendOp value) { BlendOpAlpha = value; return *this; }
            constexpr RenderTarget& SetColorWriteMask(EColorMask value) { ColorWriteMask = value; return *this; }
        	
            constexpr bool operator ==(const RenderTarget& other) const
            {
                return bBlendEnable == other.bBlendEnable
                    && SrcBlend == other.SrcBlend
                    && DestBlend == other.DestBlend
                    && BlendOp == other.BlendOp
                    && SrcBlendAlpha == other.SrcBlendAlpha
                    && DestBlendAlpha == other.DestBlendAlpha
                    && BlendOpAlpha == other.BlendOpAlpha
                    && ColorWriteMask == other.ColorWriteMask;
            }

            constexpr bool operator !=(const RenderTarget& other) const
            {
                return !(*this == other);
            }
        };

        RenderTarget Targets[MaxRenderTargets];
        bool AlphaToCoverageEnable = false;

        constexpr FBlendState& SetRenderTarget(uint32 index, const RenderTarget& target) { Targets[index] = target; return *this; }
        constexpr FBlendState& SetAlphaToCoverageEnable(bool enable) { AlphaToCoverageEnable = enable; return *this; }
        constexpr FBlendState& EnableAlphaToCoverage() { AlphaToCoverageEnable = true; return *this; }
        constexpr FBlendState& DisableAlphaToCoverage() { AlphaToCoverageEnable = false; return *this; }
		
        constexpr bool operator ==(const FBlendState& other) const
        {
            if (AlphaToCoverageEnable != other.AlphaToCoverageEnable)
            {
	            return false;
            }

            for (uint32_t i = 0; i < MaxRenderTargets; ++i)
            {
                if (Targets[i] != other.Targets[i])
                {
	                return false;
                }
            }

            return true;
        }

        constexpr bool operator !=(const FBlendState& other) const
        {
            return !(*this == other);
        }
    };

	
	//-------------------------------------------------------------------------------------------------------------------

	
	struct RUNTIME_API FRasterState
    {
        ERasterFillMode FillMode = ERasterFillMode::Solid;
        ERasterCullMode CullMode = ERasterCullMode::Back;
        bool FrontCounterClockwise = false;
        bool DepthClipEnable = false;
        bool ScissorEnable = false;
        bool MultisampleEnable = false;
        bool AntialiasedLineEnable = false;
        int DepthBias = 0;
        float DepthBiasClamp = 0.0f;
        float SlopeScaledDepthBias = 0.0f;
		float LineWidth = 1.0f;


        uint8 ForcedSampleCount = 0;
        bool ProgrammableSamplePositionsEnable = false;
        bool ConservativeRasterEnable = false;
        bool QuadFillEnable = false;
        char SamplePositionsX[16]{};
        char SamplePositionsY[16]{};

		constexpr FRasterState& SetLineWidth(float Width) { LineWidth = Width; return *this; } 
        constexpr FRasterState& SetFillMode(ERasterFillMode value) { FillMode = value; return *this; }
        constexpr FRasterState& SetFillSolid() { FillMode = ERasterFillMode::Solid; return *this; }
        constexpr FRasterState& SetFillWireframe() { FillMode = ERasterFillMode::Wireframe; return *this; }
        constexpr FRasterState& SetCullMode(ERasterCullMode value) { CullMode = value; return *this; }
        constexpr FRasterState& SetCullBack() { CullMode = ERasterCullMode::Back; return *this; }
        constexpr FRasterState& SetCullFront() { CullMode = ERasterCullMode::Front; return *this; }
        constexpr FRasterState& SetCullNone() { CullMode = ERasterCullMode::None; return *this; }
        constexpr FRasterState& SetFrontCounterClockwise(bool value) { FrontCounterClockwise = value; return *this; }
        constexpr FRasterState& SetDepthClipEnable(bool value) { DepthClipEnable = value; return *this; }
        constexpr FRasterState& EnableDepthClip() { DepthClipEnable = true; return *this; }
        constexpr FRasterState& DisableDepthClip() { DepthClipEnable = false; return *this; }
        constexpr FRasterState& SetScissorEnable(bool value) { ScissorEnable = value; return *this; }
        constexpr FRasterState& EnableScissor() { ScissorEnable = true; return *this; }
        constexpr FRasterState& DisableScissor() { ScissorEnable = false; return *this; }
        constexpr FRasterState& SetMultisampleEnable(bool value) { MultisampleEnable = value; return *this; }
        constexpr FRasterState& EnableMultisample() { MultisampleEnable = true; return *this; }
        constexpr FRasterState& DisableMultisample() { MultisampleEnable = false; return *this; }
        constexpr FRasterState& SetAntialiasedLineEnable(bool value) { AntialiasedLineEnable = value; return *this; }
        constexpr FRasterState& EnableAntialiasedLine() { AntialiasedLineEnable = true; return *this; }
        constexpr FRasterState& DisableAntialiasedLine() { AntialiasedLineEnable = false; return *this; }
        constexpr FRasterState& SetDepthBias(int value) { DepthBias = value; return *this; }
        constexpr FRasterState& SetDepthBiasClamp(float value) { DepthBiasClamp = value; return *this; }
        constexpr FRasterState& SetSlopeScaleDepthBias(float value) { SlopeScaledDepthBias = value; return *this; }
        constexpr FRasterState& SetForcedSampleCount(uint8_t value) { ForcedSampleCount = value; return *this; }
        constexpr FRasterState& SetProgrammableSamplePositionsEnable(bool value) { ProgrammableSamplePositionsEnable = value; return *this; }
        constexpr FRasterState& EnableProgrammableSamplePositions() { ProgrammableSamplePositionsEnable = true; return *this; }
        constexpr FRasterState& DisableProgrammableSamplePositions() { ProgrammableSamplePositionsEnable = false; return *this; }
        constexpr FRasterState& SetConservativeRasterEnable(bool value) { ConservativeRasterEnable = value; return *this; }
        constexpr FRasterState& EnableConservativeRaster() { ConservativeRasterEnable = true; return *this; }
        constexpr FRasterState& DisableConservativeRaster() { ConservativeRasterEnable = false; return *this; }
        constexpr FRasterState& SetQuadFillEnable(bool value) { QuadFillEnable = value; return *this; }
        constexpr FRasterState& EnableQuadFill() { QuadFillEnable = true; return *this; }
        constexpr FRasterState& DisableQuadFill() { QuadFillEnable = false; return *this; }
        constexpr FRasterState& SetSamplePositions(const char* x, const char* y, int count) { for (int i = 0; i < count; i++) { SamplePositionsX[i] = x[i]; SamplePositionsY[i] = y[i]; } return *this; }
    };

	enum class RUNTIME_API EStencilOp : uint8
	{
		Keep = 1,
		Zero = 2,
		Replace = 3,
		IncrementAndClamp = 4,
		DecrementAndClamp = 5,
		Invert = 6,
		IncrementAndWrap = 7,
		DecrementAndWrap = 8
	};

	enum class RUNTIME_API EComparisonFunc : uint8
	{
		Never = 1,
		Less = 2,
		Equal = 3,
		LessOrEqual = 4,
		Greater = 5,
		NotEqual = 6,
		GreaterOrEqual = 7,
		Always = 8
	};
	
	struct RUNTIME_API FDepthStencilState
    {
        struct StencilOpDesc
        {
            EStencilOp FailOp			= EStencilOp::Keep;
            EStencilOp DepthFailOp		= EStencilOp::Keep;
            EStencilOp PassOp			= EStencilOp::Keep;
            EComparisonFunc StencilFunc = EComparisonFunc::Always;

            constexpr StencilOpDesc& SetFailOp(EStencilOp value) { FailOp = value; return *this; }
            constexpr StencilOpDesc& SetDepthFailOp(EStencilOp value) { DepthFailOp = value; return *this; }
            constexpr StencilOpDesc& SetPassOp(EStencilOp value) { PassOp = value; return *this; }
            constexpr StencilOpDesc& SetStencilFunc(EComparisonFunc value) { StencilFunc = value; return *this; }

            size_t GetHash() const
            {
	            size_t hash = 0;
            	Hash::HashCombine(hash, FailOp);
            	Hash::HashCombine(hash, DepthFailOp);
            	Hash::HashCombine(hash, PassOp);
            	Hash::HashCombine(hash, StencilFunc);
            	return hash;
            }
        };

        bool            	DepthTestEnable = true;
        bool            	DepthWriteEnable = true;
        EComparisonFunc		DepthFunc = EComparisonFunc::Less;
        bool            	StencilEnable = false;
        uint8         		StencilReadMask = 0xff;
        uint8         		StencilWriteMask = 0xff;
        uint8         		StencilRefValue = 0;
        bool            	DynamicStencilRef = false;
        StencilOpDesc   	FrontFaceStencil;
        StencilOpDesc   	BackFaceStencil;

        constexpr FDepthStencilState& SetDepthTestEnable(bool value) { DepthTestEnable = value; return *this; }
        constexpr FDepthStencilState& EnableDepthTest() { DepthTestEnable = true; return *this; }
        constexpr FDepthStencilState& DisableDepthTest() { DepthTestEnable = false; return *this; }
        constexpr FDepthStencilState& SetDepthWriteEnable(bool value) { DepthWriteEnable = value; return *this; }
        constexpr FDepthStencilState& EnableDepthWrite() { DepthWriteEnable = true; return *this; }
        constexpr FDepthStencilState& DisableDepthWrite() { DepthWriteEnable = false; return *this; }
        constexpr FDepthStencilState& SetDepthFunc(EComparisonFunc value) { DepthFunc = value; return *this; }
        constexpr FDepthStencilState& SetStencilEnable(bool value) { StencilEnable = value; return *this; }
        constexpr FDepthStencilState& EnableStencil() { StencilEnable = true; return *this; }
        constexpr FDepthStencilState& DisableStencil() { StencilEnable = false; return *this; }
        constexpr FDepthStencilState& SetStencilReadMask(uint8 value) { StencilReadMask = value; return *this; }
        constexpr FDepthStencilState& SetStencilWriteMask(uint8 value) { StencilWriteMask = value; return *this; }
        constexpr FDepthStencilState& SetStencilRefValue(uint8 value) { StencilRefValue = value; return *this; }
        constexpr FDepthStencilState& SetFrontFaceStencil(const StencilOpDesc& value) { FrontFaceStencil = value; return *this; }
        constexpr FDepthStencilState& SetBackFaceStencil(const StencilOpDesc& value) { BackFaceStencil = value; return *this; }
        constexpr FDepthStencilState& SetDynamicStencilRef(bool value) { DynamicStencilRef = value; return *this; }
        
    };

	struct FBufferRange
	{
		uint64 ByteOffset = 0;
		uint64 ByteSize = 0;
        
		FBufferRange() = default;

		FBufferRange(uint64 _byteOffset, uint64 _byteSize)
			: ByteOffset(_byteOffset)
			, ByteSize(_byteSize)
		{ }

		NODISCARD RUNTIME_API FBufferRange Resolve(const FRHIBufferDesc& Desc) const;
		NODISCARD constexpr bool IsEntireBuffer(const FRHIBufferDesc& desc) const { return (ByteOffset == 0) && (ByteSize == ~0ull || ByteSize == desc.Size); }
		constexpr bool operator == (const FBufferRange& other) const { return ByteOffset == other.ByteOffset && ByteSize == other.ByteSize; }

		constexpr FBufferRange& SetByteOffset(uint64 value) { ByteOffset = value; return *this; }
		constexpr FBufferRange& SetByteSize(uint64 value) { ByteSize = value; return *this; }
	};

	static const FBufferRange EntireBuffer = FBufferRange(0, ~0ull);


	struct RUNTIME_API FSinglePassStereoState
	{
		bool bEnabled = false;
		bool bIndependentViewportMask = false;
		uint16 RenderTargetIndexOffset = 0;

		bool operator ==(const FSinglePassStereoState& b) const
		{
			return bEnabled == b.bEnabled
				&& bIndependentViewportMask == b.bIndependentViewportMask
				&& RenderTargetIndexOffset == b.RenderTargetIndexOffset;
		}

		bool operator !=(const FSinglePassStereoState& b) const { return !(*this == b); }

		constexpr FSinglePassStereoState& SetEnabled(bool value) { bEnabled = value; return *this; }
		constexpr FSinglePassStereoState& SetIndependentViewportMask(bool value) { bIndependentViewportMask = value; return *this; }
		constexpr FSinglePassStereoState& SetRenderTargetIndexOffset(uint16 value) { RenderTargetIndexOffset = value; return *this; }
	};
	
	struct RUNTIME_API FRenderState
	{
		FBlendState				BlendState;
		FDepthStencilState		DepthStencilState;
		FRasterState			RasterState;
		FSinglePassStereoState	SinglePassStereo;

		constexpr FRenderState& SetBlendState(const FBlendState& value) { BlendState = value; return *this; }
		constexpr FRenderState& SetDepthStencilState(const FDepthStencilState& value) { DepthStencilState = value; return *this; }
		constexpr FRenderState& SetRasterState(const FRasterState& value) { RasterState = value; return *this; }
		constexpr FRenderState& SetSinglePassStereoState(const FSinglePassStereoState& value) { SinglePassStereo = value; return *this; }
	};

	struct RUNTIME_API FBindingLayoutItem
	{
		uint32 Slot;
		
		ERHIBindingResourceType Type	: 8;
		uint16 Size						: 16;

		static FBindingLayoutItem Buffer_UD(uint32 Slot, uint16 Size = 0)
		{
			FBindingLayoutItem Result;
			Result.Slot = Slot;
			Result.Type = ERHIBindingResourceType::Buffer_Uniform_Dynamic;
			Result.Size = Size;
			return Result;
		}

		static FBindingLayoutItem Buffer_SD(uint32 Slot, uint16 Size = 0)
		{
			FBindingLayoutItem Result;
			Result.Slot = Slot;
			Result.Type = ERHIBindingResourceType::Buffer_Storage_Dynamic;
			Result.Size = Size;
			return Result;
		}
		
		static FBindingLayoutItem Buffer_SRV(uint32 Slot, uint16 Size = 0)
		{
			FBindingLayoutItem Result;
			Result.Slot = Slot;
			Result.Type = ERHIBindingResourceType::Buffer_SRV;
			Result.Size = Size;
			return Result;
		}

		static FBindingLayoutItem Buffer_UAV(uint32 Slot, uint16 Size = 0)
		{
			FBindingLayoutItem Result;
			Result.Slot = Slot;
			Result.Type = ERHIBindingResourceType::Buffer_UAV;
			Result.Size = Size;
			return Result;
		}

		static FBindingLayoutItem Buffer_CBV(uint32 Slot, uint16 Size = 0)
		{
			FBindingLayoutItem Result;
			Result.Slot = Slot;
			Result.Type = ERHIBindingResourceType::Buffer_CBV;
			Result.Size = Size;
			return Result;
		}

		static FBindingLayoutItem Texture_SRV(uint32 Slot, uint16 Size = 0)
		{
			FBindingLayoutItem Result;
			Result.Slot = Slot;
			Result.Type = ERHIBindingResourceType::Texture_SRV;
			Result.Size = Size;
			return Result;
		}

		static FBindingLayoutItem Texture_UAV(uint32 Slot, uint16 Size = 0)
		{
			FBindingLayoutItem Result;
			Result.Slot = Slot;
			Result.Type = ERHIBindingResourceType::Texture_UAV;
			Result.Size = Size;
			return Result;
		}
		
		bool operator ==(const FBindingLayoutItem& b) const
		{
			return Slot == b.Slot
				&& Type == b.Type
				&& Size == b.Size;
		}
	};

	static_assert(64 % sizeof(FBindingLayoutItem) == 0, "FBindingLayoutItem should match a cache-line.");
	
	
	struct RUNTIME_API FBindingLayoutDesc
	{
		FString DebugName;
		TBitFlags<ERHIShaderType> StageFlags;
		uint32 Index = 0;

		TFixedVector<FBindingLayoutItem, 4> Bindings;

		FBindingLayoutDesc& SetDebugName(FStringView Name) { DebugName = Name; return *this; }
		FBindingLayoutDesc& SetVisibility(ERHIShaderType InType) { StageFlags.SetFlag(InType); return *this; }
		FBindingLayoutDesc& SetBindingIndex(uint32 Value) { Index = Value; return *this; }
		FBindingLayoutDesc& AddItem(const FBindingLayoutItem& Item) { Bindings.push_back(Item); return *this; }
	};

	struct RUNTIME_API FBindlessLayoutDesc
	{
		FString DebugName;
		TBitFlags<ERHIShaderType> StageFlags;
		uint32 FirstSlot = 0;
		uint32 MaxCapacity = 0;
		TFixedVector<FBindingLayoutItem, 4> Bindings;

		FBindlessLayoutDesc& SetVisibility(ERHIShaderType Value) { StageFlags.SetFlag(Value); return *this; }
		FBindlessLayoutDesc& SetFirstSlot(uint32 Value) { FirstSlot = Value; return *this; }
		FBindlessLayoutDesc& SetMaxCapacity(uint32 Value) { MaxCapacity = Value; return *this; }
		FBindlessLayoutDesc& AddBinding(const FBindingLayoutItem& value) { Bindings.push_back(value); return *this; }
	};

	struct RUNTIME_API FBindingTextureResource
	{
		FTextureSubresourceSet	Subresources;
		FRHISampler*			Sampler;
		EImageDimension			Dimension;
		
		bool operator==(const FBindingTextureResource& Other) const
		{
			return Subresources == Other.Subresources && Sampler == Other.Sampler && Dimension == Other.Dimension;
		}
	};
	
	struct RUNTIME_API FBindingSetItem
	{
		using FBindingVariant = TVariant<eastl::monostate, FBindingTextureResource, FBufferRange>;
		
		FBindingVariant				Variant;
		
		IRHIResource*				ResourceHandle	= nullptr;
		uint32						Slot			= 0;
		uint32						ArrayElement	= 0;
		ERHIBindingResourceType		Type			= ERHIBindingResourceType::Unknown;
		EFormat						Format			= EFormat::UNKNOWN;
		
		static_assert(sizeof(FTextureSubresourceSet) == 16, "sizeof(TextureSubresourceSet) is supposed to be 16 bytes");
		static_assert(sizeof(FBufferRange) == 16, "sizeof(BufferRange) is supposed to be 16 bytes");
		
		const FBufferRange& GetBufferRange() const
		{
			return eastl::get<FBufferRange>(Variant);
		}
		
		const FBufferRange* TryGetBufferRange() const
		{
			return eastl::get_if<FBufferRange>(&Variant);
		}
		
		const FBindingTextureResource& GetTextureResource() const
		{
			return eastl::get<FBindingTextureResource>(Variant);
		}
    
		const FBindingTextureResource* TryGetTextureResource() const
		{
			return eastl::get_if<FBindingTextureResource>(&Variant);
		}
		
		static FBindingSetItem BufferSRV(uint32 Slot, FRHIBuffer* Buffer, EFormat Format = EFormat::UNKNOWN, FBufferRange Range = EntireBuffer)
		{
			bool bIsDynamic = Buffer->GetDescription().Usage.IsFlagSet(BUF_Dynamic);
			
			FBindingSetItem Result;
			Result.Type				= bIsDynamic ? ERHIBindingResourceType::Buffer_Storage_Dynamic : ERHIBindingResourceType::Buffer_SRV;
			Result.ResourceHandle	= Buffer;
			Result.Variant			= Range;
			Result.Format			= Format;
			Result.Slot				= Slot;
        
			return Result;
		}

		static FBindingSetItem BufferUAV(uint32 Slot, FRHIBuffer* Buffer, EFormat Format = EFormat::UNKNOWN, FBufferRange Range = EntireBuffer)
		{
			bool bIsDynamic = Buffer->GetDescription().Usage.IsFlagSet(BUF_Dynamic);
			
			FBindingSetItem Result;
			Result.Type				= bIsDynamic ? ERHIBindingResourceType::Buffer_Storage_Dynamic : ERHIBindingResourceType::Buffer_UAV;
			Result.ResourceHandle	= Buffer;
			Result.Variant			= Range;
			Result.Format			= Format;
			Result.Slot				= Slot;
        
			return Result;
		}

		static FBindingSetItem BufferCBV(uint32 Slot, FRHIBuffer* Buffer, EFormat Format = EFormat::UNKNOWN, FBufferRange Range = EntireBuffer)
		{
			bool bIsDynamic = Buffer->GetDescription().Usage.IsFlagSet(BUF_Dynamic);
			
			FBindingSetItem Result;
			Result.Type				= bIsDynamic ? ERHIBindingResourceType::Buffer_Storage_Dynamic : ERHIBindingResourceType::Buffer_CBV;
			Result.ResourceHandle	= Buffer;
			Result.Variant			= Range;
			Result.Format			= Format;
			Result.Slot				= Slot;
        
			return Result;
		}

		static FBindingSetItem TextureSRV(uint32 Slot, FRHIImage* Image, FRHISampler* Sampler = nullptr, EFormat Format = EFormat::UNKNOWN, 
			FTextureSubresourceSet Subresources = AllSubresources, EImageDimension Dimension = EImageDimension::Unknown)
		{
			FBindingSetItem Result;
			Result.Slot = Slot;
			Result.Type = ERHIBindingResourceType::Texture_SRV;
			Result.ResourceHandle = Image;
			Result.Format = Format;
			Result.Variant = FBindingTextureResource{Subresources, Sampler, Dimension};

			return Result;
		}

		
		static FBindingSetItem TextureUAV(uint32 Slot, FRHIImage* Image, EFormat Format = EFormat::UNKNOWN, FTextureSubresourceSet Subresources =
			FTextureSubresourceSet(0, 1, 0, FTextureSubresourceSet::AllArraySlices), EImageDimension Dimension = EImageDimension::Unknown)
		{
			FBindingSetItem Result;
			Result.Slot = Slot;
			Result.Type = ERHIBindingResourceType::Texture_UAV;
			Result.ResourceHandle = Image;
			Result.Format = Format;
			Result.Variant = FBindingTextureResource{Subresources, nullptr, Dimension};
			
			return Result;
		}
		
		bool operator == (const FBindingSetItem& b) const
		{
			return ResourceHandle == b.ResourceHandle
				&& Slot == b.Slot
				&& Type == b.Type
				&& Format == b.Format
				&& Variant == b.Variant;
		}

	};
	
	struct RUNTIME_API FBindingSetDesc
	{
		FString DebugName;
		TFixedVector<FBindingSetItem, 1> Bindings;
		
		bool operator ==(const FBindingSetDesc& b) const
		{
			if (Bindings.size() != b.Bindings.size())
			{
				return false;
			}

			for (size_t i = 0; i < Bindings.size(); ++i)
			{
				if (Bindings[i] != b.Bindings[i])
				{
					return false;
				}
			}

			return true;
		}

		bool operator !=(const FBindingSetDesc& b) const
		{
			return !(*this == b);
		}

		FBindingSetDesc& SetDebugName(FStringView Name) { DebugName = Name; return *this; }
		FBindingSetDesc& AddItem(const FBindingSetItem& value) { Bindings.push_back(value); return *this; }
	};

	
	class RUNTIME_API FRHIBindingLayout : public IRHIResource
	{
	public:
		
		NODISCARD virtual const FBindingLayoutDesc* GetDesc() const = 0;
		NODISCARD virtual const FBindlessLayoutDesc* GetBindlessDesc() const = 0;

	};

	class RUNTIME_API FRHIBindingSet : public IRHIResource
	{
	public:
		
		NODISCARD virtual const FBindingSetDesc* GetDesc() const = 0;
		NODISCARD virtual FRHIBindingLayout* GetLayout() const = 0;
		
	};
	
	class RUNTIME_API FRHIDescriptorTable : public FRHIBindingSet
	{
	public:
		
		NODISCARD virtual uint32_t GetCapacity() const = 0;
		NODISCARD virtual uint32_t GetFirstDescriptorIndexInHeap() const = 0;
	};

	enum class RUNTIME_API EVariableShadingRate : uint8
	{
		e1x1,
		e1x2,
		e2x1,
		e2x2,
		e2x4,
		e4x2,
		e4x4
	};

	enum class RUNTIME_API EShadingRateCombiner : uint8
	{
		Passthrough,
		Override,
		Min,
		Max,
		ApplyRelative
	};

	struct RUNTIME_API FVariableRateShadingState
	{
		bool bEnabled = false;
		EVariableShadingRate ShadingRate = EVariableShadingRate::e1x1;
		EShadingRateCombiner PipelinePrimitiveCombiner = EShadingRateCombiner::Passthrough;
		EShadingRateCombiner ImageCombiner = EShadingRateCombiner::Passthrough;

		bool operator ==(const FVariableRateShadingState& b) const
		{
			return bEnabled == b.bEnabled
				&& ShadingRate == b.ShadingRate
				&& PipelinePrimitiveCombiner == b.PipelinePrimitiveCombiner
				&& ImageCombiner == b.ImageCombiner;
		}

		bool operator !=(const FVariableRateShadingState& b) const { return !(*this == b); }

		constexpr FVariableRateShadingState& SetEnabled(bool value) { bEnabled = value; return *this; }
		constexpr FVariableRateShadingState& SetShadingRate(EVariableShadingRate value) { ShadingRate = value; return *this; }
		constexpr FVariableRateShadingState& SetPipelinePrimitiveCombiner(EShadingRateCombiner value) { PipelinePrimitiveCombiner = value; return *this; }
		constexpr FVariableRateShadingState& SetImageCombiner(EShadingRateCombiner value) { ImageCombiner = value; return *this; }
	};
	
    struct FGraphicsPipelineDesc
    {
        FRenderState							RenderState;
        TFixedVector<FRHIBindingLayoutRef, 1>	BindingLayouts;
    	FString									DebugName;
        FRHIInputLayoutRef						InputLayout;
        FRHIVertexShaderRef						VS;
        FRHIPixelShaderRef						PS;
    	FRHIGeometryShaderRef					GS;
        FRHIMeshShaderRef						MS;
        FRHITaskShaderRef						TS;
        FVariableRateShadingState				ShadingRateState;

        EPrimitiveType							PrimType = EPrimitiveType::TriangleList;

		FORCEINLINE bool IsMeshShaderPipeline() const { return MS != nullptr; }

		FORCEINLINE FGraphicsPipelineDesc& SetDebugName(const FString& InDebugName) { DebugName = Move(InDebugName); return *this; }
        FORCEINLINE FGraphicsPipelineDesc& SetPrimType(EPrimitiveType value) { PrimType = value; return *this; }
        FORCEINLINE FGraphicsPipelineDesc& SetInputLayout(FRHIInputLayout* value) { InputLayout = value; return *this; }
    	FORCEINLINE FGraphicsPipelineDesc& SetVertexShader(FRHIVertexShader* value) { VS = value; return *this; }
        FORCEINLINE FGraphicsPipelineDesc& SetPixelShader(FRHIPixelShader* value) { PS = value; return *this; }
    	FORCEINLINE FGraphicsPipelineDesc& SetGeometryShader(FRHIGeometryShader* value) { GS = value; return *this; }
        FORCEINLINE FGraphicsPipelineDesc& SetMeshShader(FRHIMeshShader* value) { MS = value; return *this; }
        FORCEINLINE FGraphicsPipelineDesc& SetTaskShader(FRHITaskShader* value) { TS = value; return *this; }
        FORCEINLINE FGraphicsPipelineDesc& SetFragmentShader(FRHIPixelShader* value) { PS = value; return *this; }
        FORCEINLINE FGraphicsPipelineDesc& SetRenderState(const FRenderState& value) { RenderState = value; return *this; }
        FORCEINLINE FGraphicsPipelineDesc& SetVariableRateShadingState(const FVariableRateShadingState& value) { ShadingRateState = value; return *this; }
        FORCEINLINE FGraphicsPipelineDesc& AddBindingLayout(FRHIBindingLayout* layout) { BindingLayouts.push_back(layout); return *this; }
    };

	struct RUNTIME_API FGraphicsState
	{
		FRHIGraphicsPipeline* Pipeline = nullptr;
		FRenderPassDesc RenderPass = {};
		FViewportState ViewportState;
		TFixedVector<FRHIBindingSet*, 1> Bindings;
        
		TFixedVector<FVertexBufferBinding, 1> VertexBuffers;
		FIndexBufferBinding IndexBuffer;

		FRHIBuffer* IndirectParams = nullptr;
		FRHIBuffer* CountBuffer = nullptr;

		FORCEINLINE FGraphicsState& SetRenderPass(FRenderPassDesc&& value) { RenderPass = Move(value); return *this; }
		FORCEINLINE FGraphicsState& SetViewportState(FViewportState&& value) { ViewportState = Move(value); return *this; }

		
		FORCEINLINE FGraphicsState& SetRenderPass(const FRenderPassDesc& value) { RenderPass = value; return *this; }
		FORCEINLINE FGraphicsState& SetPipeline(FRHIGraphicsPipeline* value) { Pipeline = value; return *this; }
		FORCEINLINE FGraphicsState& AddViewport(const FViewport& Viewport) { ViewportState.Viewports.push_back(Viewport); return *this; }
		FORCEINLINE FGraphicsState& AddScissor(const FRect& Scissor) { ViewportState.Scissors.push_back(Scissor); return *this; }
		FORCEINLINE FGraphicsState& SetViewportState(const FViewportState& value) { ViewportState = Move(value); return *this; }
		FORCEINLINE FGraphicsState& AddBindingSet(FRHIBindingSet* value) { Bindings.push_back(value); return *this; }
		FORCEINLINE FGraphicsState& AddVertexBuffer(const FVertexBufferBinding& value) { VertexBuffers.push_back(value); return *this; }
		FORCEINLINE FGraphicsState& SetVertexBuffer(const FVertexBufferBinding& value)
		{
			if (VertexBuffers.empty())
			{
				VertexBuffers.push_back(value);
			}
			else
			{
				VertexBuffers[0] = value;
			}

			return *this;
		}
		FORCEINLINE FGraphicsState& SetIndexBuffer(const FIndexBufferBinding& value) { IndexBuffer = value; return *this; }
		FORCEINLINE FGraphicsState& SetIndirectParams(FRHIBuffer* value) { IndirectParams = value; return *this; }
		FORCEINLINE FGraphicsState& SetIndirectCount(FRHIBuffer* Value) { CountBuffer = Value; return *this; }
	};

	struct RUNTIME_API FComputeState
	{
		FRHIComputePipeline* Pipeline = nullptr;
		TFixedVector<FRHIBindingSet*, 1> Bindings;

		FRHIBuffer* IndirectParams = nullptr;

		FORCEINLINE FComputeState& SetPipeline(FRHIComputePipeline* value) { Pipeline = value; return *this; }
		FORCEINLINE FComputeState& AddBindingSet(FRHIBindingSet* value) { Bindings.push_back(value); return *this; }
		FORCEINLINE FComputeState& SetIndirectParams(FRHIBuffer* value) { IndirectParams = value; return *this; }
        
	};

	
	class RUNTIME_API FRHIGraphicsPipeline : public IRHIResource
	{
	public:

		RENDER_RESOURCE(RRT_GraphicsPipeline)

		NODISCARD virtual const FGraphicsPipelineDesc& GetDesc() const = 0;
		
	};

	
	//-------------------------------------------------------------------------------------------------------------------


	struct RUNTIME_API FComputePipelineDesc
	{
		FString									DebugName;
		FRHIComputeShaderRef					CS;
		TFixedVector<FRHIBindingLayoutRef, 1>	BindingLayouts;

		FComputePipelineDesc& SetComputeShader(FRHIComputeShader* value) { CS = value; return *this; }
		FComputePipelineDesc& AddBindingLayout(FRHIBindingLayout* layout) { BindingLayouts.push_back(layout); return *this; }
	};
	
	class RUNTIME_API FRHIComputePipeline : public IRHIResource
	{
	public:

		RENDER_RESOURCE(RRT_ComputePipeline)

		NODISCARD virtual const FComputePipelineDesc& GetDesc() const = 0;
		
	};
	
}


namespace eastl
{
	using namespace Lumina;

	template<>
	struct hash<FVertexAttributeDesc>
	{
		size_t operator()(const FVertexAttributeDesc& Item) const
		{
			size_t hash = 0;
			Hash::HashCombine(hash, Item.ArraySize);
			Hash::HashCombine(hash, Item.bInstanced);
			Hash::HashCombine(hash, Item.BufferIndex);
			Hash::HashCombine(hash, Item.ElementStride);
			Hash::HashCombine(hash, (uint32)Item.Format);
			Hash::HashCombine(hash, Item.Offset);

			return hash;
		}
	};

	template<>
	struct hash<FTextureSubresourceSet>
	{
		size_t operator()(const FTextureSubresourceSet& Item) const
		{
			size_t Hash = 0;
			Hash::HashCombine(Hash, Item.BaseArraySlice);
			Hash::HashCombine(Hash, Item.NumArraySlices);
			Hash::HashCombine(Hash, Item.BaseMipLevel);
			Hash::HashCombine(Hash, Item.NumMipLevels);

			return Hash;
		}
	};

	template<>
	struct hash<FRenderPassDesc::FAttachment>
	{
		size_t operator()(const FRenderPassDesc::FAttachment& Item) const
		{
			size_t Hash = 0;
			Hash::HashCombine(Hash, Item.bReadOnly);
			Hash::HashCombine(Hash, Item.ClearColor);
			Hash::HashCombine(Hash, Item.Format);
			Hash::HashCombine(Hash, Item.Image);
			Hash::HashCombine(Hash, Item.LoadOp);
			Hash::HashCombine(Hash, Item.ResolveImage);
			Hash::HashCombine(Hash, Item.StoreOp);
			Hash::HashCombine(Hash, Item.Subresources);
			return Hash;
		}
	};

	template<>
	struct hash<FRenderPassDesc>
	{
		size_t operator()(const FRenderPassDesc& Item) const
		{
			
			size_t Hash = 0;
			Hash::HashCombine(Hash, Item.ViewMask);
			Hash::HashCombine(Hash, Item.SampleCount);
			Hash::HashCombine(Hash, Item.RenderArea);
			for (const FRenderPassDesc::FAttachment& Attachment : Item.ColorAttachments)
			{
				Hash::HashCombine(Hash, Attachment);
			}
			Hash::HashCombine(Hash, Item.DepthAttachment);
			return Hash;
		}
	};
	
	template<>
	struct hash<FBindingSetItem>
	{
		size_t operator()(const FBindingSetItem& Item) const
		{
			size_t hash = 0;
			Hash::HashCombine(hash, Item.ResourceHandle);
			Hash::HashCombine(hash, Item.Slot);
			Hash::HashCombine(hash, Item.Type);
			
			if (const FBufferRange* Range = Item.TryGetBufferRange())
			{
				Hash::HashCombine(hash, Range->ByteSize);
				Hash::HashCombine(hash, Range->ByteOffset);
			}
			else if (const FBindingTextureResource* Texture = Item.TryGetTextureResource())
			{
				Hash::HashCombine(hash, Texture->Sampler);
				Hash::HashCombine(hash, Texture->Subresources);
				Hash::HashCombine(hash, Texture->Dimension);
			}
			
			return hash;
		}
	};

	template<> 
	struct hash<FSamplerDesc> 
	{ 
		size_t operator()(const FSamplerDesc& Item) const 
		{ 
			size_t hash = 0;
			Hash::HashCombine(hash, Item.MaxAnisotropy);
			Hash::HashCombine(hash, Item.MipBias);
			Hash::HashCombine(hash, Item.MinFilter);
			Hash::HashCombine(hash, Item.MagFilter);
			Hash::HashCombine(hash, Item.MipFilter);
			Hash::HashCombine(hash, static_cast<int>(Item.AddressU));
			Hash::HashCombine(hash, static_cast<int>(Item.AddressV));
			Hash::HashCombine(hash, static_cast<int>(Item.AddressW));
			Hash::HashCombine(hash, static_cast<int>(Item.ReductionType));
			return hash; 
		} 
	};

	template<>
	struct hash<FBindingLayoutItem>
	{
		size_t operator()(const FBindingLayoutItem& Item) const
		{
			size_t Hash = 0;
			Hash::HashCombine(Hash, Item.Size);
			Hash::HashCombine(Hash, Item.Slot);
			Hash::HashCombine(Hash, Item.Type);

			return Hash;
		}
	};

	template<>
	struct hash<FBindingLayoutDesc>
	{
		size_t operator()(const FBindingLayoutDesc& Item) const
		{
			size_t Hash = 0;
			Hash::HashCombine(Hash, Item.Index);
			Hash::HashCombine(Hash, Item.StageFlags.Get());
			for (const FBindingLayoutItem& LayoutItem : Item.Bindings)
			{
				Hash::HashCombine(Hash, LayoutItem);
			}

			return Hash;
		}
	};

	template<>
	struct hash<FBindingSetDesc>
	{
		size_t operator()(const FBindingSetDesc& Item) const
		{
			size_t hash = 0;
			for (const FBindingSetItem& LayoutItem : Item.Bindings)
			{
				Hash::HashCombine(hash, LayoutItem);
			}

			return hash;
		}
	};

	template<>
	struct hash<FBlendState>
	{
		size_t operator()(const FBlendState& State) const
		{
			size_t hash = 0;
			Hash::HashCombine(hash, State.AlphaToCoverageEnable);

			for (uint32_t i = 0; i < MaxRenderTargets; ++i)
			{
				Hash::HashCombine(hash, State.Targets[i].bBlendEnable);
				Hash::HashCombine(hash, (State.Targets[i].SrcBlend));
				Hash::HashCombine(hash, (State.Targets[i].DestBlend));
				Hash::HashCombine(hash, (State.Targets[i].BlendOp));
				Hash::HashCombine(hash, (State.Targets[i].SrcBlendAlpha));
				Hash::HashCombine(hash, (State.Targets[i].DestBlendAlpha));
				Hash::HashCombine(hash, (State.Targets[i].BlendOpAlpha));
				Hash::HashCombine(hash, (State.Targets[i].ColorWriteMask));
			}

			return hash;
		}
	};


	template<>
	struct hash<FSinglePassStereoState>
	{
		size_t operator()(const FSinglePassStereoState& State) const
		{
			size_t hash = 0;
            
			Hash::HashCombine(hash, State.bEnabled);
			Hash::HashCombine(hash, State.bIndependentViewportMask);
			Hash::HashCombine(hash, State.RenderTargetIndexOffset);

			return hash;
		}
	};

	
	template<>
	struct hash<FRasterState>
	{
		size_t operator()(const FRasterState& State) const
		{
			size_t hash = 0;
            
			Hash::HashCombine(hash, static_cast<uint32>(State.FillMode));
			Hash::HashCombine(hash, static_cast<uint32>(State.CullMode));
			Hash::HashCombine(hash, State.FrontCounterClockwise);
			Hash::HashCombine(hash, State.DepthClipEnable);
			Hash::HashCombine(hash, State.ScissorEnable);
			Hash::HashCombine(hash, State.MultisampleEnable);
			Hash::HashCombine(hash, State.AntialiasedLineEnable);
			Hash::HashCombine(hash, State.DepthBias);
			Hash::HashCombine(hash, State.DepthBiasClamp);
			Hash::HashCombine(hash, State.SlopeScaledDepthBias);
			Hash::HashCombine(hash, State.ForcedSampleCount);
			Hash::HashCombine(hash, State.ProgrammableSamplePositionsEnable);
			Hash::HashCombine(hash, State.ConservativeRasterEnable);
			Hash::HashCombine(hash, State.QuadFillEnable);
			Hash::HashCombine(hash, State.LineWidth);

			for (size_t i = 0; i < 16; ++i)
			{
				Hash::HashCombine(hash, State.SamplePositionsX[i]);
				Hash::HashCombine(hash, State.SamplePositionsY[i]);
			}

			return hash;
		}
	};
	
	template<>
	struct hash<FRenderState>
	{
		size_t operator()(const FRenderState& State) const
		{
			size_t hash = 0;
			Hash::HashCombine(hash, State.BlendState);
			Hash::HashCombine(hash, State.DepthStencilState);
			Hash::HashCombine(hash, State.RasterState);
			Hash::HashCombine(hash, State.SinglePassStereo);
			return hash;
		}
	};
	
	
	template<>
	struct hash<FVariableRateShadingState>
	{
		size_t operator()(const FVariableRateShadingState& State) const
		{
			size_t hash = 0;
			Hash::HashCombine(hash, State.bEnabled);
			Hash::HashCombine(hash, static_cast<uint32>(State.ShadingRate));
			Hash::HashCombine(hash, static_cast<uint32>(State.PipelinePrimitiveCombiner));
			Hash::HashCombine(hash, static_cast<uint32>(State.ImageCombiner));
			return hash;
		}
	};

	template<>
	struct hash<FDepthStencilState>
	{
		size_t operator()(const FDepthStencilState& State) const
		{
			size_t hash = 0;
			Hash::HashCombine(hash, (uint32)State.DepthFunc);
			Hash::HashCombine(hash, State.StencilEnable);
			Hash::HashCombine(hash, State.BackFaceStencil.GetHash());
			Hash::HashCombine(hash, State.DepthTestEnable);
			Hash::HashCombine(hash, State.DepthWriteEnable);
			Hash::HashCombine(hash, State.StencilReadMask);
			Hash::HashCombine(hash, State.StencilRefValue);
			Hash::HashCombine(hash, State.StencilWriteMask);
			return hash;
		}
	};

	template<>
	struct hash<FGraphicsPipelineDesc>
	{
		size_t operator()(const FGraphicsPipelineDesc& Desc) const
		{
			size_t hash = 0;
			Hash::HashCombine(hash, Desc.RenderState);
			Hash::HashCombine(hash, (uint32)Desc.PrimType);
			Hash::HashCombine(hash, Desc.ShadingRateState);
			Hash::HashCombine(hash, Desc.VS ? Desc.VS->GetHashCode() : 0);
			Hash::HashCombine(hash, Desc.PS ? Desc.PS->GetHashCode() : 0);
			Hash::HashCombine(hash, Desc.GS ? Desc.GS->GetHashCode() : 0);
			Hash::HashCombine(hash, Desc.MS ? Desc.MS->GetHashCode() : 0);
			Hash::HashCombine(hash, Desc.TS ? Desc.TS->GetHashCode() : 0);
			Hash::HashCombine(hash, Desc.InputLayout.GetReference());

			return hash;
		}
	};

	template<>
	struct hash<FComputePipelineDesc>
	{
		size_t operator()(const FComputePipelineDesc& Desc) const
		{
			size_t hash = 0;
			Hash::HashCombine(hash, Desc.CS->GetHashCode());
			return hash;
		}
	};
}