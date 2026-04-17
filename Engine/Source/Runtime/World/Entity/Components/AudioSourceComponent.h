#pragma once

#include "Audio/AudioTypes.h"
#include "Core/Object/ObjectMacros.h"
#include "AudioSourceComponent.generated.h"

namespace Lumina
{
	REFLECT(Component)
	struct RUNTIME_API SAudioSourceComponent
	{
		GENERATED_BODY()

		PROPERTY(Script, Editable)
		FString SoundFile;

		PROPERTY(Script, Editable)
		float Volume = 1.0f;

		PROPERTY(Script, Editable)
		float Pitch = 1.0f;

		PROPERTY(Script, Editable)
		float MinDistance = 1.0f;

		PROPERTY(Script, Editable)
		float MaxDistance = 50.0f;

		PROPERTY(Script, Editable)
		bool bLooping = false;

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

	REFLECT(Component)
	struct RUNTIME_API SAudioListenerComponent
	{
		GENERATED_BODY()

		uint8 Foobar;
	};
}
