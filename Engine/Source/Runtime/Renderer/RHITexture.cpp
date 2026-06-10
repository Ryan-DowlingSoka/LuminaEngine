#include "pch.h"
#include "RHITexture.h"
#include "Core/Templates/LuminaTemplate.h"

#include "Core/Math/Math.h"
#include "Core/Threading/Thread.h"

#ifdef CreateSemaphore
#undef CreateSemaphore
#endif

namespace Lumina::RHI::Textures
{
    struct FStorageSlot
    {
        uint64 TextureHandle;
        uint32 Mip;
        uint32 Slot;
    };

    struct FPendingRelease
    {
        FManagedTexture Texture;
        TVector<uint32> StorageSlots;
        uint32          TicksRemaining;
    };

    struct FState
    {
        FSemaphoreH         UploadSemaphore;
        uint64              UploadCounter = 0;
        FMutex              UploadMutex;

        TVector<FStorageSlot> StorageSlots;
        FMutex                StorageMutex;

        TVector<FPendingRelease> PendingReleases;
        FMutex                   ReleaseMutex;

        FManagedTexture     Default;
        bool                bInitialized = false;
    };

    static FState GState;

    void Initialize()
    {
        GState.UploadSemaphore = CreateSemaphore(0);
        GState.bInitialized    = true;

        // 1x1 magenta placeholder: distinct enough that a missing/invalid texture is obvious.
        GState.Default = Create(FTexture2DDesc{ .Width = 1, .Height = 1, .Format = EFormat::RGBA8_UNORM });
        const uint8 Magenta[4] = { 255, 0, 255, 255 };
        Upload(GState.Default, 0, Magenta, sizeof(Magenta), 1);
    }

    void Shutdown()
    {
        if (!GState.bInitialized)
        {
            return;
        }

        WaitDeviceIdle();

        // Flush every deferred release immediately; the device is idle.
        {
            FScopeLock Lock(GState.ReleaseMutex);
            for (FPendingRelease& Pending : GState.PendingReleases)
            {
                for (uint32 Slot : Pending.StorageSlots)
                {
                    HeapFreeRWTexture(Core::GetGlobalHeap(), Slot);
                }
                if (Pending.Texture.SampledSlot != kInvalidHeapSlot)
                {
                    HeapFreeTexture(Core::GetGlobalHeap(), Pending.Texture.SampledSlot);
                }
                FreeH(Pending.Texture.Texture);
            }
            GState.PendingReleases.clear();
        }

        if (GState.Default.SampledSlot != kInvalidHeapSlot)
        {
            HeapFreeTexture(Core::GetGlobalHeap(), GState.Default.SampledSlot);
        }
        FreeH(GState.Default.Texture);
        GState.Default = FManagedTexture{};

        FreeH(GState.UploadSemaphore);
        GState.bInitialized = false;
    }

    void Tick()
    {
        FScopeLock Lock(GState.ReleaseMutex);

        for (size_t i = 0; i < GState.PendingReleases.size(); )
        {
            FPendingRelease& Pending = GState.PendingReleases[i];
            if (Pending.TicksRemaining > 0)
            {
                --Pending.TicksRemaining;
                ++i;
                continue;
            }

            for (uint32 Slot : Pending.StorageSlots)
            {
                HeapFreeRWTexture(Core::GetGlobalHeap(), Slot);
            }
            if (Pending.Texture.SampledSlot != kInvalidHeapSlot)
            {
                HeapFreeTexture(Core::GetGlobalHeap(), Pending.Texture.SampledSlot);
            }
            FreeH(Pending.Texture.Texture);

            GState.PendingReleases[i] = Move(GState.PendingReleases.back());
            GState.PendingReleases.pop_back();
        }
    }

    FManagedTexture Create(const FTexture2DDesc& Desc)
    {
        FTextureDesc TextureDesc;
        TextureDesc.Type      = ETextureType::Tex2D;
        TextureDesc.Dimension = FUIntVector3(Math::Max(Desc.Width, 1u), Math::Max(Desc.Height, 1u), 1u);
        TextureDesc.MipCount  = Math::Max(Desc.Mips, 1u);
        TextureDesc.Format    = Desc.Format;
        TextureDesc.Usage     = EImageUsageFlags::Sampled | EImageUsageFlags::TransferDst | EImageUsageFlags::TransferSrc;
        if (Desc.bStorage)
        {
            TextureDesc.Usage |= EImageUsageFlags::Storage;
        }
        if (Desc.bRenderTarget)
        {
            TextureDesc.Usage |= EImageUsageFlags::ColorAttachment;
        }

        FManagedTexture Out;
        Out.Texture     = CreateTexture(TextureDesc);
        Out.SampledSlot = HeapWriteTexture(Core::GetGlobalHeap(), Out.Texture);
        return Out;
    }

    void Upload(const FManagedTexture& Tex, uint32 Mip, const void* Data, uint64 Size, uint32 RowPitchTexels)
    {
        if (!Tex.IsValid() || Data == nullptr || Size == 0)
        {
            return;
        }

        const GPUPtr Staging = Malloc(Size, kDefaultAlign, EMemoryType::CPUWrite);
        Memory::Memcpy(ToHost(Staging), Data, Size);

        FTextureSlice Slice;
        Slice.Mip = Mip;

        FScopeLock Lock(GState.UploadMutex);

        const FCmdListH CL = OpenCommandList(EQueueType::Graphics);
        CmdCopyMemoryToTexture(CL, Staging, RowPitchTexels, Tex.Texture, Slice);

        const uint64 Value = ++GState.UploadCounter;
        const FSemaphoreInfo Signal { GState.UploadSemaphore, Value, EStageFlags::AllCommands };
        Submit(EQueueType::Graphics, TSpan{&CL, 1}, {}, TSpan{&Signal, 1});

        WaitSemaphore(GState.UploadSemaphore, Value);
        ResetCommandList(CL);

        Free(Staging);
    }

    void Clear(const FManagedTexture& Tex, const float Value[4])
    {
        if (!Tex.IsValid())
        {
            return;
        }

        FScopeLock Lock(GState.UploadMutex);

        const FCmdListH CL = OpenCommandList(EQueueType::Graphics);
        CmdClearTexture(CL, Tex.Texture, Value);

        const uint64 Counter = ++GState.UploadCounter;
        const FSemaphoreInfo Signal { GState.UploadSemaphore, Counter, EStageFlags::AllCommands };
        Submit(EQueueType::Graphics, TSpan{&CL, 1}, {}, TSpan{&Signal, 1});

        WaitSemaphore(GState.UploadSemaphore, Counter);
        ResetCommandList(CL);
    }

    uint32 StorageSlot(const FManagedTexture& Tex, uint32 Mip)
    {
        const uint64 Handle = Tex.Texture.Handle;

        FScopeLock Lock(GState.StorageMutex);
        for (const FStorageSlot& Existing : GState.StorageSlots)
        {
            if (Existing.TextureHandle == Handle && Existing.Mip == Mip)
            {
                return Existing.Slot;
            }
        }

        const uint32 Slot = HeapWriteRWTexture(Core::GetGlobalHeap(), Tex.Texture, Mip);
        if (Slot != kInvalidHeapSlot)
        {
            GState.StorageSlots.push_back(FStorageSlot{ Handle, Mip, Slot });
        }
        return Slot;
    }

    void Release(FManagedTexture& Tex)
    {
        if (!Tex.IsValid())
        {
            return;
        }

        FPendingRelease Pending;
        Pending.Texture        = Tex;
        Pending.TicksRemaining = kFramesInFlight;

        // Collect (and forget) any storage slots this texture registered.
        {
            const uint64 Handle = Tex.Texture.Handle;
            FScopeLock Lock(GState.StorageMutex);
            for (size_t i = 0; i < GState.StorageSlots.size(); )
            {
                if (GState.StorageSlots[i].TextureHandle == Handle)
                {
                    Pending.StorageSlots.push_back(GState.StorageSlots[i].Slot);
                    GState.StorageSlots[i] = GState.StorageSlots.back();
                    GState.StorageSlots.pop_back();
                }
                else
                {
                    ++i;
                }
            }
        }

        {
            FScopeLock Lock(GState.ReleaseMutex);
            GState.PendingReleases.push_back(Move(Pending));
        }

        Tex = FManagedTexture{};
    }

    uint32 DefaultResourceID()
    {
        return GState.Default.SampledSlot;
    }
}
