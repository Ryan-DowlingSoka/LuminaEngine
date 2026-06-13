#pragma once

#include "Audio/AudioContext.h"
#include "Audio/ProceduralAudioStream.h"
#include "Containers/Array.h"
#include "Core/Threading/Thread.h"
#include "Core/Threading/Atomic.h"
#include "Memory/SmartPtr.h"
#include "MiniAudio/miniaudio.h"

namespace Lumina::Jobs { struct FCounter; }

namespace Lumina
{
	class FMiniaudioContext final : public IAudioContext
	{
	public:

		FMiniaudioContext();
		~FMiniaudioContext() override;

		// Kicks a single-in-flight pump job onto the task pool (drains commands + housekeeping).
		void Update() override;

		void* GetNative() const override { return (void*)&Engine; }

		FAudioHandle PlaySound2D(FStringView File, float Volume, float Pitch, bool bLooping) override;
		FAudioHandle PlaySoundAtLocation(FStringView File, FVector3 Location,
			float Volume, float Pitch, float MinDistance, float MaxDistance, bool bLooping) override;

		FAudioHandle PlayAudio2D(const TSharedPtr<FAudioData>& Data, float Volume, float Pitch, bool bLooping, uint64 StartFrame) override;
		FAudioHandle PlayAudioAtLocation(const TSharedPtr<FAudioData>& Data, FVector3 Location,
			float Volume, float Pitch, float MinDistance, float MaxDistance, bool bLooping, uint64 StartFrame) override;

		void StopSound(FAudioHandle Handle, EAudioStopMode Mode) override;
		void SetVolume(FAudioHandle Handle, float Volume) override;
		void SetPitch(FAudioHandle Handle, float Pitch) override;
		void SetLooping(FAudioHandle Handle, bool bLooping) override;
		void SetPosition(FAudioHandle Handle, FVector3 Position) override;
		void SetMinMaxDistance(FAudioHandle Handle, float MinDistance, float MaxDistance) override;
		void SeekToFrame(FAudioHandle Handle, uint64 Frame) override;
		void UpdateListenerPosition(FVector3 Location, FQuat Rotation) override;
		void StopAllSounds() override;

		TSharedPtr<FProceduralAudioStream> CreateProceduralStream(uint32 SampleRate, uint32 ChannelCount, uint32 BufferFrames) override;
		FAudioHandle PlayProceduralStream(TSharedPtr<FProceduralAudioStream> Stream, bool bSpatialized,
			FVector3 Position, float Volume, float Pitch, float MinDistance, float MaxDistance) override;

	private:

		FAudioHandle AllocateHandle();
		FAudioHandle PlayAudioInternal(const TSharedPtr<FAudioData>& Data, bool bSpatialized,
			FVector3 Position, float Volume, float Pitch, float MinDistance, float MaxDistance, bool bLooping, uint64 StartFrame);
		void PumpOnce();                         // one drain pass: commands + procedural + cleanup
		static void PumpEntry(void* Arg, uint32 WorkerIndex);
		void ProcessCommand(const FAudioCommand& Cmd);
		void CleanupFinishedSounds();

		struct FActiveSound
		{
			FAudioHandle Handle;
			TVector<uint8> Bytes;
			ma_decoder Decoder;
			ma_sound Sound;
			bool bInitialized = false;

			// When non-null, the decoder reads from this shared asset data instead of Bytes; the ref
			// keeps the bytes alive even if the owning asset is unloaded mid-playback.
			TSharedPtr<FAudioData> Source;

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
			FVector3 Position;
			float Volume;
			float Pitch;
			float MinDistance;
			float MaxDistance;
		};
		TConcurrentQueue<FPendingProceduralStart> PendingProceduralStarts;
		void ProcessPendingProceduralStart(const FPendingProceduralStart& Start);

		// Side queue for asset-backed Play commands; the unioned FAudioCommand can't carry a TSharedPtr.
		struct FPendingDataPlay
		{
			FAudioHandle Handle;
			TSharedPtr<FAudioData> Data;
			bool bSpatialized;
			FVector3 Position;
			float Volume;
			float Pitch;
			float MinDistance;
			float MaxDistance;
			bool bLooping;
			uint64 StartFrame;
		};
		TConcurrentQueue<FPendingDataPlay> PendingDataPlays;
		void ProcessPendingDataPlay(FPendingDataPlay& Play);

		FActiveSound* FindSound(FAudioHandle Handle);
		void UninitSound(FActiveSound& Sound);

		ma_engine Engine;

		TConcurrentQueue<FAudioCommand> CommandQueue;

		TAtomic<bool>   bRunning{false};
		TAtomic<bool>   bPumpActive{false};      // exactly one pump job in flight at a time
		Jobs::FCounter* PumpCounter = nullptr;   // tracks the in-flight pump job (for shutdown drain)

		TAtomic<uint32> NextGeneration{1};
		
		TVector<TUniquePtr<FActiveSound>> ActiveSounds;
	};
}
