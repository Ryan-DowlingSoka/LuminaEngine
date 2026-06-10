#include "pch.h"
#include "RHICore.h"
#include "Log/Log.h"

#include "RenderResource.h"
#include "RHITexture.h"
#include "ShaderLibrary.h"
#include "Core/Math/Math.h"
#include "Core/Threading/Atomic.h"
#include "Core/Threading/Thread.h"

#ifdef CreateSemaphore
#undef CreateSemaphore
#endif

namespace Lumina::RHI::Core
{
    // 32 MiB per frame slot; asserts on overflow.
    static constexpr uint64 kTransientSliceSize = 32ull * 1024 * 1024;

    struct FTransientSlice
    {
        GPUPtr          Gpu = 0;
        std::byte*      Cpu = nullptr;
        TAtomic<uint64> Cursor{0};
    };

    struct FPendingFree
    {
        GPUPtr Memory;
        uint32 TicksRemaining;
    };

    struct FCoreState
    {
        FTextureHeapH       GlobalHeap;
        FSemaphoreH         FrameTimeline;
        uint64              TimelineCounter = 0;
        uint64              SlotWaitValue[kFramesInFlight] = {};
        TVector<FCmdListH>  SlotCommandLists[kFramesInFlight];
        FTransientSlice     Slices[kFramesInFlight];
        uint32              CurrentSlot = 0;
        FMutex              SubmitMutex;

        FSemaphoreH         UploadSemaphore;
        uint64              UploadCounter = 0;
        FMutex              UploadMutex;

        TVector<FPendingFree> PendingFrees;
        FMutex                PendingFreeMutex;

        bool                bInitialized = false;
    };

    static FCoreState GCore;

    void Initialize()
    {
        GCore.GlobalHeap      = CreateTextureHeap(8192, 1024, 64);
        GCore.FrameTimeline   = CreateSemaphore(0);
        GCore.UploadSemaphore = CreateSemaphore(0);

        for (FTransientSlice& Slice : GCore.Slices)
        {
            Slice.Gpu = Malloc(kTransientSliceSize, kDefaultAlign, EMemoryType::CPUWrite);
            Slice.Cpu = static_cast<std::byte*>(ToHost(Slice.Gpu));
            Slice.Cursor.store(0, std::memory_order_relaxed);
        }

        // Stock samplers. Registration order must match EStockSampler and the
        // SAMPLER_* constants in GlobalRHI.slang.
        auto AddSampler = [](EStockSampler Expected, const FSamplerDesc& Desc)
        {
            const uint32 Slot = HeapWriteSampler(GCore.GlobalHeap, Desc);
            ASSERT(Slot == (uint32)Expected, "Stock sampler slot mismatch");
        };

        FSamplerDesc Linear{};

        FSamplerDesc Desc = Linear;
        AddSampler(EStockSampler::LinearWrap, Desc);

        Desc = Linear;
        Desc.AddressU = Desc.AddressV = Desc.AddressW = EAddressMode::ClampToEdge;
        AddSampler(EStockSampler::LinearClamp, Desc);

        Desc = Linear;
        Desc.AddressU = Desc.AddressV = Desc.AddressW = EAddressMode::MirroredRepeat;
        AddSampler(EStockSampler::LinearMirror, Desc);

        Desc = Linear;
        Desc.MinFilter = Desc.MagFilter = Desc.MipFilter = EFilter::Nearest;
        AddSampler(EStockSampler::PointWrap, Desc);

        Desc.AddressU = Desc.AddressV = Desc.AddressW = EAddressMode::ClampToEdge;
        AddSampler(EStockSampler::PointClamp, Desc);

        Desc = Linear;
        Desc.MaxAnisotropy = 16.0f;
        AddSampler(EStockSampler::AnisoWrap, Desc);

        Desc.AddressU = Desc.AddressV = Desc.AddressW = EAddressMode::ClampToEdge;
        AddSampler(EStockSampler::AnisoClamp, Desc);

        Desc = Linear;
        Desc.AddressU = Desc.AddressV = Desc.AddressW = EAddressMode::ClampToEdge;
        Desc.CompareOp = EOp::Less;
        AddSampler(EStockSampler::Shadow, Desc);

        Desc = Linear;
        Desc.AddressU = Desc.AddressV = Desc.AddressW = EAddressMode::ClampToEdge;
        Desc.Reduction = EReduction::Min;
        AddSampler(EStockSampler::MinReduction, Desc);

        Desc.Reduction = EReduction::Max;
        AddSampler(EStockSampler::MaxReduction, Desc);

        GCore.bInitialized = true;

        Textures::Initialize();
    }

    void Shutdown()
    {
        if (!GCore.bInitialized)
        {
            return;
        }

        WaitDeviceIdle();

        Textures::Shutdown();

        // Device is idle: flush every deferred buffer free immediately.
        {
            FScopeLock Lock(GCore.PendingFreeMutex);
            for (const FPendingFree& Pending : GCore.PendingFrees)
            {
                RHI::Free(Pending.Memory);
            }
            GCore.PendingFrees.clear();
        }

        for (FTransientSlice& Slice : GCore.Slices)
        {
            Free(Slice.Gpu);
            Slice.Gpu = 0;
            Slice.Cpu = nullptr;
            Slice.Cursor.store(0, std::memory_order_relaxed);
        }

        for (TVector<FCmdListH>& Lists : GCore.SlotCommandLists)
        {
            Lists.clear();
        }

        FreeH(GCore.UploadSemaphore);
        FreeH(GCore.FrameTimeline);
        FreeH(GCore.GlobalHeap);
        GCore.bInitialized = false;
    }

    void Upload(GPUPtr Dest, const void* Data, uint64 Size)
    {
        if (Dest == 0 || Data == nullptr || Size == 0)
        {
            return;
        }

        // Host-visible destination.
        if (void* Mapped = ToHost(Dest))
        {
            Memory::Memcpy(Mapped, Data, Size);
            return;
        }

        // Small aligned writes ride the command list directly; only large ones stage.
        const bool bInline = Size <= kMaxInlineWrite && (Size & 3) == 0 && (Dest & 3) == 0;
        const GPUPtr Staging = bInline ? 0 : Malloc(Size, kDefaultAlign, EMemoryType::CPUWrite);
        if (!bInline)
        {
            Memory::Memcpy(ToHost(Staging), Data, Size);
        }

        FScopeLock Lock(GCore.UploadMutex);

        const FCmdListH CL = OpenCommandList(EQueueType::Graphics);
        if (bInline)
        {
            CmdWriteMemory(CL, Dest, Data, Size);
        }
        else
        {
            CmdMemcpy(CL, Dest, Staging, Size);
        }

        const uint64 Value = ++GCore.UploadCounter;
        const FSemaphoreInfo Signal { GCore.UploadSemaphore, Value, EStageFlags::AllCommands };
        RHI::Submit(EQueueType::Graphics, TSpan{&CL, 1}, {}, TSpan{&Signal, 1});

        WaitSemaphore(GCore.UploadSemaphore, Value);
        ResetCommandList(CL);

        if (Staging != 0)
        {
            Free(Staging);
        }
    }

    void DeferredFree(GPUPtr Memory)
    {
        if (Memory == 0)
        {
            return;
        }

        if (!GCore.bInitialized)
        {
            RHI::Free(Memory);
            return;
        }

        FScopeLock Lock(GCore.PendingFreeMutex);
        GCore.PendingFrees.push_back(FPendingFree{ Memory, kFramesInFlight });
    }

    void BeginFrame(uint32 SlotIndex)
    {
        if (!GCore.bInitialized)
        {
            return;
        }

        const uint32 Slot = SlotIndex % kFramesInFlight;

        if (GCore.SlotWaitValue[Slot] != 0)
        {
            WaitSemaphore(GCore.FrameTimeline, GCore.SlotWaitValue[Slot]);
        }

        Textures::Tick();
        RHI::TickFrame();

        {
            FScopeLock Lock(GCore.PendingFreeMutex);
            for (size_t i = 0; i < GCore.PendingFrees.size(); )
            {
                FPendingFree& Pending = GCore.PendingFrees[i];
                if (Pending.TicksRemaining > 0)
                {
                    --Pending.TicksRemaining;
                    ++i;
                    continue;
                }

                RHI::Free(Pending.Memory);
                GCore.PendingFrees[i] = GCore.PendingFrees.back();
                GCore.PendingFrees.pop_back();
            }
        }

        for (FCmdListH CommandList : GCore.SlotCommandLists[Slot])
        {
            ResetCommandList(CommandList);
        }
        GCore.SlotCommandLists[Slot].clear();

        GCore.Slices[Slot].Cursor.store(0, std::memory_order_relaxed);
        GCore.CurrentSlot = Slot;
    }

    void Submit(FCmdListH CommandList)
    {
        FScopeLock Lock(GCore.SubmitMutex);

        const uint64 Value = ++GCore.TimelineCounter;

        const FSemaphoreInfo Signal { GCore.FrameTimeline, Value, EStageFlags::AllCommands };
        RHI::Submit(EQueueType::Graphics, TSpan{&CommandList, 1}, {}, TSpan{&Signal, 1});

        GCore.SlotWaitValue[GCore.CurrentSlot] = Value;
        GCore.SlotCommandLists[GCore.CurrentSlot].push_back(CommandList);
    }

    bool Present(FSwapchainH Swapchain, FCmdListH CommandList)
    {
        FScopeLock Lock(GCore.SubmitMutex);

        const uint64 Value = ++GCore.TimelineCounter;

        // PresentSwapchain submits CommandList (wait acquire, signal present + this frame value), presents.
        const bool bOk = PresentSwapchain(Swapchain, CommandList, GCore.FrameTimeline, Value);

        GCore.SlotWaitValue[GCore.CurrentSlot] = Value;
        GCore.SlotCommandLists[GCore.CurrentSlot].push_back(CommandList);
        return bOk;
    }

    FTextureHeapH GetGlobalHeap()
    {
        return GCore.GlobalHeap;
    }

    FTransientAlloc AllocTransient(uint64 Size, uint64 Alignment)
    {
        FTransientSlice& Slice = GCore.Slices[GCore.CurrentSlot];

        const uint64 Padded = Size + Alignment;
        const uint64 RawOffset = Slice.Cursor.fetch_add(Padded, std::memory_order_relaxed);
        ASSERT(RawOffset + Padded <= kTransientSliceSize, "Transient ring overflow; raise kTransientSliceSize");

        const uint64 AlignedGpu = Math::AlignUp(Slice.Gpu + RawOffset, Alignment);
        const uint64 Skew = AlignedGpu - (Slice.Gpu + RawOffset);

        return FTransientAlloc
        {
            .Cpu = Slice.Cpu + RawOffset + Skew,
            .Gpu = AlignedGpu
        };
    }

    FPipelineH CreateGraphicsPipeline(const FName& VertexShader, const FName& PixelShader, const FRasterDesc& Desc)
    {
        const FShaderEntry* Vertex = FShaderLibrary::Get(VertexShader);
        const FShaderEntry* Pixel  = FShaderLibrary::Get(PixelShader);

        if (!Vertex->IsValid() || !Pixel->IsValid())
        {
            LOG_ERROR("RHICore: missing shaders for pipeline ({} / {})", VertexShader.c_str(), PixelShader.c_str());
            return {};
        }

        return RHI::CreateGraphicsPipeline(Vertex->Source(), Pixel->Source(), Desc);
    }

    FPipelineH CreateComputePipeline(const FName& ComputeShader)
    {
        const FShaderEntry* Compute = FShaderLibrary::Get(ComputeShader);

        if (!Compute->IsValid())
        {
            LOG_ERROR("RHICore: missing compute shader {}", ComputeShader.c_str());
            return {};
        }

        return RHI::CreateComputePipeline(Compute->Source());
    }
}
