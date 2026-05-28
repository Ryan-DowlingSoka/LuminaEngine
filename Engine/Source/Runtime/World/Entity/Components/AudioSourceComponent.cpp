#include "pch.h"
#include "AudioSourceComponent.h"

#include "Audio/AudioGlobals.h"

namespace Lumina
{
	void SAudioSourceComponent::Play()
	{
		if (bPlaying && ActiveHandle.IsValid())
		{
			GAudioContext->StopSound(ActiveHandle);
		}

		if (SoundFile.empty())
		{
			return;
		}

		ActiveHandle = GAudioContext->PlaySoundAtLocation(
			FStringView(SoundFile),
			FVector3(0.0f), // Position will be set by the system from the transform.
			Volume,
			Pitch,
			MinDistance,
			MaxDistance,
			bLooping
		);

		bPlaying = true;
	}

	void SAudioSourceComponent::Stop(EAudioStopMode Mode)
	{
		if (bPlaying && ActiveHandle.IsValid())
		{
			GAudioContext->StopSound(ActiveHandle, Mode);
			ActiveHandle = FAudioHandle::Invalid();
			bPlaying = false;
		}
	}
}
