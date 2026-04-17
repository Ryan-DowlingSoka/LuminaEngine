#pragma once

#include "Audio/AudioContext.h"
#include "Containers/Array.h"
#include "Core/Threading/Thread.h"
#include "Core/Threading/Atomic.h"
#include "Memory/SmartPtr.h"
#include "MiniAudio/miniaudio.h"

namespace Lumina
{
	class FMiniaudioContext final : public IAudioContext
	{
	public:

		FMiniaudioContext();
		~FMiniaudioContext() override;

		void* GetNative() const override { return (void*)&Engine; }

		FAudioHandle PlaySound2D(FStringView File, float Volume, float Pitch, bool bLooping) override;
		FAudioHandle PlaySoundAtLocation(FStringView File, glm::vec3 Location,
			float Volume, float Pitch, float MinDistance, float MaxDistance, bool bLooping) override;

		void StopSound(FAudioHandle Handle, EAudioStopMode Mode) override;
		void SetVolume(FAudioHandle Handle, float Volume) override;
		void SetPitch(FAudioHandle Handle, float Pitch) override;
		void SetLooping(FAudioHandle Handle, bool bLooping) override;
		void SetPosition(FAudioHandle Handle, glm::vec3 Position) override;
		void SetMinMaxDistance(FAudioHandle Handle, float MinDistance, float MaxDistance) override;
		void UpdateListenerPosition(glm::vec3 Location, glm::quat Rotation) override;
		void StopAllSounds() override;

	private:

		// Allocate a handle on the game thread (thread-safe via atomic).
		FAudioHandle AllocateHandle();

		// The audio thread entry point.
		void AudioThreadMain();

		// Process a single command on the audio thread.
		void ProcessCommand(const FAudioCommand& Cmd);

		// Cleanup finished sounds on the audio thread.
		void CleanupFinishedSounds();

		struct FActiveSound
		{
			FAudioHandle Handle;
			TVector<uint8> Bytes;
			ma_decoder Decoder;
			ma_sound Sound;
			bool bInitialized = false;
		};

		// Finds an active sound by handle. Returns nullptr if not found.
		FActiveSound* FindSound(FAudioHandle Handle);

		// Stops and uninitializes a sound. Does not remove it from the list.
		void UninitSound(FActiveSound& Sound);

		ma_engine Engine;

		TConcurrentQueue<FAudioCommand> CommandQueue;

		FThread AudioThread;
		TAtomic<bool> bRunning{false};

		TAtomic<uint32> NextGeneration{1};
		
		TVector<TUniquePtr<FActiveSound>> ActiveSounds;
	};
}
