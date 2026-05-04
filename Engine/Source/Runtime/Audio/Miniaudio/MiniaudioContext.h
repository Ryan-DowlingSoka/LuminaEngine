#pragma once

#include "Audio/AudioContext.h"
#include "Audio/ProceduralAudioStream.h"
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

		TSharedPtr<FProceduralAudioStream> CreateProceduralStream(uint32 SampleRate, uint32 ChannelCount, uint32 BufferFrames) override;
		FAudioHandle PlayProceduralStream(TSharedPtr<FProceduralAudioStream> Stream, bool bSpatialized,
			glm::vec3 Position, float Volume, float Pitch, float MinDistance, float MaxDistance) override;

	private:

		FAudioHandle AllocateHandle();
		void AudioThreadMain();
		void ProcessCommand(const FAudioCommand& Cmd);
		void CleanupFinishedSounds();

		struct FActiveSound
		{
			FAudioHandle Handle;
			TVector<uint8> Bytes;
			ma_decoder Decoder;
			ma_sound Sound;
			bool bInitialized = false;

			// When non-null, this is a procedural sound; ma_sound is attached to Procedural's ring buffer
			// instead of Decoder. Procedural sounds are never auto-cleaned by CleanupFinishedSounds.
			TSharedPtr<FProceduralAudioStream> Procedural;
		};

		// Side queue for procedural Play commands; the unioned FAudioCommand can't carry a TSharedPtr.
		struct FPendingProceduralStart
		{
			FAudioHandle Handle;
			TSharedPtr<FProceduralAudioStream> Stream;
			bool bSpatialized;
			glm::vec3 Position;
			float Volume;
			float Pitch;
			float MinDistance;
			float MaxDistance;
		};
		TConcurrentQueue<FPendingProceduralStart> PendingProceduralStarts;
		void ProcessPendingProceduralStart(const FPendingProceduralStart& Start);

		FActiveSound* FindSound(FAudioHandle Handle);
		void UninitSound(FActiveSound& Sound);

		ma_engine Engine;

		TConcurrentQueue<FAudioCommand> CommandQueue;

		FThread AudioThread;
		TAtomic<bool> bRunning{false};

		TAtomic<uint32> NextGeneration{1};
		
		TVector<TUniquePtr<FActiveSound>> ActiveSounds;
	};
}
