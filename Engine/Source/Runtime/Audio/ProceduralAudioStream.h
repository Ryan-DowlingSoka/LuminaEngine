#pragma once

#include "Platform/GenericPlatform.h"
#include "MiniAudio/miniaudio.h"

namespace Lumina
{
    // Lock-free SPSC ring buffer for streaming PCM float samples to the audio thread.
    // Producer is whoever calls Write (game/Lua thread). Consumer is the audio thread.
    class RUNTIME_API FProceduralAudioStream
    {
    public:

        FProceduralAudioStream(uint32 InSampleRate, uint32 InChannelCount, uint32 BufferFrames);
        ~FProceduralAudioStream();

        FProceduralAudioStream(const FProceduralAudioStream&) = delete;
        FProceduralAudioStream& operator=(const FProceduralAudioStream&) = delete;

        // Writes up to NumFrames of interleaved float samples. Returns frames actually written.
        uint32 Write(const float* Samples, uint32 NumFrames);

        uint32 GetAvailableWriteFrames();
        uint32 GetAvailableReadFrames();

        uint32 GetSampleRate() const { return SampleRate; }
        uint32 GetChannelCount() const { return ChannelCount; }

        // Underlying miniaudio data source — only the audio thread should pass this to ma_sound.
        ma_data_source* GetDataSource() { return bInitialized ? (ma_data_source*)&RingBuffer : nullptr; }

    private:

        ma_pcm_rb RingBuffer{};
        uint32 SampleRate = 0;
        uint32 ChannelCount = 0;
        bool bInitialized = false;
    };
}
