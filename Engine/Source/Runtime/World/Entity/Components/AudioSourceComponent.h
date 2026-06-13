#pragma once

#include "Audio/AudioTypes.h"
#include "Core/Object/ObjectMacros.h"
#include "Core/Object/ObjectHandleTyped.h"
#include "AudioSourceComponent.generated.h"

namespace Lumina
{
	class CAudioStream;

	REFLECT(Component, Category = "Audio")
	struct RUNTIME_API SAudioSourceComponent
	{
		GENERATED_BODY()

		/** Audio asset to play. */
		PROPERTY(Script, Editable)
		TObjectPtr<CAudioStream> Sound;

		/** Playback volume multiplier (1.0 = full volume). */
		PROPERTY(Script, Editable)
		float Volume = 1.0f;

		/** Playback pitch multiplier (1.0 = original pitch). */
		PROPERTY(Script, Editable)
		float Pitch = 1.0f;

		/** Distance (meters) at which the sound begins to attenuate. */
		PROPERTY(Script, Editable)
		float MinDistance = 1.0f;

		/** Distance (meters) beyond which the sound is inaudible. */
		PROPERTY(Script, Editable)
		float MaxDistance = 50.0f;

		/** When true, the sound restarts automatically upon completion. */
		PROPERTY(Script, Editable)
		bool bLooping = false;

		/** When true, playback starts automatically once the component is initialized. */
		PROPERTY(Script, Editable)
		bool bPlayOnReady = false;
		
		// Handle to the currently playing sound instance.
		FAudioHandle ActiveHandle;

		// Set once the component has been initialized by the audio system.
		bool bReady = false;

		// True when the sound is currently playing.
		bool bPlaying = false;

		// Dirty flags for parameter changes.
		bool bVolumeDirty  = false;
		bool bPitchDirty   = false;
		bool bLoopingDirty = false;

		void Play();
		void Stop(EAudioStopMode Mode = EAudioStopMode::Immediate);
	};

	REFLECT(Component, Category = "Audio")
	struct RUNTIME_API SAudioListenerComponent
	{
		GENERATED_BODY()
	};
}
