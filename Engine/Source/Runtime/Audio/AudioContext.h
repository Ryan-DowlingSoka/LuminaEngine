#pragma once

#include "AudioTypes.h"
#include "AudioCommand.h"
#include "Containers/String.h"
#include "Memory/SmartPtr.h"
#include "Platform/Platform.h"

namespace Lumina
{
    class FProceduralAudioStream;

	namespace Audio
	{
		void Initialize();
		void Shutdown();
		// Per-frame pump (drains queued commands + housekeeping). Call once per frame from the engine.
		void Update();
	}

	// Thread-safe; commands queue and are drained by a per-frame pump job on the task pool.
	class RUNTIME_API IAudioContext
	{
	public:

		virtual ~IAudioContext() = default;

		// Drain queued audio commands + housekeeping (kicked once per frame). Default no-op.
		virtual void Update() {}

		NODISCARD virtual void* GetNative() const = 0;

		NODISCARD virtual FAudioHandle PlaySound2D(FStringView File, float Volume = 1.0f, float Pitch = 1.0f, bool bLooping = false) = 0;

		NODISCARD virtual FAudioHandle PlaySoundAtLocation(FStringView File, FVector3 Location,
			float Volume = 1.0f, float Pitch = 1.0f, float MinDistance = 1.0f, float MaxDistance = 50.0f, bool bLooping = false) = 0;

		virtual void StopSound(FAudioHandle Handle, EAudioStopMode Mode = EAudioStopMode::Immediate) = 0;
		virtual void SetVolume(FAudioHandle Handle, float Volume) = 0;
		virtual void SetPitch(FAudioHandle Handle, float Pitch) = 0;
		virtual void SetLooping(FAudioHandle Handle, bool bLooping) = 0;
		virtual void SetPosition(FAudioHandle Handle, FVector3 Position) = 0;
		virtual void SetMinMaxDistance(FAudioHandle Handle, float MinDistance, float MaxDistance) = 0;

		virtual void UpdateListenerPosition(FVector3 Location, FQuat Rotation) = 0;

		virtual void StopAllSounds() = 0;

		// Allocates a streaming PCM buffer (float32). Caller pushes samples via the returned stream;
		// playback is started by passing the stream to PlayProceduralStream.
		NODISCARD virtual TSharedPtr<FProceduralAudioStream> CreateProceduralStream(
			uint32 SampleRate, uint32 ChannelCount, uint32 BufferFrames) = 0;

		NODISCARD virtual FAudioHandle PlayProceduralStream(TSharedPtr<FProceduralAudioStream> Stream,
			bool bSpatialized, FVector3 Position, float Volume = 1.0f, float Pitch = 1.0f,
			float MinDistance = 1.0f, float MaxDistance = 50.0f) = 0;
	};
}
