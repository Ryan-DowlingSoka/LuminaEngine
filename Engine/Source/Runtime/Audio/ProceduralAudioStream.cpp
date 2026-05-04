#include "pch.h"
#include "ProceduralAudioStream.h"

#include "Log/Log.h"
#include "Memory/Memory.h"
#include "Memory/Memcpy.h"

namespace Lumina
{
    static void* MAStreamMalloc(size_t Size, void*) { return Memory::Malloc(Size); }
    static void* MAStreamRealloc(void* P, size_t Size, void*) { return Memory::Realloc(P, Size); }
    static void  MAStreamFree(void* P, void*) { Memory::Free(P); }

    FProceduralAudioStream::FProceduralAudioStream(uint32 InSampleRate, uint32 InChannelCount, uint32 BufferFrames)
        : SampleRate(InSampleRate)
        , ChannelCount(InChannelCount)
    {
        if (InSampleRate == 0 || InChannelCount == 0 || BufferFrames == 0)
        {
            LOG_ERROR("FProceduralAudioStream: invalid parameters (rate={}, channels={}, frames={})",
                InSampleRate, InChannelCount, BufferFrames);
            return;
        }

        ma_allocation_callbacks Callbacks;
        Callbacks.pUserData = nullptr;
        Callbacks.onMalloc  = MAStreamMalloc;
        Callbacks.onRealloc = MAStreamRealloc;
        Callbacks.onFree    = MAStreamFree;

        ma_result Result = ma_pcm_rb_init(ma_format_f32, InChannelCount, BufferFrames, nullptr, &Callbacks, &RingBuffer);
        if (Result != MA_SUCCESS)
        {
            LOG_ERROR("FProceduralAudioStream: ma_pcm_rb_init failed ({})", (int)Result);
            return;
        }

        ma_pcm_rb_set_sample_rate(&RingBuffer, InSampleRate);
        bInitialized = true;
    }

    FProceduralAudioStream::~FProceduralAudioStream()
    {
        if (bInitialized)
        {
            ma_pcm_rb_uninit(&RingBuffer);
            bInitialized = false;
        }
    }

    uint32 FProceduralAudioStream::Write(const float* Samples, uint32 NumFrames)
    {
        if (!bInitialized || Samples == nullptr || NumFrames == 0)
        {
            return 0;
        }

        uint32 FramesRemaining = NumFrames;
        uint32 FramesWritten   = 0;
        const float* Src = Samples;

        // Loop because ma_pcm_rb may give a smaller contiguous region near the wrap.
        while (FramesRemaining > 0)
        {
            ma_uint32 RegionFrames = FramesRemaining;
            void* Region = nullptr;

            if (ma_pcm_rb_acquire_write(&RingBuffer, &RegionFrames, &Region) != MA_SUCCESS || RegionFrames == 0)
            {
                break;
            }

            const size_t Bytes = (size_t)RegionFrames * ChannelCount * sizeof(float);
            Memory::Memcpy(Region, Src, Bytes);

            if (ma_pcm_rb_commit_write(&RingBuffer, RegionFrames) != MA_SUCCESS)
            {
                break;
            }

            Src           += (size_t)RegionFrames * ChannelCount;
            FramesWritten += RegionFrames;
            FramesRemaining -= RegionFrames;
        }

        return FramesWritten;
    }

    uint32 FProceduralAudioStream::GetAvailableWriteFrames()
    {
        return bInitialized ? ma_pcm_rb_available_write(&RingBuffer) : 0;
    }

    uint32 FProceduralAudioStream::GetAvailableReadFrames()
    {
        return bInitialized ? ma_pcm_rb_available_read(&RingBuffer) : 0;
    }
}
